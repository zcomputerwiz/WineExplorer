#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Run after build-assembled.sh; no installed runtime or user prefix is touched.
set -euo pipefail
root=/srv/protonexplorer/wex-refactor-p2
test "$(cat "$root/build.status")" = BUILD_PASSED
mkdir -p "$root/checks" "$root/prefixes"
exec 9>"$root/check.lock"
flock -n 9
trap 'printf "%s\n" CHECK_FAILED > "$root/checks/status"' ERR
printf '%s\n' CHECK_RUNNING > "$root/checks/status"
x86_64-w64-mingw32-gcc -Wall -Wextra "$root/checks/tray-layout-probe.c" -mwindows -o "$root/checks/tray-layout-probe.exe"
cc -Wall -Wextra "$root/checks/linux-dialog-probe.c" -lX11 -o "$root/checks/linux-dialog-probe"
export WINEPREFIX="$root/prefixes/smoke"
export WINESERVER="$root/build/server/wineserver"
export WINELOADER="$root/build/loader/wine"
export WINEDEBUG=-all
export WINEDLLOVERRIDES='mscoree,mshtml='
export WINETEST_INTERACTIVE=0 WINETEST_DEBUG=1
export WEX_P2_ROOT="$root"
timeout 180 xvfb-run -a -s '-screen 0 800x600x24' bash -eu -o pipefail -c '
    root="$WEX_P2_ROOT"
    cleanup() { "$WINESERVER" -k || true; }
    trap cleanup EXIT
    timeout 90 "$WINELOADER" wineboot -u > "$root/checks/wineboot.log" 2>&1
    "$WINESERVER" -k
    "$WINESERVER" -w
    "$WINELOADER" explorer /desktop=RefactorP2,800x600 /shell > "$root/checks/shell.log" 2>&1 &
    sleep 8
    "$WINELOADER" "$root/checks/tray-layout-probe.exe" > "$root/checks/tray-run.log" 2>&1
    cp /tmp/tray-layout-probe.txt "$root/checks/tray-layout.txt"
    import -window root "$root/checks/desktop.png"
    "$root/checks/linux-dialog-probe" > "$root/checks/linux-dialog.log" 2>&1
' 9>&-
printf '%s\n' CHECK_PASSED > "$root/checks/status"
