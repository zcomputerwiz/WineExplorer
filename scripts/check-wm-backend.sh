#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Exercise both PE ABIs without touching the installed desktop/session.
set -euo pipefail
root=${WEX_ASSEMBLED_ROOT:-/srv/protonexplorer/wex-wayland-l1}
b=${WEX_CHECK_BUILD:-$root/wow64-source/build/wine64}
source_root=${WEX_CHECK_SOURCE:-$root/wow64-source}
export WINEPREFIX=${WEX_CHECK_PREFIX:?Set WEX_CHECK_PREFIX to an isolated test prefix}
export WINELOADER="$b/loader/wine" WINESERVER="$b/server/wineserver"
export WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml='
export WEX_BACKEND_ROOT="$root"
probe=$(dirname "$(readlink -f "$0")")/wex-wm-backend-probe.c
mkdir -p "$root/checks"
make -C "$b" -j8 dlls/wexhost/all dlls/wexhost/x86_64-windows/libwexhost.a dlls/wexhost/i386-windows/libwexhost.a
x86_64-w64-mingw32-gcc "$probe" -I"$source_root/src/include" -L"$b/dlls/wexhost/x86_64-windows" -lwexhost -luser32 -o "$root/checks/wm-backend64.exe"
i686-w64-mingw32-gcc "$probe" -I"$source_root/src/include" -L"$b/dlls/wexhost/i386-windows" -lwexhost -luser32 -o "$root/checks/wm-backend32.exe"
timeout 90 xvfb-run -a -s '-screen 0 800x600x24' bash -eu -c '
    trap '\''"$WINESERVER" -k || true'\'' EXIT
    for bits in 64 32; do
        report="$WEX_BACKEND_ROOT/checks/l2-backend$bits.log"
        : > "$report"
        "$WINELOADER" explorer /desktop=BackendProbe,800x600 "$WEX_BACKEND_ROOT/checks/wm-backend$bits.exe" "Z:$report" > "$WEX_BACKEND_ROOT/checks/l2-backend-launch$bits.log" 2>&1 &
        for ((attempt=0; attempt<200; ++attempt)); do
            if grep -q "BACKEND_ABI_" "$report"; then break; fi
            sleep 0.1
        done
        grep "BACKEND_ABI_PASSED bits=$bits failures=0" "$report"
        "$WINESERVER" -k
        "$WINESERVER" -w
    done
'
