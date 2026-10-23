#!/usr/bin/env bash
# Install MacTCP 2.0.6 into a Basilisk II system image and preconfigure it
# for the emulator's slirp network.
#
# Usage:
#   scripts/install-mactcp.sh /path/to/MacTCP.bin
#   scripts/install-mactcp.sh /path/to/mactcp.dsk
#
# MacTCP is Apple software and is not distributed with this project.  The
# input may be either a MacBinary II `.bin` file or the original MacTCP
# 2.0.6 HFS disk image.  The script patches the driver's saved configuration
# and installs it in both Control Panels and Startup Items.  On each boot,
# open Control Panels -> MacTCP and close it once to load the .IPP driver.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STATE="${MAC_STATE_DIR:-$ROOT/.state}"
IMAGE="$STATE/system.img"
IP="${MACTCP_IP:-10.0.2.15}"
MASK="${MACTCP_MASK:-255.0.0.0}"
DNS="${MACTCP_DNS:-10.0.2.3}"
IMAGE_TAG="${RETRO68_IMAGE:-ghcr.io/autc04/retro68:latest}"

usage() {
    cat >&2 <<'EOF'
usage: scripts/install-mactcp.sh INPUT

  INPUT  MacTCP MacBinary (.bin) or MacTCP 2.0.6 disk image (.dsk/.img)

Environment:
  MAC_STATE_DIR   emulator state directory (default .state)
  MACTCP_IP       guest IP (default 10.0.2.15)
  MACTCP_MASK     subnet mask (default 255.0.0.0)
  MACTCP_DNS      DNS server (default 10.0.2.3)
EOF
    exit 2
}

[[ $# -ge 1 ]] || usage
INPUT="$1"
[[ -f "$INPUT" ]] || { echo "error: no such file: $INPUT" >&2; exit 1; }
mkdir -p "$STATE"
if [[ ! -f "$IMAGE" ]]; then
    if [[ -f "$ROOT/images/system.img" ]]; then
        cp --reflink=auto -- "$ROOT/images/system.img" "$IMAGE"
    else
        echo "error: system image not found: $IMAGE" >&2
        echo "Run 'make run' (or scripts/configure-emulator.sh) once first." >&2
        exit 1
    fi
fi
if pgrep -x BasiliskII >/dev/null 2>&1; then
    echo "error: quit Basilisk II before modifying the system image" >&2
    exit 1
fi
command -v docker >/dev/null 2>&1 || {
    echo "error: Docker is required for hfsutils" >&2
    exit 1
}

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

# Normalize the input to a MacBinary file.
if [[ "$INPUT" == *.bin ]]; then
    cp -- "$INPUT" "$TMP/MacTCP.bin"
else
    cp -- "$INPUT" "$TMP/mactcp.dsk"
    docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
        -v "$TMP:/host" -w /host "$IMAGE_TAG" bash -lc '
            set -e
            export PATH=/Retro68-build/toolchain/bin:$PATH
            hmount /host/mactcp.dsk >/dev/null
            hcopy -m :MacTCP /host/MacTCP.bin
            humount
        '
fi
[[ -s "$TMP/MacTCP.bin" ]] || { echo "error: MacTCP extraction failed" >&2; exit 1; }

# Patch the saved IP/mask/DNS configuration in the driver resource.
python3 - "$TMP/MacTCP.bin" "$IP" "$MASK" "$DNS" <<'PY'
import sys

path, ip, mask, dns = sys.argv[1:5]

def addr(text):
    parts = [int(p) for p in text.split(".")]
    if len(parts) != 4 or any(p < 0 or p > 255 for p in parts):
        raise SystemExit(f"bad dotted address: {text}")
    return bytes(parts)

data = bytearray(open(path, "rb").read())
needle = b"\x00\x00\x00\x81\x00\x00\x00\x01"
pos = data.find(needle)
if pos < 0:
    raise SystemExit("MacTCP configuration block not found (unexpected version)")
if pos + 28 > len(data):
    raise SystemExit("MacTCP configuration block is truncated")
data[pos + 12:pos + 16] = addr(ip)
data[pos + 16:pos + 20] = addr(mask)
data[pos + 24:pos + 28] = addr(dns)
open(path, "wb").write(data)
print(f"patched MacTCP config: ip={ip} mask={mask} dns={dns}")
PY

docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$STATE:/state" -v "$TMP:/host" -w /host "$IMAGE_TAG" bash -lc '
        set -e
        export PATH=/Retro68-build/toolchain/bin:$PATH
        hmount /state/system.img >/dev/null
        hcopy -m /host/MacTCP.bin ":System Folder:Control Panels:MacTCP"
        hcopy -m /host/MacTCP.bin ":System Folder:Startup Items:MacTCP"
        hls -la ":System Folder:Startup Items" | grep MacTCP || true
        humount
    '

cat <<EOF
Installed MacTCP 2.0.6 into:
  $IMAGE
  :System Folder:Control Panels:MacTCP
  :System Folder:Startup Items:MacTCP
Configured for $IP / $MASK / DNS $DNS.

Boot the emulator, open Control Panels -> MacTCP, then close the window.
That installs the .IPP driver.  Launch Jellyfin68K afterwards.
EOF
