#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
set -euo pipefail
root=/srv/protonexplorer/wex-refactor-p3b
test "$(cat "$root/build.status")" = BUILD_PASSED
mkdir -p "$root/checks/search"
echo SEARCH_CHECK_RUNNING > "$root/checks/search/status"
trap 'echo SEARCH_CHECK_FAILED > "$root/checks/search/status"' ERR
x86_64-w64-mingw32-gcc -Wall -I "$root/src/include" "$root/checks/search-module-probe.c" \
    -mwindows -lole32 -lcomctl32 -o "$root/checks/search/probe.exe"
export WINEPREFIX="$root/prefixes/search" WINESERVER="$root/build/server/wineserver"
export WINELOADER="$root/build/loader/wine" WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml='
export WEX_SEARCH_ROOT="$root"
timeout 160 xvfb-run -a -s '-screen 0 800x600x24' bash -eu -o pipefail -c '
    root="$WEX_SEARCH_ROOT"
    trap "\"$WINESERVER\" -k || true" EXIT
    "$WINELOADER" wineboot -u > "$root/checks/search/boot.log" 2>&1
    "$WINESERVER" -k
    "$WINESERVER" -w
    timeout 40 "$WINELOADER" "$root/checks/search/probe.exe" standalone
    cp /tmp/search-module-probe.txt "$root/checks/search/standalone.txt"
    grep -q "^COMPLETE: 0 failures" "$root/checks/search/standalone.txt"
    "$WINESERVER" -k
    "$WINESERVER" -w
    for state in present absent; do
        export WINEDLLOVERRIDES="mscoree,mshtml="
        if [ "$state" = absent ]; then export WINEDLLOVERRIDES="wexsearch=;mscoree,mshtml="; fi
        rm -f /tmp/search-module-probe.txt
        "$WINELOADER" explorer /desktop=SearchCheck,800x600 /shell "$root/checks/search/probe.exe" "$state" \
            > "$root/checks/search/$state.log" 2>&1 &
        for ((attempt=0; attempt<200; ++attempt)); do
            if grep -q "^COMPLETE:" /tmp/search-module-probe.txt 2>/dev/null; then break; fi
            sleep 0.1
        done
        cp /tmp/search-module-probe.txt "$root/checks/search/$state.txt"
        grep -q "^COMPLETE: 0 failures" "$root/checks/search/$state.txt"
        "$WINESERVER" -k
        "$WINESERVER" -w
    done
'
echo SEARCH_CHECK_PASSED > "$root/checks/search/status"
