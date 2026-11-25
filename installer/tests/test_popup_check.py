#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""popup-check.py on synthetic screens: a popup shaped to its card (no black box), a black box, and a soft shadow.
    python3 installer/tests/test_popup_check.py"""
import importlib.util
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
CHECK = HERE / "popup-check.py"
W, H = 400, 300
POPUP = (100, 100, 120, 80)   # x, y, w, h
BEFORE = "window id=0x1 viewable=1 size=400x300 at=0,0 class=explorer.exe name=desktop\n"
AFTER = BEFORE + "window id=0x2 viewable=1 size=120x80 at=100,100 class=explorer.exe name=\n"


def screen(path, outside=(200, 120, 20), shadow=False, black_box=False):
    """A flat screen with a white popup; around it the background, a black box, or a soft shadow."""
    pixels = bytearray(bytes(outside) * (W * H))
    x0, y0, w, h = POPUP
    for y in range(H):
        for x in range(W):
            dx = x0 - x if x < x0 else x - (x0 + w) + 1 if x >= x0 + w else 0
            dy = y0 - y if y < y0 else y - (y0 + h) + 1 if y >= y0 + h else 0
            d = max(dx, dy)
            i = (y * W + x) * 3
            if d == 0:
                pixels[i:i + 3] = b"\xff\xff\xff"
            elif black_box and d <= 6:
                pixels[i:i + 3] = b"\0\0\0"
            elif shadow and d <= 12:
                k = (13 - d) / 13 * 0.3
                pixels[i:i + 3] = bytes(int(c * (1 - k)) for c in outside)
    Path(path).write_bytes(b"P6\n%d %d\n255\n" % (W, H) + bytes(pixels))


class PopupCheck(unittest.TestCase):
    def run_check(self, composition, **kw):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "before.txt").write_text(BEFORE)
            (d / "after.txt").write_text(AFTER)
            screen(d / "screen.ppm", **kw)
            r = subprocess.run([sys.executable, str(CHECK), d / "before.txt", d / "after.txt", d / "screen.ppm",
                                composition], capture_output=True, text=True)
        return r.returncode, r.stdout

    def test_shaped_popup_without_composition_passes(self):
        self.assertEqual(self.run_check("0")[0], 0)

    def test_black_box_fails_in_both_modes(self):
        for composition in ("0", "1"):
            rc, out = self.run_check(composition, black_box=True)
            self.assertEqual(rc, 1, out)
            self.assertIn("black box", out)

    def test_shadow_passes_with_composition(self):
        rc, out = self.run_check("1", shadow=True)
        self.assertEqual(rc, 0, out)
        self.assertIn("shadow on:", out)

    def test_no_shadow_fails_with_composition(self):
        rc, out = self.run_check("1")
        self.assertEqual(rc, 1, out)
        self.assertIn("no side has a shadow", out)

    def test_no_popup_fails(self):
        with tempfile.TemporaryDirectory() as d:
            d = Path(d)
            (d / "a.txt").write_text(BEFORE)
            screen(d / "s.ppm")
            r = subprocess.run([sys.executable, str(CHECK), d / "a.txt", d / "a.txt", d / "s.ppm", "0"],
                               capture_output=True, text=True)
        self.assertEqual(r.returncode, 1)
        self.assertIn("no new viewable window", r.stdout)


if __name__ == "__main__":
    unittest.main()
