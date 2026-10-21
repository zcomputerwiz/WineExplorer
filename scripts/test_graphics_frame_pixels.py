#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
import unittest
from PIL import Image
from graphics_frame_pixels import frame_pixels


class FramePixels(unittest.TestCase):
    def fixture(self):
        # Synthetic control only: neutral rim, non-neutral client and blue square.
        image = Image.new('RGB', (400, 300), 'black')
        for x in range(10, 310):
            for y in range(10, 260):
                image.putpixel((x, y), (255, 255, 255) if x < 14 or x >= 306 or y < 70 or y >= 256 else (232, 245, 233))
        for x in range(60, 100):
            for y in range(110, 150):
                image.putpixel((x, y), (21, 101, 192))
        return image, dict(x=10, y=10, width=300, height=250)

    def test_complete_frame(self):
        image, state = self.fixture()
        self.assertTrue(frame_pixels(image, state)['passed'])

    def test_missing_rim_or_missing_client_content(self):
        for which in ('bottom', 'right', 'blue'):
            image, state = self.fixture()
            for x in range(10, 310):
                for y in range(10, 260):
                    if ((which == 'bottom' and y >= 256) or (which == 'right' and x >= 306)
                            or (which == 'blue' and image.getpixel((x, y)) == (21, 101, 192))):
                        image.putpixel((x, y), (232, 245, 233))
            self.assertFalse(frame_pixels(image, state)['passed'], which)

    def test_offscreen_geometry_rejected(self):
        image, state = self.fixture()
        state['width'] = 1000
        with self.assertRaises(ValueError):
            frame_pixels(image, state)


if __name__ == '__main__':
    unittest.main()
