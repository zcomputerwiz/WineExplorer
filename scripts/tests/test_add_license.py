#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Protect existing notices while distinguishing GNU product names from licenses."""

import importlib.util
from pathlib import Path
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location(
    "add_license", Path(__file__).resolve().parents[1] / "add-license.py")
HEADERS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HEADERS)


class LicenseNoticeTests(unittest.TestCase):
    def process_source(self, source):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "example.c"
            path.write_text(source, encoding="utf-8")
            changed = HEADERS.process(str(path), False)
            return changed, path.read_text(encoding="utf-8")

    def test_product_name_does_not_count_as_notice(self):
        source = 'const char *product = "Debian GNU/Linux";\n'
        changed, result = self.process_source(source)
        self.assertTrue(changed)
        self.assertIn("SPDX-License-Identifier: GPL-2.0-or-later", result)
        self.assertTrue(result.endswith(source))

    def test_wrapped_lgpl_notice_is_preserved(self):
        source = "/*\n * GNU Lesser General Public\n * License, version 2.1 or later.\n */\nint value;\n"
        changed, result = self.process_source(source)
        self.assertFalse(changed)
        self.assertEqual(result, source)

    def test_explicit_spdx_notice_is_preserved(self):
        source = "/* SPDX-License-Identifier: MIT */\nint value;\n"
        changed, result = self.process_source(source)
        self.assertFalse(changed)
        self.assertEqual(result, source)


if __name__ == "__main__":
    unittest.main()
