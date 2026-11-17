#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Unit tests for wexcommon module."""

import os
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from unittest import mock

# python3 -I ignores the current directory: the module is next to this test
sys.path.insert(0, os.path.dirname(os.path.realpath(__file__)))
import wexcommon  # noqa: E402
from wexcommon import (
    COUNTDOWN_MAX,
    atomic_write,
    boot_conf_value,
    read_boot_conf,
    recovery_enabled,
    set_boot_conf_value,
)


class TestWexCommon(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="wexcommon_test_"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_duplicate_key_uses_documented_default(self):
        """Duplicate keys indicate a malformed boot.conf -> use documented defaults."""
        conf = self.tmp / "boot.conf"

        # Duplicate recovery_timeout uses documented default 30
        conf.write_text("recovery_timeout=10\nrecovery_timeout=20\n", encoding="utf-8")
        values = read_boot_conf(conf)
        self.assertEqual(values["recovery_timeout"], "30")
        self.assertEqual(boot_conf_value("recovery_timeout", conf), "30")

        # Duplicate recovery_menu uses documented default 1
        conf.write_text("recovery_menu=0\nrecovery_menu=0\n", encoding="utf-8")
        values = read_boot_conf(conf)
        self.assertEqual(values["recovery_menu"], "1")
        self.assertEqual(boot_conf_value("recovery_menu", conf), "1")
        self.assertTrue(recovery_enabled(conf))

        # Both duplicated
        conf.write_text(
            "recovery_timeout=5\nrecovery_menu=0\nrecovery_timeout=15\nrecovery_menu=0\n",
            encoding="utf-8",
        )
        values = read_boot_conf(conf)
        self.assertEqual(values, {"recovery_timeout": "30", "recovery_menu": "1"})
        self.assertEqual(boot_conf_value("recovery_timeout", conf), "30")
        self.assertEqual(boot_conf_value("recovery_menu", conf), "1")
        self.assertTrue(recovery_enabled(conf))

        # Single occurrence returns file value
        conf.write_text("recovery_timeout=7\nrecovery_menu=0\n", encoding="utf-8")
        values = read_boot_conf(conf)
        self.assertEqual(values, {"recovery_timeout": "7", "recovery_menu": "0"})
        self.assertEqual(boot_conf_value("recovery_timeout", conf), "7")
        self.assertEqual(boot_conf_value("recovery_menu", conf), "0")
        self.assertFalse(recovery_enabled(conf))

        # Missing key returns default
        conf.write_text("# empty\n", encoding="utf-8")
        self.assertEqual(read_boot_conf(conf), {})
        self.assertEqual(boot_conf_value("recovery_timeout", conf), "30")
        self.assertEqual(boot_conf_value("recovery_menu", conf), "1")
        self.assertEqual(boot_conf_value("unknown_key", conf, default="fallback"), "fallback")

    def test_set_boot_conf_value_rewrites_single_line_and_appends_missing_key(self):
        """set_boot_conf_value rewrites single occurrence in place and appends missing key."""
        conf = self.tmp / "boot.conf"
        conf.write_text("# header\nother=123\nrecovery_timeout=30\n", encoding="utf-8")

        # Rewriting a single line keeps other lines and comments
        set_boot_conf_value(conf, "recovery_timeout", "10")
        self.assertEqual(
            conf.read_text(encoding="utf-8"),
            "# header\nother=123\nrecovery_timeout=10\n",
        )

        # Appending a missing key
        set_boot_conf_value(conf, "recovery_menu", "0")
        self.assertEqual(
            conf.read_text(encoding="utf-8"),
            "# header\nother=123\nrecovery_timeout=10\nrecovery_menu=0\n",
        )

        # Creating a new file starts with header
        new_conf = self.tmp / "new_boot.conf"
        set_boot_conf_value(new_conf, "recovery_timeout", "7", header="# New file\n")
        self.assertEqual(
            new_conf.read_text(encoding="utf-8"),
            "# New file\nrecovery_timeout=7\n",
        )

        # Setting value on a file with duplicate keys cleans them up to a single line
        dup_conf = self.tmp / "dup_boot.conf"
        dup_conf.write_text("recovery_timeout=5\nrecovery_timeout=10\n", encoding="utf-8")
        set_boot_conf_value(dup_conf, "recovery_timeout", "15")
        self.assertEqual(dup_conf.read_text(encoding="utf-8"), "recovery_timeout=15\n")

    def test_atomic_write_leaves_no_temp_file_and_replaces_atomically(self):
        """atomic_write replaces target atomically and leaves no leftover temporary files."""
        dest_dir = self.tmp / "target_dir"
        target = dest_dir / "test.txt"

        atomic_write(target, "first data\n")
        self.assertEqual(target.read_text(encoding="utf-8"), "first data\n")
        entries = [e.name for e in dest_dir.iterdir()]
        self.assertEqual(entries, ["test.txt"])

        # Atomically replace existing file
        atomic_write(target, "second data\n")
        self.assertEqual(target.read_text(encoding="utf-8"), "second data\n")
        entries = [e.name for e in dest_dir.iterdir()]
        self.assertEqual(entries, ["test.txt"])

        # Error during write/replace cleans up temporary file and leaves target untouched
        with mock.patch("os.replace", side_effect=OSError("simulated replace failure")):
            with self.assertRaises(OSError):
                atomic_write(target, "should fail\n")

        self.assertEqual(target.read_text(encoding="utf-8"), "second data\n")
        entries = [e.name for e in dest_dir.iterdir()]
        self.assertEqual(entries, ["test.txt"])

    def test_symlinked_directory_is_refused(self):
        """atomic_write refuses to write to a symlinked target directory."""
        real_dir = self.tmp / "real_dir"
        real_dir.mkdir()
        link_dir = self.tmp / "symlink_dir"

        has_link = False
        try:
            os.symlink(real_dir, link_dir, target_is_directory=True)
            has_link = True
        except OSError:
            try:
                import _winapi

                _winapi.CreateJunction(str(real_dir), str(link_dir))
                has_link = True
            except (ImportError, OSError):
                pass

        if has_link:
            with self.assertRaises(ValueError):
                atomic_write(link_dir / "output.txt", "data\n")
            self.assertFalse((real_dir / "output.txt").exists())

        # Also verify with mock to guarantee test coverage regardless of OS filesystem privileges
        with mock.patch("wexcommon._is_symlink_dir", return_value=True):
            with self.assertRaises(ValueError):
                atomic_write(self.tmp / "mock_dir" / "output.txt", "data\n")


if __name__ == "__main__":
    unittest.main()
