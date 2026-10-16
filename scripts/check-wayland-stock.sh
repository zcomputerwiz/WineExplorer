#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
set -euo pipefail
root=${WEX_ASSEMBLED_ROOT:-/srv/protonexplorer/wex-wayland-l1}
source_root=${WEX_CHECK_SOURCE:-$root/wow64-source}
b=${WEX_CHECK_BUILD:-$source_root/build/wine64}
scripts=$(dirname "$(readlink -f "$0")")
export WINEPREFIX=${WEX_CHECK_PREFIX:?Set an isolated Wayland prefix}
test "$WINEPREFIX" != "$HOME/.wine"
exec 9>"$root/check.lock"
flock -n 9
state=$(mktemp -d "$root/checks/stock-wayland.XXXXXX")
mkdir -m 700 "$state/runtime" "$state/config"
printf '<labwc_config><core><decoration>server</decoration></core></labwc_config>\n' > "$state/config/rc.xml"
export XDG_RUNTIME_DIR="$state/runtime" WAYLAND_DISPLAY=wayland-0
export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1
export WINELOADER="$b/loader/wine" WINESERVER="$b/server/wineserver"
export WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d'
unset DISPLAY
/usr/bin/labwc -C "$state/config" > "$state/compositor.log" 2>&1 &
compositor=$!
cleanup() {
    "$WINESERVER" -k || true
    "$WINESERVER" -w || true
    kill "$compositor" 2>/dev/null || true
    wait "$compositor" 2>/dev/null || true
}
trap cleanup EXIT
for ((i=0;i<100;++i)); do
    if [ -S "$state/runtime/wayland-0" ]; then break; fi
    kill -0 "$compositor"
    sleep .1
done
test ! -e "$state/runtime/wex-shell-0"
for arch in x86_64 i686; do
    if [ "$arch" = x86_64 ]; then bits=64; else bits=32; fi
    "$arch-w64-mingw32-gcc" "$scripts/wayland-stock-probe.c" -idirafter "$source_root/wine/include" -luser32 -lgdi32 -o "$state/stock$bits.exe"
    timeout 30 "$WINELOADER" "$state/stock$bits.exe" > "$state/probe$bits.log" 2>&1
    grep "STOCK_WAYLAND_PASSED bits=$bits failures=0" "$state/probe$bits.log"
    "$WINESERVER" -k
    "$WINESERVER" -w
done
printf 'Stock Wayland logs: %s\n' "$state"
