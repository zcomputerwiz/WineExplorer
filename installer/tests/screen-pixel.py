#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""One pixel of a QEMU screendump (binary PPM), as "R G B" in decimal. installer/tests/safe-mode.sh uses it to check a
screen's background, for example the black of Safe Mode's last-resort logon screen (work order A4). A negative X or
Y counts from the right or bottom edge (-1 is the last column or row).

    screen-pixel.py SCREEN.ppm X Y
"""
import importlib.util
import sys
from pathlib import Path

_spec = importlib.util.spec_from_file_location("highlight_row", Path(__file__).with_name("highlight-row.py"))
_highlight_row = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_highlight_row)


def main(argv):
    if len(argv) != 4:
        raise SystemExit(__doc__)
    width, height, pixels = _highlight_row.read_ppm(argv[1])
    x, y = int(argv[2]), int(argv[3])
    x, y = x + width if x < 0 else x, y + height if y < 0 else y
    if not (0 <= x < width and 0 <= y < height):
        raise SystemExit(f"{x},{y} is outside the {width}x{height} screen")
    offset = (y * width + x) * 3
    print(*pixels[offset:offset + 3])
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
