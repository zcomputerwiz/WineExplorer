#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Wine-free checks for explicit, private-prefix theme setup."""
import importlib.util
from pathlib import Path
import subprocess
import unittest
from unittest import mock

spec = importlib.util.spec_from_file_location('correctness', Path(__file__).with_name('check-g3-correctness.py'))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ThemeSetup(unittest.TestCase):
    def configure(self, mode, style=None, code=0):
        environment = {'WINEPREFIX': '/private/owned-prefix'}
        with mock.patch.object(runner, 'run_logged', return_value=subprocess.CompletedProcess([], code)) as launch:
            records = runner.configure_private_theme(Path('/owned/wine'), environment, Path('/owned/logs'),
                                                     'x86', mode, style)
        return records, environment, launch

    def test_default_does_not_mutate(self):
        records, environment, launch = self.configure('default')
        self.assertEqual(records, [])
        self.assertEqual(environment, {'WINEPREFIX': '/private/owned-prefix'})
        launch.assert_not_called()

    def test_classic_explicitly_disables_themes(self):
        records, environment, launch = self.configure('classic')
        self.assertEqual(len(records), 1)
        self.assertEqual(launch.call_args.args[0][-6:], ['ThemeActive', '/t', 'REG_SZ', '/d', '0', '/f'])
        self.assertEqual(environment['WEX_G4_THEME_EXPECT'], 'classic')

    def test_open_style_path_and_scheme_are_bound(self):
        records, environment, launch = self.configure('open-luna', Path('/owned/Luna.msstyles'))
        self.assertEqual(len(records), 4)
        self.assertEqual(environment['WEX_G4_THEME_STYLE'], r'Z:\owned\Luna.msstyles')
        self.assertEqual([c.args[0][-2] for c in launch.call_args_list],
                         ['1', r'Z:\owned\Luna.msstyles', 'NormalColor', 'NormalSize'])
        self.assertTrue(all(r['exit_code'] == 0 for r in records))

    def test_failed_registry_setup_is_not_accepted(self):
        with self.assertRaisesRegex(RuntimeError, 'Private theme setup failed'):
            self.configure('classic', code=1)


if __name__ == '__main__':
    unittest.main()
