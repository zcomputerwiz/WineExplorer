#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Synthetic capture validation tests, not real desktop acceptance."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image

spec = importlib.util.spec_from_file_location('audit', Path(__file__).with_name('check-oobe-captures.py'))
a = importlib.util.module_from_spec(spec)
spec.loader.exec_module(a)


class CaptureTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix='wex-capture-audit-')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)

    def capture(self, name, size=(1280, 800)):
        path = self.root / name
        Image.new('RGB', size, 'black').save(path)
        return path

    def test_wrong_resolution_rejected(self):
        data = a.audit(self.capture('classic-800x600-desktop.png'), ocr=False)
        self.assertTrue(data['rejections'])
        self.assertEqual((data['width'], data['height']), (1280, 800))

    def test_login_screen_not_desktop_evidence(self):
        with patch.object(a.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0,
                b'To begin, click your user name. Alice Logged on', b'')):
            data = a.audit(self.capture('luna-1280x800-desktop.png'))
        self.assertIn('Login screen', data['rejections'][0])

    def test_matching_dimensions_do_not_certify_theme(self):
        data = a.audit(self.capture('classic-1280x800-taskbar.png'), ocr=False)
        self.assertFalse(data['rejections'])
        self.assertTrue(data['human_theme_backend_and_state_review_required'])

    def test_corrupt_capture_is_error(self):
        path = self.root / 'luna-800x600.png'
        path.write_bytes(b'not a screenshot')
        with self.assertRaises(OSError):
            a.audit(path)


if __name__ == '__main__':
    unittest.main()
