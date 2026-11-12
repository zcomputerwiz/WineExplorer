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
# A Safe Mode logon leaves the account's session choice (X11 or Wayland) as it was (wex-session-choice)
if [ "$WEXMODE" != normal ]; then
    "${WEX_SESSION_CHOICE:-/usr/local/bin/wex-session-choice}" --restore || true
fi
# Safe Mode runs at 640x480, as XP's basic display driver does (docs/SAFE_MODE_WORKORDER.md C3), and so does Enable
# VGA Mode, a normal start (D2). wexcomp starts each output at WEX_OUTPUT_MODE when the output offers it; nothing is
# saved, so the next normal start has its usual mode.
WEXOPTION=$("${WEX_BOOTMODE_HELPER:-/usr/local/bin/wex-bootmode}" --option 2>/dev/null) || WEXOPTION=
if [ "$WEXMODE" != normal ] || [ "$WEXOPTION" = vga ]; then
    export WEX_OUTPUT_MODE=640x480
    echo "Mode $WEXMODE${WEXOPTION:+, option $WEXOPTION}: 640x480"
else
    # A normal start restores the mode accepted in Display Properties (wexhost writes the profile; the X11 session
    # reads the same file), at that size nearest 60 Hz
    profile="${XDG_CONFIG_HOME:-$HOME/.config}/wineexplorer/display"
    if [ -r "$profile" ] && read -r saved_width saved_height _ < "$profile" \
            && [[ "$saved_width $saved_height" =~ ^[0-9]+\ [0-9]+$ ]]; then
        export WEX_OUTPUT_MODE="${saved_width}x${saved_height}"
        echo "Saved display mode: $WEX_OUTPUT_MODE"
    fi
fi

compositor_bin="${WEX_WEXCOMP_BIN:-/usr/local/bin/wexcomp}"
compositor_config="${WEX_WAYLAND_CONFIG:-/etc/xdg/wineexplorer/wayland}"
server_bin="$(dirname "$WINE")/wineserver"
# wexcomp opens its privileged socket under WEX_SHELL_SOCKET (src/server.c); Explorer connects there. (Until
# 2026-11-11 this exported LABWC_WEX_SHELL_SOCKET, which nothing reads; both names meant the default, wex-shell-0.)
shell_socket=wex-shell-0
export WEX_SHELL_SOCKET=$shell_socket
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
# X11 is the fallback: when the compositor cannot start, the account's next logon starts the X11 session (outside Safe
# Mode, wex-session-choice --fallback), and this one ends back at the logon screen.
compositor_failed() {
    echo "wexcomp did not start"
    cat "$state/compositor.log"
    "${WEX_SESSION_CHOICE:-/usr/local/bin/wex-session-choice}" --fallback wine-explorer || true
    exit 1
}
"$compositor_bin" -C "$compositor_config" > "$state/compositor.log" 2>&1 &
compositor=$!
for ((i=0;i<100;i++)); do
    test ! -S "$XDG_RUNTIME_DIR/$shell_socket" || break
    kill -0 "$compositor" 2>/dev/null || compositor_failed
    sleep .1
done
test -S "$XDG_RUNTIME_DIR/$shell_socket" || compositor_failed
export WAYLAND_DISPLAY=$shell_socket
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
