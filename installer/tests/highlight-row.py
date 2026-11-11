#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""The text row of a QEMU screendump (binary PPM) that carries the highlight bar of the Advanced Options menu or
the failure screen (scripts/wex-advanced-options.py). As on XP, the bar starts at column 0 in the console's light
gray, and nothing else on those screens has that colour there. Prints the 0-based row, or nothing when no row has
the bar. installer/tests/safe-mode.sh uses it to move the highlight to a known row whatever key presses were lost.

    highlight-row.py SCREEN.ppm [CELL_HEIGHT]     (default 16, the Linux console's 8x16 font)
"""
import sys


def read_ppm(path):
    """(width, height, RGB bytes) of an 8-bit binary PPM."""
    with open(path, "rb") as f:
        data = f.read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":           # a comment runs to the end of its line
            while data[pos:pos + 1] not in (b"\n", b""):
                pos += 1
            continue
        start = pos
        while data[pos:pos + 1] and not data[pos:pos + 1].isspace():
            pos += 1
        fields.append(data[start:pos])
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise SystemExit(f"{path}: not an 8-bit binary PPM")
    return int(fields[1]), int(fields[2]), data[pos + 1:]


def light_gray(pixel):
    """The console's light gray (VGA colour 7, 170 on the Linux console): the bar, not the dark gray of an item
    that does not apply, nor the white or blue of other text."""
    r, g, b = pixel
    return 140 <= r <= 210 and abs(r - g) < 16 and abs(r - b) < 16


def bar_row(width, height, pixels, cell=16):
    for row in range(height // cell):
        y = row * cell + cell // 2
        if all(light_gray(pixels[(y * width + x) * 3:(y * width + x) * 3 + 3]) for x in (1, 4, 6)):
            return row
    return None


def main(argv):
    if len(argv) not in (2, 3):
        raise SystemExit(__doc__)
    width, height, pixels = read_ppm(argv[1])
    row = bar_row(width, height, pixels, int(argv[2]) if len(argv) == 3 else 16)
    if row is not None:
        print(row)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
