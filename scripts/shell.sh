#!/usr/bin/env bash
# Open a shell with the Retro68 cross-compiler available.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${RETRO68_IMAGE:-ghcr.io/autc04/retro68:latest}"

if [[ -n "${RETRO68_TOOLCHAIN:-}" ]]; then
    export PATH="$RETRO68_TOOLCHAIN/bin:$PATH"
    echo "Retro68 toolchain: $RETRO68_TOOLCHAIN"
    exec "${SHELL:-/bin/bash}"
fi

command -v docker >/dev/null 2>&1 || {
    echo "error: Docker is required (or set RETRO68_TOOLCHAIN)" >&2
    exit 1
}

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    docker pull "$IMAGE"
fi

exec docker run --rm -it \
    --user "$(id -u):$(id -g)" \
    -e HOME=/tmp \
    -v "$ROOT:/work" \
    -w /work \
    "$IMAGE" \
    bash -l
