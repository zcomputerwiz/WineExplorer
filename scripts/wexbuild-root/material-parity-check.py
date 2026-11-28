#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# material-parity-check.py SCREENSHOT BG TOL: the pixel check of x11-material-parity.sh and wayland-material-parity.sh.
# The screenshot holds the eight windows of scripts/dwm-material-parity-probe.c over a flat desktop colour BG (rrggbb).
# Expected pixel = background * (1 - alpha) + tint * alpha, from wex_material_default_tint (src/include/wexhost.h) and
# the app tints the probe sets; TOL per channel.
import sys
from PIL import Image

im = Image.open(sys.argv[1]).convert("RGB")
px = im.load()
bg = tuple(int(sys.argv[2][i:i + 2], 16) for i in (0, 2, 4))
tol = int(sys.argv[3])
rows = [  # name, window x, y, tint 0xAARRGGBB or None (the background), checked
    ("Mica light", 40, 40, 0x80f3f3f3, True), ("Mica dark", 290, 40, 0xcc202020, True),
    ("Glass only", 540, 40, None, True), ("Acrylic light", 40, 240, 0xb3f3f3f3, True),
    ("Acrylic dark", 290, 240, 0xb3202020, True), ("Mica Alt light", 540, 240, None, False),
    # an app's own tint (ACCENT_POLICY): explicit alpha 0 is the blur alone, not the default tint; 50 % red as given
    ("Acrylic tint alpha 0", 40, 440, None, True), ("Gradient red 50 %", 290, 440, 0x80ff0000, True)]
rc = 0
for name, x, y, tint, checked in rows:
    got = px[x + 110, y + 90]   # the middle of the 220x150 window
    if tint is None:
        want = bg
    else:
        a = (tint >> 24) / 255.0
        t = ((tint >> 16) & 255, (tint >> 8) & 255, tint & 255)
        want = tuple(round(bg[i] * (1 - a) + t[i] * a) for i in range(3))
    ok = all(abs(got[i] - want[i]) <= tol for i in range(3))
    if not checked:
        print("INFO", name, "got", got, "(no table row; background", bg, ")")
        continue
    print("PASS" if ok else "FAIL", name, "got", got, "want", want)
    if not ok:
        rc = 1
print("desktop background at 1200,700:", px[1200, 700], "expected", bg)
if px[1200, 700] != bg:
    print("INFO the desktop colour is not the one asked for; the Glass row is against it anyway")
sys.exit(rc)
