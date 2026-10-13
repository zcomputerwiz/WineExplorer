#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
set -euo pipefail
root=/srv/protonexplorer/wex-refactor-p3b
test "$(cat "$root/build.status")" = BUILD_PASSED
mkdir -p "$root/checks/browser"
echo BROWSER_CHECK_RUNNING > "$root/checks/browser/status"
trap 'echo BROWSER_CHECK_FAILED > "$root/checks/browser/status"' ERR
x86_64-w64-mingw32-gcc -Wall -I "$root/src/include" -I "$root/wine/include" "$root/checks/browser-module-probe.c" \
    -mwindows -o "$root/checks/browser/probe.exe"
export WINEPREFIX="$root/prefixes/browser" WINESERVER="$root/build/server/wineserver"
export WINELOADER="$root/build/loader/wine" WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml='
export WEX_BROWSER_ROOT="$root"
timeout 180 xvfb-run -a -s '-screen 0 800x600x24' bash -eu -o pipefail -c '
    root="$WEX_BROWSER_ROOT"
    trap "\"$WINESERVER\" -k || true" EXIT
    "$WINELOADER" wineboot -u > "$root/checks/browser/boot.log" 2>&1
    "$WINELOADER" reg add "HKCU\\Software\\Wine\\Explorer" /v Desktop /d BrowserCheck /f >/dev/null
    "$WINELOADER" reg add "HKCU\\Software\\Wine\\Explorer\\Desktops" /v BrowserCheck /d 800x600 /f >/dev/null
    "$WINESERVER" -k
    "$WINESERVER" -w
    timeout 60 "$WINELOADER" "$root/checks/browser/probe.exe" standalone
    cp /tmp/browser-module-probe.txt "$root/checks/browser/standalone.txt"
    grep -q "^COMPLETE: 0 failures" "$root/checks/browser/standalone.txt"
    "$WINESERVER" -k
    "$WINESERVER" -w
    for state in present absent; do
        export WINEDLLOVERRIDES="mscoree,mshtml="
        if [ "$state" = absent ]; then export WINEDLLOVERRIDES="wexbrowse=;mscoree,mshtml="; fi
        rm -f /tmp/browser-module-probe.txt
        "$WINELOADER" explorer /desktop=BrowserCheck,800x600 /shell "$root/checks/browser/probe.exe" "$state" \
            > "$root/checks/browser/$state.log" 2>&1 &
        for ((attempt=0; attempt<200; ++attempt)); do
            if grep -q "^COMPLETE:" /tmp/browser-module-probe.txt 2>/dev/null; then break; fi
            sleep 0.1
        done
        cp /tmp/browser-module-probe.txt "$root/checks/browser/$state.txt"
        grep -q "^COMPLETE: 0 failures" "$root/checks/browser/$state.txt"
        "$WINESERVER" -k
        "$WINESERVER" -w
    done
'
echo BROWSER_CHECK_PASSED > "$root/checks/browser/status"
