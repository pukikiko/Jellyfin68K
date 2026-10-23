#!/usr/bin/env bash
# One-time setup: check prerequisites, get the toolchain, and build the sample.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${RETRO68_IMAGE:-ghcr.io/autc04/retro68:latest}"
missing=0

section() {
    printf '\n== %s ==\n' "$1"
}

section "Host tools"

if [[ -n "${RETRO68_TOOLCHAIN:-}" ]]; then
    echo "ok: using existing Retro68 toolchain at $RETRO68_TOOLCHAIN"
elif command -v docker >/dev/null 2>&1; then
    if docker info >/dev/null 2>&1; then
        echo "ok: Docker daemon is available"
    else
        echo "error: Docker is installed but the daemon is not reachable" >&2
        missing=1
    fi
else
    echo "error: install Docker, or set RETRO68_TOOLCHAIN" >&2
    echo "       https://docs.docker.com/engine/install/" >&2
    missing=1
fi

if command -v BasiliskII >/dev/null 2>&1; then
    echo "ok: BasiliskII is installed"
else
    echo "missing: BasiliskII emulator" >&2
    cat >&2 <<'EOF'
  Arch Linux:  yay -S basiliskii-git
  Debian/Ubuntu: sudo apt install basilisk2
  Or build: https://github.com/kanjitalk755/macemu
EOF
    missing=1
fi

section "Mac ROM and system disk"

if [[ -f "$ROOT/images/mac.rom" ]]; then
    echo "ok: images/mac.rom"
else
    echo "missing: images/mac.rom" >&2
    missing=1
fi

if [[ -f "$ROOT/images/system.img" ]]; then
    echo "ok: images/system.img"
else
    echo "missing: images/system.img" >&2
    missing=1
fi

if ((missing)); then
    cat >&2 <<'EOF'

Install the missing pieces, then run `make setup` again.

To install the Mac images:
  scripts/setup-images.sh --rom /path/to/ROM --system /path/to/System.img

Or download the System 7.5.3 test image (bring your own ROM):
  scripts/setup-images.sh --fetch-system
EOF
    exit 1
fi

section "Retro68 toolchain"

if [[ -n "${RETRO68_TOOLCHAIN:-}" ]]; then
    echo "Using local toolchain: $RETRO68_TOOLCHAIN"
else
    if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
        echo "Pulling $IMAGE (about 4 GB)..."
        docker pull "$IMAGE"
    else
        echo "ok: $IMAGE is already present"
    fi
fi

section "Jellyfin 68K client"
"$ROOT/scripts/build.sh"
"$ROOT/scripts/configure-emulator.sh"

cat <<'EOF'

Setup complete.

Next steps:
  1. Install MacTCP 2.0.6 in the System 7 image (see README, "MacTCP").
     On each boot, open Control Panels -> MacTCP and close it once so the
     .IPP driver is installed, then launch the client.
  2. shared/Jellyfin68K.cfg is created on the first `make run`.
     The emulator default is host=10.0.2.2 (slirp + relay).
  3. In one terminal:  make relay
     In another:       make run

  make build  # rebuild after editing app/
  make shell  # shell with the cross-compiler in PATH
  make doctor # re-check the environment

EOF
