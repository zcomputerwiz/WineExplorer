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
echo "USER=$USER WAYLAND_SESSION=1"
: "${XDG_RUNTIME_DIR:?The login session must provide XDG_RUNTIME_DIR}"
test -d "$XDG_RUNTIME_DIR"

# Safe Mode (docs/SAFE_MODE.md), as in wine-explorer-session.sh: the session keeps its name, and the mode decides
# what starts. wex_prepare_prefix skips the optional logon steps in Safe Mode.
WEXMODE=$("${WEX_BOOTMODE_HELPER:-/usr/local/bin/wex-bootmode}" 2>/dev/null) || WEXMODE=normal
case "$WEXMODE" in
    safe|safenet|safecmd) ;;
    *) WEXMODE=normal ;;
esac
export WEXMODE
echo "WEXMODE=$WEXMODE"

compositor_bin="${WEX_WEXCOMP_BIN:-/usr/local/bin/wexcomp}"
compositor_config="${WEX_WAYLAND_CONFIG:-/etc/xdg/wineexplorer/wayland}"
server_bin="$(dirname "$WINE")/wineserver"
export LABWC_WEX_SHELL_SOCKET=wex-shell-0
export WEX_WAYLAND_NATIVE_WINDOWS=1
unset DISPLAY WAYLAND_DISPLAY

# With Command Prompt the recovery console replaces the shell, as Windows starts its AlternateShell. It is an xterm,
# so it runs on the compositor's XWayland (which sets DISPLAY for it), needs no Wine, and the compositor ends with
# it (-S), which ends the session.
if [ "$WEXMODE" = safecmd ]; then
    echo "Safe Mode with Command Prompt: starting the recovery console"
    exec "$compositor_bin" -C "$compositor_config" -S "${WEX_SAFE_TERMINAL:-/usr/local/bin/wex-safe-terminal.sh}" \
        > "$state/compositor.log" 2>&1
fi

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
"$compositor_bin" -C "$compositor_config" > "$state/compositor.log" 2>&1 &
compositor=$!
for ((i=0;i<100;i++)); do
    test ! -S "$XDG_RUNTIME_DIR/wex-shell-0" || break
    kill -0 "$compositor" || { cat "$state/compositor.log"; exit 1; }
    sleep .1
done
test -S "$XDG_RUNTIME_DIR/wex-shell-0"
export WAYLAND_DISPLAY=wex-shell-0
export WINELOADER="$WINE" WINESERVER="$server_bin"
wex_prepare_prefix
# The shell and all Wine children use the private endpoint. Native desktop-file
# launches obtain the public endpoints through wexhost's compositor inventory.
"$WINE" reg add 'HKCU\Software\Wine\Drivers' /v Graphics /d x11,wayland /f
# The first client performs any required prefix update. Stop its bare desktop
# before starting Explorer as the shell; do not force a reinstall on every login.
"$server_bin" -k
"$server_bin" -w
"$WINE" explorer.exe /shell
