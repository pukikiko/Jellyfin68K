#!/usr/bin/env bash
# Diagnose common problems in the classic Mac development environment.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGE="${RETRO68_IMAGE:-ghcr.io/autc04/retro68:latest}"
warnings=0
errors=0

ok()   { printf '  ok    %s\n' "$1"; }
warn() { printf '  warn  %s\n' "$1"; warnings=$((warnings + 1)); }
fail() { printf '  FAIL  %s\n' "$1"; errors=$((errors + 1)); }

echo "Retro68 68K Mac development environment"
echo

echo "Toolchain"
if [[ -n "${RETRO68_TOOLCHAIN:-}" ]]; then
    if [[ -x "$RETRO68_TOOLCHAIN/bin/m68k-apple-macos-gcc" ]]; then
        ok "local toolchain: $RETRO68_TOOLCHAIN"
    else
        fail "RETRO68_TOOLCHAIN is set but m68k-apple-macos-gcc was not found"
    fi
else
    if command -v docker >/dev/null 2>&1; then
        if docker info >/dev/null 2>&1; then
            ok "Docker daemon"
            if docker image inspect "$IMAGE" >/dev/null 2>&1; then
                ok "Retro68 image: $IMAGE"
            else
                warn "Retro68 image not pulled yet (make setup will pull it)"
            fi
        else
            fail "Docker is installed but the daemon is not reachable"
        fi
    else
        fail "neither Docker nor RETRO68_TOOLCHAIN is available"
    fi
fi

echo
echo "Emulator"
if command -v BasiliskII >/dev/null 2>&1; then
    ok "BasiliskII: $(command -v BasiliskII)"
else
    fail "BasiliskII is not installed (Arch: yay -S basiliskii-git)"
fi

if [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
    ok "graphical session: ${WAYLAND_DISPLAY:-$DISPLAY}"
else
    warn "no DISPLAY/WAYLAND_DISPLAY; a graphical session is needed to test"
fi

echo
echo "Mac images"
if [[ -f "$ROOT/images/mac.rom" ]]; then
    ok "ROM: images/mac.rom ($(du -h "$ROOT/images/mac.rom" | cut -f1))"
else
    fail "ROM missing: images/mac.rom"
fi

if [[ -f "$ROOT/images/system.img" ]]; then
    ok "system disk: images/system.img ($(du -h "$ROOT/images/system.img" | cut -f1))"
else
    fail "system disk missing: images/system.img"
fi

echo
echo "Project"
if [[ -f "$ROOT/build/Jellyfin68K.dsk" ]]; then
    ok "client disk: build/Jellyfin68K.dsk"
else
    warn "client has not been built yet (run make build)"
fi

if [[ -f "$ROOT/scripts/jellyfin-relay.py" ]]; then
    ok "Jellyfin relay: scripts/jellyfin-relay.py"
else
    warn "Jellyfin relay script is missing"
fi

if [[ -f "$ROOT/shared/Jellyfin68K.cfg" ]]; then
    ok "client config: shared/Jellyfin68K.cfg"
else
    warn "client config missing (run.sh creates one on first run)"
fi

if [[ -f "$ROOT/.state/BasiliskII.prefs" ]]; then
    ok "emulator preferences: .state/BasiliskII.prefs"
else
    warn "emulator preferences not generated yet (make run does this)"
fi

if [[ -d "$ROOT/shared" ]]; then
    ok "shared folder: shared/"
else
    warn "shared folder is missing"
fi

echo
if ((errors)); then
    echo "Result: $errors error(s), $warnings warning(s)"
    exit 1
fi

if ((warnings)); then
    echo "Result: ready with $warnings warning(s)"
else
    echo "Result: ready"
fi
