#!/usr/bin/env python3
"""Transparent TCP relay used by the Basilisk II test setup.

Basilisk II's slirp stack answers for 10.0.2.2, but this host's Jellyfin
server lives on the LAN at 10.0.1.1.  The guest connects to 10.0.2.2:8096;
this relay forwards the bytes to 10.0.1.1:8096.  It is a plain TCP pipe --
Jellyfin still receives HTTP requests and performs the MP3 transcode itself.
"""
import argparse
import select
import socket
import sys
import threading


def pipe(a: socket.socket, b: socket.socket) -> None:
    try:
        while True:
            readable, _, _ = select.select([a, b], [], [], 60)
            if not readable:
                continue
            for src, dst in ((a, b), (b, a)):
                if src in readable:
                    data = src.recv(65536)
                    if not data:
                        return
                    dst.sendall(data)
    except (OSError, ValueError):
        pass
    finally:
        for sock in (a, b):
            try:
                sock.close()
            except OSError:
                pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--listen", default="0.0.0.0:8096",
                        help="address/port the emulator connects to")
    parser.add_argument("--target", default="10.0.1.1:8096",
                        help="Jellyfin server address/port")
    args = parser.parse_args()

    listen_host, listen_port = args.listen.rsplit(":", 1)
    target_host, target_port = args.target.rsplit(":", 1)
    target = (target_host, int(target_port))

    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind((listen_host, int(listen_port)))
    server.listen(20)
    print(f"relay listening on {args.listen} -> {args.target}", flush=True)

    try:
        while True:
            client, peer = server.accept()
            try:
                upstream = socket.create_connection(target, timeout=10)
            except OSError as exc:
                print(f"connect to {args.target} failed: {exc}", file=sys.stderr,
                      flush=True)
                client.close()
                continue
            print(f"connection from {peer} -> {args.target}", flush=True)
            threading.Thread(target=pipe, args=(client, upstream),
                             daemon=True).start()
    except KeyboardInterrupt:
        print("\nrelay stopped", flush=True)
    finally:
        server.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
