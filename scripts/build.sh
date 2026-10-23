#!/usr/bin/env bash
# Build the sample application with Retro68.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${RETRO68_IMAGE:-ghcr.io/autc04/retro68:latest}"
BUILD_DIR="${BUILD_DIR:-build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

cd "$ROOT"
mkdir -p "$BUILD_DIR"

if [[ -n "${RETRO68_TOOLCHAIN:-}" ]]; then
    TOOLCHAIN_FILE="$RETRO68_TOOLCHAIN/m68k-apple-macos/cmake/retro68.toolchain.cmake"
    [[ -f "$TOOLCHAIN_FILE" ]] || {
        echo "error: Retro68 toolchain file not found: $TOOLCHAIN_FILE" >&2
        exit 1
    }

    cmake -S app -B "$BUILD_DIR" \
        -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE"
    cmake --build "$BUILD_DIR" --parallel
else
    command -v docker >/dev/null 2>&1 || {
        echo "error: Docker is required (or set RETRO68_TOOLCHAIN)" >&2
        exit 1
    }
    docker info >/dev/null 2>&1 || {
        echo "error: the Docker daemon is not reachable" >&2
        exit 1
    }

    if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
        echo "Pulling $IMAGE..."
        docker pull "$IMAGE"
    fi

    docker run --rm \
        --user "$(id -u):$(id -g)" \
        -e HOME=/tmp \
        -e BUILD_TYPE="$BUILD_TYPE" \
        -v "$ROOT:/work" \
        -w /work \
        "$IMAGE" \
        bash -lc '
            set -euo pipefail
            cmake -S app -B build \
                -DCMAKE_BUILD_TYPE="$BUILD_TYPE" \
                -DCMAKE_TOOLCHAIN_FILE=/Retro68-build/toolchain/m68k-apple-macos/cmake/retro68.toolchain.cmake
            cmake --build build --parallel
        '
fi

echo
echo "Primary application:"
ls -lh "$BUILD_DIR"/Jellyfin68K.dsk "$BUILD_DIR"/Jellyfin68K.bin \
    "$BUILD_DIR"/.rsrc/Jellyfin68K.APPL 2>/dev/null || true
echo
echo "Diagnostics: NetProbe, NetTest, MP3Test, VolDump, LaunchJF, Hello68K"
