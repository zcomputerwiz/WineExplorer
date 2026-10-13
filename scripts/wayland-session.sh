#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Opt-in lab session only. Never installed as the ISO default.
set -euo pipefail
compositor_bin=${WEX_WEXCOMP_BIN:-$HOME/wexcomp-build/build/labwc}
wine_bin=${WEX_WINE_BIN:-wine}
server_bin=${WEX_WINESERVER_BIN:-wineserver}
wine_bin=$(command -v "$wine_bin")
server_bin=$(command -v "$server_bin")
export WINELOADER="$wine_bin" WINESERVER="$server_bin"
export WINEPREFIX=${WEX_WAYLAND_PREFIX:-$HOME/.wine-wayland-l1}
test "$WINEPREFIX" != "$HOME/.wine" || { echo 'Use an isolated Wayland test prefix.' >&2; exit 2; }
test -x "$compositor_bin"
state=${WEX_WAYLAND_STATE:-$HOME/.local/state/wineexplorer-wayland}
mkdir -p "$state"
session=$(mktemp -d "$state/session.XXXXXX")
mkdir -m 700 "$session/runtime" "$session/config"
export XDG_RUNTIME_DIR="$session/runtime"
export WAYLAND_DISPLAY=wex-shell-0 LABWC_WEX_SHELL_SOCKET=wex-shell-0
export WINEDLLOVERRIDES=${WINEDLLOVERRIDES:-'mscoree,mshtml=;winemenubuilder.exe=d'}
# Empty binding lists cause labwc to load defaults. One inert binding in each
# list suppresses that fallback; there is deliberately no <default/> entry.
cat > "$session/config/rc.xml" <<'EOF'
<?xml version="1.0"?>
<labwc_config>
  <core><decoration>none</decoration></core>
  <keyboard><keybind key="A-Tab"><action name="None"/></keybind></keyboard>
  <mouse><context name="Root"><mousebind button="Right" action="Press"><action name="None"/></mousebind></context></mouse>
</labwc_config>
EOF
printf '<openbox_menu/>\n' > "$session/config/menu.xml"
compositor=
cleanup() {
    "$server_bin" -k 2>/dev/null || true
    if [ -n "$compositor" ]; then kill "$compositor" 2>/dev/null || true; wait "$compositor" 2>/dev/null || true; fi
}
trap cleanup EXIT
"$compositor_bin" -C "$session/config" > "$session/compositor.log" 2>&1 & compositor=$!
for ((i=0;i<100;i++)); do
    test -S "$XDG_RUNTIME_DIR/wex-shell-0" && break
    kill -0 "$compositor" || { cat "$session/compositor.log" >&2; exit 1; }
    sleep .1
done
test -S "$XDG_RUNTIME_DIR/wex-shell-0"
# The same privileged connection is inherited by every launched Wine process.
unset DISPLAY
"$wine_bin" reg add 'HKCU\Software\Wine\Drivers' /v Graphics /d wayland /f > "$session/prefix.log" 2>&1
"$wine_bin" wineboot -u >> "$session/prefix.log" 2>&1
"$server_bin" -k
"$server_bin" -w
echo "Wayland test logs: $session"
"$wine_bin" explorer /shell "$@" > "$session/shell.log" 2>&1
