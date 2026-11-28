#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# x11-material-parity.sh: the on-screen colour of each system material under the X11 composer, against the shared
# default tint table (wex_material_default_tint, src/include/wexhost.h; docs/WAYLAND_DEFAULT.md, "Mica"). Xvfb with
# composition on (Shell: Composition, DwmService, DwmAlpha, DwmMaterials, DwmFrameEffects = 1), the desktop colour set to one flat colour (so blur has nothing to change), explorer /shell (the X11 window manager
# and composer), scripts/dwm-material-parity-probe.c with six windows; the screen is read with import (not xwd: see
# docs/TOOLING.md). Expected pixel = background * (1 - alpha) + tint * alpha, per the table:
#   Mica light 0x80f3f3f3, Mica dark 0xcc202020, Acrylic light/dark 0xb3f3f3f3 / 0xb3202020, Glass only: the background.
# Acrylic with an app tint of alpha 0 is the background (the blur alone); a 50 % red gradient is red at 50 %. Mica Alt has no table row: it is printed, not checked. TOL (default 6) per channel. W, PROBE, OUT name the build, the
# probe and the output directory; BG is the desktop colour (default 3a6ea5).
RC=0
HERE=$(cd "$(dirname "$0")" && pwd)
W=${W:-/root/WE-chrome/build/wow64}
PROBE=${PROBE:-/root/dwm-material-parity-probe.exe}
OUT=${OUT:-/root/mp-out}
BG=${BG:-3a6ea5}
TOL=${TOL:-6}
export DISPLAY=:97 WINEPREFIX=${WINEPREFIX:-/root/mp-pfx} WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=${WINEDEBUG:--all}
unset WAYLAND_DISPLAY
mkdir -p "$OUT"
pkill -9 -x Xvfb; pkill -9 -x wineserver; pkill -9 -x services.exe; sleep 1
Xvfb :97 -screen 0 1280x800x24 +extension Composite +extension DAMAGE +extension RENDER >"$OUT/xvfb.log" 2>&1 &
XPID=$!; sleep 2
cd "$W"
if [ ! -d "$WINEPREFIX/drive_c" ]; then
    timeout 150 ./wine wineboot -i >"$OUT/boot.log" 2>&1
    timeout 30 ./server/wineserver -w
fi
R=$((16#${BG:0:2})); G=$((16#${BG:2:2})); B=$((16#${BG:4:2}))
./wine reg add 'HKCU\Control Panel\Colors' /v Background /t REG_SZ /d "$R $G $B" /f >/dev/null 2>&1
./wine reg add 'HKCU\Control Panel\Desktop' /v Wallpaper /t REG_SZ /d "" /f >/dev/null 2>&1
timeout 30 ./server/wineserver -w
for v in Composition DwmService DwmAlpha DwmMaterials DwmFrameEffects; do
    ./wine reg add 'HKCU\Software\WineExplorer\Shell' /v $v /t REG_DWORD /d 1 /f >/dev/null 2>&1
done
./wine reg add 'HKCU\Software\Wine\Drivers' /v Graphics /t REG_SZ /d x11 /f >/dev/null 2>&1
./wine reg add 'HKCU\Software\Wine\Explorer\Desktops\Default' /v EnableShell /t REG_DWORD /d 1 /f >/dev/null 2>&1
timeout 30 ./server/wineserver -w
# the X11 composer needs a Wine virtual desktop to compose in
(timeout 120 ./wine explorer.exe /desktop=Default,1280x800 /shell >"$OUT/shell.log" 2>&1 &)
sleep 15
(timeout 90 ./wine "Z:$PROBE" 60 >"$OUT/probe.log" 2>&1 &)
sleep 12
import -window root "$OUT/screen.png" 2>/dev/null
python3 "$HERE/material-parity-check.py" "$OUT/screen.png" "$BG" "$TOL" || RC=1
./server/wineserver -k; kill $XPID
exit $RC
