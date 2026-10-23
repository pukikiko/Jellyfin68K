#!/usr/bin/env bash
# Create a project-local Basilisk II configuration.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STATE="${MAC_STATE_DIR:-$ROOT/.state}"
ROM="${MAC_ROM:-$ROOT/images/mac.rom}"
SYSTEM="${MAC_SYSTEM_DISK:-$ROOT/images/system.img}"
SCREEN="${MAC_SCREEN:-512x342}"
DEPTH="${MAC_COLOR_DEPTH:-8}"
RAMSIZE_MB="${MAC_RAM_MB:-64}"
MODEL_ID="${MAC_MODEL_ID:-14}"
CPU="${MAC_CPU:-4}"
FPU="${MAC_FPU:-1}"
SOUND="${MAC_SOUND:-1}"
GUI="${MAC_GUI:-0}"
PREFS="$STATE/BasiliskII.prefs"
APP_DISK="${APP_DISK:-$ROOT/build/Jellyfin68K.dsk}"

[[ -f "$ROM" ]] || {
    echo "error: ROM not found: $ROM" >&2
    echo "Run: scripts/setup-images.sh --rom /path/to/ROM" >&2
    exit 1
}
[[ -f "$SYSTEM" ]] || {
    echo "error: system disk not found: $SYSTEM" >&2
    echo "Run: scripts/setup-images.sh --system /path/to/System.img" >&2
    echo "  or: scripts/setup-images.sh --fetch-system" >&2
    exit 1
}
[[ "$SCREEN" =~ ^[0-9]+x[0-9]+$ ]] || {
    echo "error: MAC_SCREEN must look like 512x342" >&2
    exit 1
}

width="${SCREEN%x*}"
height="${SCREEN#*x}"
[[ "$DEPTH" =~ ^(1|2|4|8|16|24|32)$ ]] || {
    echo "error: MAC_COLOR_DEPTH must be 1, 2, 4, 8, 16, 24, or 32" >&2
    exit 1
}
[[ "$RAMSIZE_MB" =~ ^[0-9]+$ ]] || {
    echo "error: MAC_RAM_MB must be a number" >&2
    exit 1
}
[[ "$FPU" =~ ^(0|1)$ ]] || {
    echo "error: MAC_FPU must be 0 or 1" >&2
    exit 1
}

mkdir -p "$STATE" "$ROOT/shared"

# Keep the supplied system image pristine.  The working copy retains Finder
# state and anything installed while testing (MacTCP, for example).  Remove
# .state/system.img, or run `make reset-state`, to start over.
if [[ ! -f "$STATE/system.img" ]]; then
    cp --reflink=auto -- "$SYSTEM" "$STATE/system.img"
fi

app_line=""
if [[ -f "$APP_DISK" ]]; then
    if [[ ! -f "$STATE/app.dsk" ]] || ! cmp -s "$APP_DISK" "$STATE/app.dsk"; then
        cp --reflink=auto -- "$APP_DISK" "$STATE/app.dsk"
    fi
    app_line="disk $STATE/app.dsk"
fi

cat > "$PREFS" <<EOF
rom $ROM
disk $STATE/system.img
${app_line:+$app_line
}extfs $ROOT/shared
screen win/$width/$height/$DEPTH
displaycolordepth $DEPTH
modelid $MODEL_ID
cpu $CPU
fpu $([[ "$FPU" == 1 ]] && echo true || echo false)
ramsize $((RAMSIZE_MB * 1024 * 1024))
bootdrive 0
bootdriver 0
nosound $([[ "$SOUND" == 1 ]] && echo false || echo true)
nogui $([[ "$GUI" == 1 ]] && echo false || echo true)
idlewait true
jit false
swap_opt_cmd true
keyboardtype 5
mousewheelmode 1
mousewheellines 3
hotkey 0
scale_nearest false
scale_integer false
noclipconversion false
name_encoding 0
delay 0
init_grab false
ignoresegv true
ether slirp
udptunnel false
udpport 6066
xpram $STATE/xpram
EOF

echo "Wrote $PREFS"
if [[ -n "$app_line" ]]; then
    echo "The application disk will appear as a mounted Mac volume."
else
    echo "No application disk yet; run 'make build' first."
fi
