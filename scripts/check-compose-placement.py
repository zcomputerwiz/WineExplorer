# SPDX-License-Identifier: GPL-2.0-or-later
"""Compare --stationary compose-probe captures and Win32 rectangle reports."""
import re
import sys
from PIL import Image, ImageChops

x11_image, wayland_image, x11_rects, wayland_rects = sys.argv[1:]
expected = [(110,500,910,660), (180,170,460,330), (330,230,610,400),
            (520,150,740,350), (650,370,910,470)]
def rectangles(path):
    with open(path, encoding='utf-8') as stream:
        lines = re.findall(r'^RECT (\d+) (-?\d+) (-?\d+) (-?\d+) (-?\d+)', stream.read(), re.M)
    assert len(lines) == 5, path
    return [tuple(map(int, line[1:])) for line in lines]
for path in (x11_rects, wayland_rects):
    rects = rectangles(path)
    for actual, target in zip(rects, expected):
        assert all(abs(a-b) <= 1 for a,b in zip(actual,target)), (path,actual,target)
with Image.open(x11_image) as first, Image.open(wayland_image) as second:
    first, second = first.convert('RGB'), second.convert('RGB')
    assert first.size == second.size == (1024,768)
    # These regions have no animated text, cursor or shell decorations.
    for region in expected[1:4]:
        delta = ImageChops.difference(first.crop(region), second.crop(region))
        assert max(high for low,high in delta.getextrema()) <= 1, ('render mismatch', region)
    bg = second.getpixel((750,200))
    assert second.getpixel((650,200)) == bg, 'color-key hole is opaque'
    uniform = second.getpixel((400,350))
    alpha_target = tuple(round((a+b)/2) for a,b in zip(bg,(0,80,255)))
    assert all(abs(a-b) <= 1 for a,b in zip(uniform,alpha_target)), (uniform,alpha_target)
print('COMPOSE_PLACEMENT_AND_ALPHA_PASSED: five rectangles within 1 px; alpha regions match X11')
