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


class DriverSetup(unittest.TestCase):
    def test_selection_clears_inherited_loader(self):
        for driver in ('software', 'd3d12'):
            env = dict(GALLIUM_DRIVER='other', MESA_LOADER_DRIVER_OVERRIDE='other', LD_LIBRARY_PATH='/other')
            runner.configure_gl_driver(env, driver)
            self.assertNotIn('MESA_LOADER_DRIVER_OVERRIDE', env)
            self.assertEqual(env['LIBGL_ALWAYS_SOFTWARE'], '1' if driver == 'software' else '0')
            if driver == 'software':
                self.assertNotIn('GALLIUM_DRIVER', env)
                self.assertNotIn('LD_LIBRARY_PATH', env)
            else:
                self.assertEqual(env['GALLIUM_DRIVER'], 'd3d12')
                self.assertEqual(env['LD_LIBRARY_PATH'], '/usr/lib/wsl/lib')

    def test_no_silent_acceleration_fallback(self):
        good = 'Accelerated: yes\nOpenGL renderer string: D3D12 (Intel test)\n'
        for code, text in ((0, good), (1, good), (0, good.replace('yes', 'no')),
                           (0, good.replace('Intel test', 'llvmpipe')), (0, '')):
            with mock.patch.object(runner, 'run_logged',
                                   return_value=subprocess.CompletedProcess([], code, text, '')):
                if code == 0 and text == good:
                    self.assertTrue(runner.verify_gl_driver({}, Path('/owned'), 'd3d12')['passed'])
                else:
                    with self.assertRaises(RuntimeError):
                        runner.verify_gl_driver({}, Path('/owned'), 'd3d12')


if __name__ == '__main__':
    unittest.main()
