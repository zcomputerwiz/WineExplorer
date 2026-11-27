#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# b136-redirect.sh: B136 without a VM. On Xvfb (no compositor), scripts/b136-redirect-probe.cpp in each mode (plain,
# d3d, dcomp) shows its red GDI window; the root's screenshot must show it red, not black, in every mode. The wow64 build
# is W, the probe PROBE (built from the .cpp, see its header). MODES picks the modes, default "plain d3d dcomp d3dchild dcompchild".
RC=0
W=${W:-/root/WE-chrome/build/wow64}
PROBE=${PROBE:-/root/b136-redirect-probe.exe}
OUT=${OUT:-/root/b136-out}
export DISPLAY=:95 WINEPREFIX=${WINEPREFIX:-/root/b136-pfx} WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=${WINEDEBUG:--all}
unset WAYLAND_DISPLAY
mkdir -p "$OUT"
pkill -9 -x Xvfb; pkill -9 -x wineserver; sleep 1
Xvfb :95 -screen 0 1280x800x24 >"$OUT/xvfb.log" 2>&1 &
XPID=$!; sleep 2
cd "$W"
if [ ! -d "$WINEPREFIX/drive_c" ]; then
    WINEDEBUG=-all timeout 150 ./wine wineboot -i >"$OUT/boot.log" 2>&1
    timeout 30 ./server/wineserver -w
fi
red() {   # red NAME: the share of pure red pixels in the GDI window's client area on the root's screenshot, in percent
    import -window root "$OUT/$1.png" 2>/dev/null
    python3 - "$OUT/$1.png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB"); px = im.load()
box = (620, 140, 880, 280)   # inside the client area of the 300x200 window at 600,100, below any caption
n = red = 0
for y in range(box[1], box[3]):
    for x in range(box[0], box[2]):
        n += 1
        r, g, b = px[x, y]
        if r > 240 and g < 20 and b < 20: red += 1
print(red * 100 // n)
PY
}
for mode in ${MODES:-plain d3d dcomp d3dchild dcompchild}; do
    (B136_MODE=$mode B136_SECONDS=25 timeout 40 ./wine "Z:$PROBE" >"$OUT/$mode.log" 2>&1 &)
    sleep 12
    share=$(red "$mode")
    if [ "${share:-0}" -ge 90 ]; then echo "PASS $mode: the GDI window is red ($share% of its client area)"
    else RC=1; echo "FAIL $mode: the GDI window is not red on the root ($share% red)"; fi
    ./server/wineserver -k; sleep 2
done
kill $XPID
exit $RC
