#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
import unittest
from PIL import Image
from graphics_frame_pixels import frame_pixels, recovery_page_pixels, recovery_pixels


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

    def recovery_images(self, scale=1):
        state = dict(x=20, y=20, width=300, height=250, fixed=65536 * scale, visible=1,
                     client_bounds={'x': 20, 'y': 20, 'width': 300, 'height': 250})
        marker = dict(x=50, y=100, width=40, height=40)
        background = (232, 245, 233)
        front = Image.new('RGB', (400 * scale, 320 * scale), 'black')
        for x in range(20 * scale, 320 * scale):
            for y in range(20 * scale, 270 * scale):
                front.putpixel((x, y), (255, 255, 255) if x < 24 * scale or x >= 316 * scale
                               or y < 80 * scale or y >= 266 * scale else background)
        page = Image.new('RGB', (300 * scale, 250 * scale), background)
        for x in range((marker['x'] + 5) * scale, (marker['x'] + 35) * scale):
            for y in range((marker['y'] + 5) * scale, (marker['y'] + 35) * scale):
                page.putpixel((x, y), (21, 101, 192))
                front.putpixel((state['x'] * scale + x, state['y'] * scale + y), (21, 101, 192))
        return front, page, state, marker, background

    def test_recovery_front_and_page_use_observed_geometry_scale_and_pixels(self):
        front, page, state, marker, background = self.recovery_images()
        verdict = recovery_pixels(front, state, 1, marker, background)
        self.assertTrue(verdict['passed'])
        self.assertTrue(recovery_page_pixels(page, {'width': 300, 'height': 250}, 1,
                                             marker, background)['passed'])
        recovered, _, _, _, _ = self.recovery_images()
        self.assertTrue(recovery_pixels(recovered, state, 1, marker, background,
                                        verdict['rim'])['passed'])

    def test_recovery_rejects_blank_partial_wrong_scale_crop_and_missing_marker(self):
        front, page, state, marker, background = self.recovery_images()
        for image in (Image.new('RGB', front.size, 'black'), Image.new('RGB', page.size, background)):
            with self.subTest(size=image.size):
                verdict = (recovery_pixels(image, state, 1, marker, background)
                           if image.size == front.size else recovery_page_pixels(
                               image, {'width': 300, 'height': 250}, 1, marker, background))
                self.assertFalse(verdict['passed'])
        with self.assertRaises(ValueError):
            recovery_page_pixels(page, {'width': 300, 'height': 250}, 1.25, marker, background)
        with self.assertRaises(ValueError):
            recovery_pixels(front, state, 1.25, marker, background)
        with self.assertRaises(ValueError):
            recovery_pixels(front.crop((0, 0, 200, 200)), state, 1, marker, background)
        no_front_marker = front.copy()
        no_front_marker.paste(Image.new('RGB', (40, 40), background), (70, 120))
        self.assertFalse(recovery_pixels(no_front_marker, state, 1, marker, background)['passed'])
        no_marker = page.copy()
        no_marker.paste(Image.new('RGB', (40, 40), background), (50, 100))
        self.assertFalse(recovery_page_pixels(no_marker, {'width': 300, 'height': 250}, 1,
                                              marker, background)['passed'])
        partial_page = page.copy()
        partial_page.paste(Image.new('RGB', (20, 40), background), (50, 100))
        self.assertFalse(recovery_page_pixels(partial_page, {'width': 300, 'height': 250}, 1,
                                              marker, background)['passed'])

    def test_recovery_rejects_changed_frame_rim(self):
        front, _, state, marker, background = self.recovery_images()
        baseline = recovery_pixels(front, state, 1, marker, background)['rim']
        changed = front.copy()
        for y in range(100, 262):
            changed.putpixel((21, y), (0, 0, 0))
        self.assertFalse(recovery_pixels(changed, state, 1, marker, background, baseline)['passed'])


if __name__ == '__main__':
    unittest.main()
