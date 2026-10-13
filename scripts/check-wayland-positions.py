# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Compare stationary placement captures, including a separate Wine process."""
import argparse
from PIL import Image, ImageChops

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('x11')
parser.add_argument('wayland')
args = parser.parse_args()
old, new = [Image.open(path).convert('RGB') for path in (args.x11, args.wayland)]
assert old.size == new.size == (1280, 800), (old.size, new.size)
colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255), (255, 0, 255)]
for i, color in enumerate(colors):
    x, y = 40 + (i % 3) * 180, 50 + (i // 3) * 170
    region = new.crop((x, y, x + 120, y + 100))
    assert not ImageChops.difference(region, Image.new('RGB', region.size, color)).getbbox(), i
assert not ImageChops.difference(old.crop((0, 0, 600, 350)), new.crop((0, 0, 600, 350))).getbbox()
region = new.crop((740, 50, 860, 150))
assert not ImageChops.difference(region, Image.new('RGB', region.size, colors[0])).getbbox()
print('PASS: six windows equal X11 pixel-for-pixel; separate process at (740,50), 120x100')
