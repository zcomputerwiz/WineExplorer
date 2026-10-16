#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# LightDM's optional Wayland session. PAM/logind owns the runtime directory/VT.
set -e
. "$(dirname "$0")/wine-explorer-session-common.sh"
state="${XDG_STATE_HOME:-$HOME/.local/state}/wineexplorer-wayland"
mkdir -p "$state"
[ ! -e "$state/session.log" ] || mv -f "$state/session.log" "$state/session.log.1"
exec > "$state/session.log" 2>&1
echo "=== WineExplorer Wayland session: $(date) ==="
: "${XDG_RUNTIME_DIR:?The login session must provide XDG_RUNTIME_DIR}"
test -d "$XDG_RUNTIME_DIR"

compositor_bin="${WEX_WEXCOMP_BIN:-/usr/local/bin/wexcomp}"
server_bin="$(dirname "$WINE")/wineserver"
export LABWC_WEX_SHELL_SOCKET=wex-shell-0
export WEX_WAYLAND_NATIVE_WINDOWS=1
unset DISPLAY WAYLAND_DISPLAY
compositor=
cleanup()
{
    "$server_bin" -k 2>/dev/null || true
    if [ -n "$compositor" ]; then
        kill "$compositor" 2>/dev/null || true
        wait "$compositor" 2>/dev/null || true
    fi
}
trap cleanup EXIT
"$compositor_bin" -C "${WEX_WAYLAND_CONFIG:-/etc/xdg/wineexplorer/wayland}" > "$state/compositor.log" 2>&1 &
compositor=$!
for ((i=0;i<100;i++)); do
    test ! -S "$XDG_RUNTIME_DIR/wex-shell-0" || break
    kill -0 "$compositor" || { cat "$state/compositor.log"; exit 1; }
    sleep .1
done
test -S "$XDG_RUNTIME_DIR/wex-shell-0"
export WAYLAND_DISPLAY=wex-shell-0
export WINELOADER="$WINE" WINESERVER="$server_bin"
# The shell and all Wine children use the private endpoint. Native desktop-file
# launches obtain the public endpoints through wexhost's compositor inventory.
"$WINE" reg add 'HKCU\Software\Wine\Drivers' /v Graphics /d x11,wayland /f
# The first client performs any required prefix update. Stop its bare desktop
# before starting Explorer as the shell; do not force a reinstall on every login.
"$server_bin" -k
"$server_bin" -w
"$WINE" explorer.exe /shell
