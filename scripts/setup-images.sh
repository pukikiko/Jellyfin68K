#!/usr/bin/env bash
# Install a 68K Mac ROM and a bootable System 7 disk image into images/.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMAGES="$ROOT/images"
STATE="$ROOT/.state"
ROM_DEST="$IMAGES/mac.rom"
SYSTEM_DEST="$IMAGES/system.img"
SYSTEM_URL="${SYSTEM_URL:-https://archive.org/download/AppleMacintoshSystem753/System7_5_3.img}"

usage() {
    cat <<EOF
Usage: scripts/setup-images.sh [options]

Options:
  --rom PATH        Copy a 68K Mac ROM to images/mac.rom
  --system PATH     Copy a bootable HFS System 7 disk to images/system.img
  --fetch-system    Download a System 7.5.3 disk image from the Internet Archive
  --force           Replace existing images
  -h, --help        Show this help

The ROM and Apple system software are not part of this project.  Only use
files you are entitled to use, and do not commit them to source control.
EOF
}

need_force=false
rom=""
system=""
fetch_system=false

while (($#)); do
    case "$1" in
        --rom)
            [[ $# -ge 2 ]] || { echo "error: --rom needs a path" >&2; exit 2; }
            rom="$2"
            shift 2
            ;;
        --system)
            [[ $# -ge 2 ]] || { echo "error: --system needs a path" >&2; exit 2; }
            system="$2"
            shift 2
            ;;
        --fetch-system)
            fetch_system=true
            shift
            ;;
        --force)
            need_force=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

mkdir -p "$IMAGES"

copy_image() {
    local src=$1
    local dest=$2
    local label=$3

    [[ -f "$src" ]] || { echo "error: $label not found: $src" >&2; exit 1; }
    [[ -s "$src" ]] || { echo "error: $label is empty: $src" >&2; exit 1; }

    if [[ -e "$dest" && "$need_force" != true ]]; then
        echo "error: $dest already exists (use --force to replace it)" >&2
        exit 1
    fi

    rm -f "$dest"
    cp --reflink=auto -- "$src" "$dest"
    echo "Installed $label -> $dest"
}

if [[ -n "$rom" ]]; then
    copy_image "$rom" "$ROM_DEST" "ROM"
fi

if [[ -n "$system" ]]; then
    copy_image "$system" "$SYSTEM_DEST" "system disk"
fi

if $fetch_system; then
    if [[ -e "$SYSTEM_DEST" && "$need_force" != true ]]; then
        echo "error: $SYSTEM_DEST already exists (use --force to replace it)" >&2
        exit 1
    fi

    tmp="$SYSTEM_DEST.part"
    rm -f "$tmp"
    echo "Downloading System 7.5.3 test image..."
    echo "  $SYSTEM_URL"
    curl -L --fail --retry 3 --progress-bar -o "$tmp" "$SYSTEM_URL"
    if command -v file >/dev/null 2>&1 && ! file "$tmp" | grep -qi 'HFS'; then
        rm -f "$tmp"
        echo "error: downloaded file does not look like an HFS disk image" >&2
        exit 1
    fi
    mv "$tmp" "$SYSTEM_DEST"
    echo "Installed system disk -> $SYSTEM_DEST"
fi

if [[ -f "$ROM_DEST" && -f "$SYSTEM_DEST" ]]; then
    rm -rf "$STATE"
fi

echo
if [[ -f "$ROM_DEST" ]]; then
    printf 'ROM:    %s (%s)\n' "$ROM_DEST" "$(du -h "$ROM_DEST" | cut -f1)"
else
    echo "ROM:    missing (use --rom PATH)"
fi

if [[ -f "$SYSTEM_DEST" ]]; then
    printf 'System: %s (%s)\n' "$SYSTEM_DEST" "$(du -h "$SYSTEM_DEST" | cut -f1)"
else
    echo "System: missing (use --system PATH or --fetch-system)"
fi
