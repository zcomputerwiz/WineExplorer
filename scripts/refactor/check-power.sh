#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Isolated, cancel-only checks: standalone DLL, Start menu, and greeter.
set -euo pipefail
root=/srv/protonexplorer/wex-refactor-p3b
test "$(cat "$root/build.status")" = BUILD_PASSED
mkdir -p "$root/checks/power/include"
exec 9>"$root/checks/power/check.lock"
flock -n 9
echo POWER_CHECK_RUNNING > "$root/checks/power/status"
trap 'echo POWER_CHECK_FAILED > "$root/checks/power/status"' ERR
cp "$root/src/include/wexpower.h" "$root/checks/power/include/"
cp "$root/wine/include/wexshell.h" "$root/checks/power/include/"
x86_64-w64-mingw32-gcc -Wall -I "$root/checks/power/include" \
    "$root/checks/power-module-probe.c" -mwindows -o "$root/checks/power/probe.exe"
x86_64-w64-mingw32-gcc -Wall -I "$root/checks/power/include" \
    "$root/checks/shell-plugin-probe.c" -mwindows -o "$root/checks/power/menu.exe"
export WINEPREFIX="$root/prefixes/power" WINESERVER="$root/build/server/wineserver"
export WINELOADER="$root/build/loader/wine" WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml='
export WEX_POWER_ROOT="$root"
timeout 240 xvfb-run -a -s '-screen 0 800x600x24' bash -eu -o pipefail -c '
    root="$WEX_POWER_ROOT"
    trap "\"$WINESERVER\" -k || true" EXIT
    "$WINELOADER" wineboot -u > "$root/checks/power/boot.log" 2>&1
    "$WINESERVER" -k
    "$WINESERVER" -w
    "$WINELOADER" "$root/checks/power/probe.exe" standalone
    cp /tmp/power-module-probe.txt "$root/checks/power/standalone.txt"
    grep -q "^COMPLETE: 0 failures" "$root/checks/power/standalone.txt"
    "$WINESERVER" -k
    "$WINESERVER" -w
    python3 "$root/checks/greeter-fixture.py"
    cp /tmp/power-module-probe.txt "$root/checks/power/greeter.txt"
    grep -q "^COMPLETE: 0 failures" "$root/checks/power/greeter.txt"
    "$WINESERVER" -k
    "$WINESERVER" -w
    "$WINELOADER" reg add "HKCU\Software\Wine\Explorer" /v Desktop /d PowerCheck /f >/dev/null
    "$WINELOADER" reg add "HKCU\Software\Wine\Explorer\Desktops" /v PowerCheck /d 800x600 /f >/dev/null
    "$WINELOADER" reg add "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Advanced" /v Start_Style /t REG_DWORD /d 0 /f >/dev/null
    "$WINESERVER" -k
    "$WINESERVER" -w
    "$WINELOADER" explorer /desktop=PowerCheck,800x600 /shell > "$root/checks/power/shell.log" 2>&1 &
    sleep 5
    "$WINELOADER" "$root/checks/power/probe.exe" consumer &
    controller=$!
    sleep 1
    "$WINELOADER" "$root/checks/power/menu.exe" menu
    xdotool mousemove 80 550 click 1
    wait "$controller"
    cp /tmp/power-module-probe.txt "$root/checks/power/start-menu.txt"
    grep -q "^COMPLETE: 0 failures" "$root/checks/power/start-menu.txt"
'
echo POWER_CHECK_PASSED > "$root/checks/power/status"
