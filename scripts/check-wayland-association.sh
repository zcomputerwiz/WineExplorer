#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Combined WoW64 build and compositor L2 branch must already be prepared.
set -euo pipefail
root=${WEX_ASSEMBLED_ROOT:-/srv/protonexplorer/wex-wayland-l1}
b=${WEX_CHECK_BUILD:-$root/wow64-source/build/wine64}
source_root=${WEX_CHECK_SOURCE:-$root/wow64-source}
scripts=$(dirname "$(readlink -f "$0")")
export WINEPREFIX=${WEX_CHECK_PREFIX:?Set an isolated Wayland test prefix}
test "$WINEPREFIX" != "$HOME/.wine"
export WINELOADER="$b/loader/wine" WINESERVER="$b/server/wineserver"
export WINEDEBUG=-all WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d'
export WEX_ASSEMBLED_ROOT="$root"
exec 9>"$root/check.lock"
flock -n 9
x86_64-w64-mingw32-gcc "$scripts/wayland-association-probe.c" -idirafter "$source_root/wine/include" -luser32 -o "$root/checks/association64.exe"
i686-w64-mingw32-gcc "$scripts/wayland-association-probe.c" -idirafter "$source_root/wine/include" -luser32 -o "$root/checks/association32.exe"
x86_64-w64-mingw32-gcc "$scripts/wayland-association-probe.c" -DWEX_TEST_HOSTING -idirafter "$source_root/wine/include" -luser32 -lgdi32 -o "$root/checks/hosting64.exe"
i686-w64-mingw32-gcc "$scripts/wayland-association-probe.c" -DWEX_TEST_HOSTING -idirafter "$source_root/wine/include" -luser32 -lgdi32 -o "$root/checks/hosting32.exe"
x86_64-w64-mingw32-gcc "$scripts/wex-wayland-binding-probe.c" -I"$source_root/src/include" -idirafter "$source_root/wine/include" -L"$b/dlls/wexhost/x86_64-windows" -lwexhost -luser32 -lgdi32 -o "$root/checks/binding64.exe"
i686-w64-mingw32-gcc "$scripts/wex-wayland-binding-probe.c" -I"$source_root/src/include" -idirafter "$source_root/wine/include" -L"$b/dlls/wexhost/i386-windows" -lwexhost -luser32 -lgdi32 -o "$root/checks/binding32.exe"
x86_64-w64-mingw32-gcc "$scripts/wayland-stacking-probe.c" -lgdi32 -o "$root/checks/association-stack64.exe"
i686-w64-mingw32-gcc "$scripts/wayland-stacking-probe.c" -lgdi32 -o "$root/checks/association-stack32.exe"
timeout 100 xvfb-run -a bash -eu -c '
    trap '\''"$WINESERVER" -k || true'\'' EXIT
    unset WAYLAND_DISPLAY
    "$WINELOADER" reg add "HKCU\Software\Wine\Drivers" /v Graphics /d wayland /f
    "$WINESERVER" -k
    "$WINESERVER" -w
' 9>&-
WEX_ASSOCIATION_STACK=${WEX_ASSOCIATION_STACK:-1} dbus-run-session -- python3 "$scripts/check-wayland-association.py" 9>&-
