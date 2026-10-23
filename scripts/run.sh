#!/usr/bin/env bash
# Boot the test environment in Basilisk II.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
STATE="${MAC_STATE_DIR:-$ROOT/.state}"
PREFS="$STATE/BasiliskII.prefs"

command -v BasiliskII >/dev/null 2>&1 || {
    cat >&2 <<'EOF'
error: BasiliskII is not installed.

On Arch Linux:
  yay -S basiliskii-git

Other distributions: install Basilisk II (sometimes called basilisk2) from
your package manager or build it from https://github.com/kanjitalk755/macemu.
EOF
    exit 1
}

"$ROOT/scripts/configure-emulator.sh"

mkdir -p "$ROOT/shared"
if [[ ! -f "$ROOT/shared/Jellyfin68K.cfg" && -f "$ROOT/examples/Jellyfin68K.cfg" ]]; then
    cp "$ROOT/examples/Jellyfin68K.cfg" "$ROOT/shared/Jellyfin68K.cfg"
    echo "Created shared/Jellyfin68K.cfg (emulator defaults; host=10.0.2.2)."
fi

cat <<'EOF'
Starting Basilisk II...

  - The System 7 disk is the startup disk.
  - The Jellyfin68K disk contains the client.
  - MacTCP must be open once after boot to install its .IPP driver:
    open Control Panels -> MacTCP, then close its window.  The client's
    status line reports the missing driver if it is launched too early.
  - Emulator networking uses a relay: in another terminal run
    `make relay` (10.0.2.2:8096 -> 10.0.1.1:8096), and keep
    `host=10.0.2.2` in shared/Jellyfin68K.cfg.
  - The Unix volume maps to shared/ in this project.
  - After changing the app, quit Basilisk II and run `make run` again.

EOF

exec BasiliskII --config "$PREFS" "$@"
