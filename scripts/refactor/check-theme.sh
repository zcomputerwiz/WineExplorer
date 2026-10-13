#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Isolated UI acceptance; never uses the live desktop prefix.
set -euo pipefail
root=/srv/protonexplorer/wex-refactor-p3b
test "$(cat "$root/build.status")" = BUILD_PASSED
mkdir -p "$root/checks/theme/include"
exec 9>"$root/checks/theme/check.lock"
flock -n 9
echo THEME_CHECK_RUNNING > "$root/checks/theme/status"
trap 'echo THEME_CHECK_FAILED > "$root/checks/theme/status"' ERR
cp "$root/src/include/wextheme.h" "$root/checks/theme/include/"
x86_64-w64-mingw32-gcc -Wall -Wextra -I "$root/checks/theme/include" \
    "$root/checks/theme-module-probe.c" -mwindows -ladvapi32 -o "$root/checks/theme/probe.exe"
export WINEPREFIX="$root/prefixes/theme" WINESERVER="$root/build/server/wineserver"
export WINELOADER="$root/build/loader/wine" WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml='
export WEX_THEME_ROOT="$root"
timeout 180 xvfb-run -a -s '-screen 0 800x600x24' bash -eu -o pipefail -c '
    root="$WEX_THEME_ROOT"
    trap "\"$WINESERVER\" -k || true" EXIT
    "$WINELOADER" wineboot -u > "$root/checks/theme/boot.log" 2>&1
    "$WINESERVER" -k
    "$WINESERVER" -w
    rm -f /tmp/theme-module-probe.txt
    "$WINELOADER" explorer /desktop=ThemeCheck,800x600 /shell "$root/checks/theme/probe.exe" \
        > "$root/checks/theme/shell.log" 2>&1 &
    for ((attempt=0; attempt<600; ++attempt)); do
        if grep -q "^COMPLETE:" /tmp/theme-module-probe.txt 2>/dev/null; then break; fi
        sleep 0.1
    done
    cp /tmp/theme-module-probe.txt "$root/checks/theme/result.txt"
    grep -q "^COMPLETE: 0 failures" "$root/checks/theme/result.txt"
    import -window root "$root/checks/theme/desktop.png"
' 9>&-
echo THEME_CHECK_PASSED > "$root/checks/theme/status"
