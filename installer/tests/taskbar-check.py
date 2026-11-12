#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Is the taskbar painted? Reads a screenshot (binary PPM, as QEMU's screendump writes it) and counts the black
pixels along two rows of a bottom taskbar, between the Start button and the notification area. Until Wine
4a1d6d5a25e, BeginBufferedPaint clipped every buffer away and that stretch stayed black (docs/STOCK_CHANGES.md).

    taskbar-check.py SCREEN.ppm [--height 30]

Prints "taskbar: N% black (WIDTH pixels per row)"; exits 1 when 90% or more is black, 2 on a file it cannot read."""
import argparse
import sys


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise ValueError("not an 8-bit binary PPM")
    width, height = int(fields[1]), int(fields[2])
    return width, height, data[pos + 1:]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("screen")
    parser.add_argument("--height", type=int, default=30, help="the taskbar's height in pixels")
    args = parser.parse_args()
    try:
        width, height, pixels = read_ppm(args.screen)
    except (OSError, ValueError, IndexError) as error:
        print(f"taskbar: cannot read {args.screen}: {error}")
        return 2
    left, right = 150, width - 220   # after the Start button and Quick Launch, before the notification area
    black = total = 0
    for y in (height - args.height // 2, height - 4):
        row = y * width * 3
        for x in range(left, right):
            r, g, b = pixels[row + 3 * x:row + 3 * x + 3]
            black += r < 16 and g < 16 and b < 16
            total += 1
    share = 100 * black // max(total, 1)
    print(f"taskbar: {share}% black ({right - left} pixels per row)")
    return 1 if share >= 90 else 0


if __name__ == "__main__":
    sys.exit(main())
