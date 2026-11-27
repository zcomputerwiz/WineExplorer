#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# island-backdrop.sh: Notepad's black tab and status bars without a VM or WinUI. On Xvfb (no compositor, no DwmAlpha),
# scripts/island-backdrop-probe.cpp extends its frame (top 75, bottom 32) with a system backdrop (Mica Alt), fills the
# strips with BLACK_BRUSH as Notepad does, and puts two WinUI-like islands (child windows without a redirection bitmap,
# transparent composition swapchains with an opaque grey square) over them. Checked from the root's screenshot, light
# and dark: the strips show the backdrop's flat colour, not black; the islands' transparent pixels show the strip under
# them; the squares are their grey; the editor is 0xf9f9f9. The wow64 build is W, the probe PROBE.
RC=0
W=${W:-/root/WE-chrome/build/wow64}
PROBE=${PROBE:-/root/island-backdrop-probe.exe}
OUT=${OUT:-/root/island-out}
export DISPLAY=:96 WINEPREFIX=${WINEPREFIX:-/root/island-pfx} WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=${WINEDEBUG:--all}
unset WAYLAND_DISPLAY
mkdir -p "$OUT"
pkill -9 -x Xvfb; pkill -9 -x wineserver; sleep 1
Xvfb :96 -screen 0 1280x800x24 >"$OUT/xvfb.log" 2>&1 &
XPID=$!; sleep 2
cd "$W"
if [ ! -d "$WINEPREFIX/drive_c" ]; then
    WINEDEBUG=-all timeout 150 ./wine wineboot -i >"$OUT/boot.log" 2>&1
    timeout 30 ./server/wineserver -w
fi
for dark in 0 1; do
    (env ISLAND_DARK=$dark ISLAND_SECONDS=25 timeout 40 ./wine "Z:$PROBE" >"$OUT/dark$dark.log" 2>&1 &)
    sleep 14
    import -window root "$OUT/dark$dark.png" 2>/dev/null
    python3 - "$OUT/dark$dark.png" "$dark" <<'PY' || RC=1
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB"); px = im.load(); dark = sys.argv[2] == "1"
# the window is at 40,40 (1016x600); find the client's top left from the editor's colour on the window's middle row
y_mid = 340
x0 = next(x for x in range(40, 200) if px[x, y_mid] == (0xf9, 0xf9, 0xf9))
def at(x, y): return px[x, y]
# screen rows for the window at 40,40 with a 30-pixel caption: the top strip is about 70-145, the menu island 112-145,
# the status bar 604-636
strip_top = at(x0 + 20, 90)                     # inside the top strip, above the menu island
rc = 0
def check(name, ok, value):
    global rc
    print(("PASS" if ok else "FAIL"), name, value)
    if not ok: rc = 1
grey = lambda c: max(c) - min(c) <= 3
check("top strip is the backdrop, not black", strip_top != (0, 0, 0) and grey(strip_top) and
      (strip_top[0] < 0x40 if dark else strip_top[0] > 0xc0), strip_top)
island_bg = at(x0 + 20, 128)                    # the menu island's transparent part, over the strip
check("menu island shows the strip under it", island_bg == strip_top, island_bg)
square = at(548, 128)                           # the menu island's square (its middle)
check("menu island's square is grey 0x404040", square == (0x40, 0x40, 0x40), square)
status_bg = at(x0 + 20, 620)
check("status island shows the strip under it", status_bg == strip_top, status_bg)
check("editor keeps 0xf9f9f9", at(x0 + 20, y_mid) == (0xf9, 0xf9, 0xf9), at(x0 + 20, y_mid))
sys.exit(rc)
PY
    ./server/wineserver -k; sleep 2
done
kill $XPID
exit $RC
