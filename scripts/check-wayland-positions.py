# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Compare stationary placement captures, including a separate Wine process."""
import argparse
from PIL import Image, ImageChops

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('x11')
parser.add_argument('wayland')
parser.add_argument('--tolerance', type=int, default=0)
args = parser.parse_args()
old, new = [Image.open(path).convert('RGB') for path in (args.x11, args.wayland)]
assert old.size == (1280,800) and all(abs(a-b) <= args.tolerance for a,b in zip(old.size,new.size)), (old.size,new.size)
colors = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255), (255, 0, 255)]
def bounds(picture, color, x, y):
    region = picture.crop((x-4,y-4,x+124,y+104))
    mask = Image.new('L',region.size)
    pixels = region.get_flattened_data() if hasattr(region,'get_flattened_data') else region.getdata()
    mask.putdata([255 if pixel == color else 0 for pixel in pixels])
    box = mask.getbbox()
    assert box is not None
    return (box[0]+x-4,box[1]+y-4,box[2]+x-4,box[3]+y-4)
for i, color in enumerate(colors):
    x, y = 40 + (i % 3) * 180, 50 + (i // 3) * 170
    expected = bounds(old,color,x,y)
    actual = bounds(new,color,x,y)
    assert all(abs(a-b) <= args.tolerance for a,b in zip(expected,actual)), (i,expected,actual)
    print(i,actual)
if not args.tolerance:
    assert not ImageChops.difference(old.crop((0, 0, 600, 350)), new.crop((0, 0, 600, 350))).getbbox()
actual = bounds(new,colors[0],740,50)
assert all(abs(a-b) <= args.tolerance for a,b in zip((740,50,860,150),actual)), actual
print('PASS: six X11 placements and separate process, tolerance',args.tolerance)
