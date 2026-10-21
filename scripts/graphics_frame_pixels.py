# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Read-only pixel oracle for the unthemed native diagnostic frame at scale1."""


def frame_pixels(image, state):
    image = image.convert('RGB')
    x, y, width, height = (state[key] for key in ('x', 'y', 'width', 'height'))
    if width < 100 or height < 120 or x < 0 or y < 0 or x + width > image.width or y + height > image.height:
        raise ValueError('Diagnostic window is outside the owned capture')
    neutral = lambda pixel: min(pixel) >= 128 and max(pixel) - min(pixel) <= 4
    strips = dict(left=[image.getpixel((x + 1, row)) for row in range(y + 80, y + height - 8)],
                  right=[image.getpixel((x + width - 2, row)) for row in range(y + 80, y + height - 8)],
                  bottom=[image.getpixel((column, y + height - 2)) for column in range(x + 8, x + width - 8)])
    fractions = {name: sum(map(neutral, pixels)) / len(pixels) for name, pixels in strips.items()}
    blue = sum(pixel == (21, 101, 192) for pixel in image.getdata())
    return dict(rim_fractions=fractions, blue_pixels=blue,
                passed=all(value >= .98 for value in fractions.values()) and blue == 1600)
