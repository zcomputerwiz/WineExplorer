#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Isolated regression checks: no real prefixes, servers, downloads or installers."""
from contextlib import nullcontext, redirect_stdout
import importlib.util
import io
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


graphics = module('graphics', ROOT / 'scripts/wex-graphics.py')
components = module('components', ROOT / 'scripts/wex-components.py')
staging = module('staging', ROOT / 'installer/stage-runtime-assets.py')
fonts = module('font_targets', ROOT / 'scripts/check-font-targets.py')


class GraphicsTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix='wex-graphics-test-')
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.shared = self.root / 'machine/windows'
        for directory in ('system32', 'syswow64'):
            (self.shared / directory).mkdir(parents=True)
            for name in graphics.DLLS:
                (self.shared / directory / (name + '.dll')).write_text('builtin-' + name)
        (self.shared / 'Fonts').mkdir()
        self.prefixes = []
        for user in ('alice', 'bob'):
            prefix = self.root / user
            (prefix / 'drive_c').mkdir(parents=True)
            (prefix / 'drive_c/windows').symlink_to(self.shared, target_is_directory=True)
            (prefix / 'user.reg').write_text('user overrides stay unchanged')
            self.prefixes.append(prefix)
        self.prefix = self.prefixes[0]
        self.libraries = self.root / 'libraries'
        for arch, machine in (('x32', 0x14c), ('x64', 0x8664)):
            (self.libraries / arch).mkdir(parents=True)
            data = bytearray(128)
            data[:2] = b'MZ'
            struct.pack_into('<I', data, 0x3c, 64)
            data[64:68] = b'PE\0\0'
            struct.pack_into('<H', data, 68, machine)
            for name in graphics.DLLS:
                (self.libraries / arch / (name + '.dll')).write_bytes(data)
        self.addCleanup(patch.stopall)
        patch.dict(os.environ, WINEPREFIX=str(self.prefix), WEX_DXVK_DIR=str(self.libraries)).start()
        self.run_mock = patch.object(graphics.subprocess, 'run', return_value=
                                     subprocess.CompletedProcess([], 0, 'GPU0:\n apiVersion = 1.3.0\n', '')).start()

    def command(self, action):
        output = io.StringIO()
        with patch.object(sys, 'argv', ['wex-graphics', 'vulkan', action]), redirect_stdout(output):
            graphics.main()
        return output.getvalue()

    def test_on_defers_changes_until_login(self):
        self.command('on')
        self.assertEqual((self.prefix / 'drive_c/windows').resolve(), self.shared)
        self.assertEqual(self.run_mock.call_count, 1)
        self.assertEqual(self.run_mock.call_args.args[0], ['vulkaninfo', '--summary'])

    def test_symlinked_lock_cannot_truncate_shared_dll(self):
        state = self.prefix / '.wex-graphics'
        state.mkdir()
        target = self.shared / 'system32/d3d11.dll'
        (state / 'lock').symlink_to(target)
        with self.assertRaises(OSError):
            self.command('on')
        self.assertEqual(target.read_text(), 'builtin-d3d11')

    def test_on_prepare_off_is_private_and_does_not_wait_for_server(self):
        self.command('on')
        self.assertEqual(self.command('prepare').strip(), graphics.OVERRIDES)
        for directory, arch in (('system32', 'x64'), ('syswow64', 'x32')):
            alice = self.prefix / 'drive_c/windows' / directory / 'd3d11.dll'
            bob = self.prefixes[1] / 'drive_c/windows' / directory / 'd3d11.dll'
            self.assertEqual(alice.resolve(), self.libraries / arch / 'd3d11.dll')
            self.assertEqual(bob.read_text(), 'builtin-d3d11')
        self.assertEqual((self.prefix / 'user.reg').read_text(), 'user overrides stay unchanged')
        self.command('off')
        self.command('prepare')
        self.assertEqual((self.prefix / 'drive_c/windows').resolve(), self.shared)
        self.assertTrue(all(call.args[0] == ['vulkaninfo', '--summary'] for call in self.run_mock.call_args_list))

    def test_repeated_prepare_picks_up_shared_updates(self):
        self.command('on')
        self.command('prepare')
        (self.shared / 'system32/new.dll').write_text('new machine update')
        self.command('prepare')
        self.assertEqual((self.prefix / 'drive_c/windows/system32/new.dll').read_text(), 'new machine update')

    def test_no_device_does_not_enable(self):
        self.run_mock.return_value = subprocess.CompletedProcess([], 0, 'no devices', '')
        with self.assertRaises(ValueError):
            self.command('on')
        self.assertFalse((self.prefix / '.wex-graphics/enabled').exists())

    def test_incomplete_bundle_does_not_enable(self):
        (self.libraries / 'x32/d3d12core.dll').unlink()
        with self.assertRaises(OSError):
            self.command('on')
        self.assertFalse((self.prefix / '.wex-graphics/enabled').exists())

    def test_wrong_architecture_rejected(self):
        (self.libraries / 'x32/d3d11.dll').write_bytes((self.libraries / 'x64/d3d11.dll').read_bytes())
        with self.assertRaises(ValueError):
            self.command('on')

    def test_old_device_rejected(self):
        self.run_mock.return_value = subprocess.CompletedProcess([], 0, 'GPU0:\n apiVersion = 1.2.0\n', '')
        with self.assertRaises(ValueError):
            self.command('on')
        self.assertFalse((self.prefix / '.wex-graphics/enabled').exists())

    def test_device_disappearing_at_login_restores_shared_view(self):
        self.command('on')
        self.command('prepare')
        self.run_mock.return_value = subprocess.CompletedProcess([], 1, '', 'no devices')
        with self.assertRaises(ValueError):
            self.command('prepare')
        self.assertEqual((self.prefix / 'drive_c/windows').resolve(), self.shared)

    def test_unrecognized_windows_link_is_not_overwritten(self):
        self.command('on')
        self.command('prepare')
        windows = self.prefix / 'drive_c/windows'
        windows.unlink()
        other = self.root / 'other'
        other.mkdir()
        windows.symlink_to(other)
        with self.assertRaises(ValueError):
            self.command('restore')
        self.assertEqual(windows.resolve(), other)

    def test_broken_bundle_at_login_leaves_shared_view(self):
        self.command('on')
        self.command('prepare')
        (self.libraries / 'x32/d3d11.dll').unlink()
        with self.assertRaises(OSError):
            self.command('prepare')
        self.assertEqual((self.prefix / 'drive_c/windows').resolve(), self.shared)


class ComponentsTests(unittest.TestCase):
    def test_unsupported_verbs_rejected(self):
        for verb in ('dotnet48', 'wmp11', 'sapi', 'ucrtbase', '--force', '../vcrun2022'):
            with self.subTest(verb=verb), self.assertRaises(ValueError):
                components.validate_verb(verb)

    def test_limited_user_cannot_launch_installer(self):
        with patch.object(components, 'is_admin', return_value=False), \
                patch.object(components.subprocess, 'run') as run:
            with self.assertRaises(ValueError):
                components.frontend(['vcrun2022'], False)
            run.assert_not_called()

    def test_missing_terms_do_not_download_or_elevate(self):
        with patch.object(components, 'is_admin', return_value=True), \
                patch.object(components, 'license_text', side_effect=FileNotFoundError('vendor terms missing')), \
                patch.object(components.subprocess, 'run') as run:
            with self.assertRaises(FileNotFoundError):
                components.frontend(['vcrun2022'], False)
            run.assert_not_called()

    def test_decline_does_not_download_or_elevate(self):
        with patch.object(components, 'is_admin', return_value=True), \
                patch.object(components, 'consent', return_value=None), \
                patch.object(components.subprocess, 'run') as run:
            components.frontend(['corefonts'], False)
            run.assert_not_called()

    def test_admin_elevates_and_failed_install_is_not_success(self):
        with patch.object(components, 'is_admin', return_value=True), \
                patch.object(components.os, 'getuid', return_value=1000), \
                patch.object(components, 'consent', return_value='accepted-sha'), \
                patch.object(components.subprocess, 'run', return_value=
                             subprocess.CompletedProcess([], 42)) as run:
            with self.assertRaisesRegex(ValueError, 'exit 42'):
                components.frontend(['vcrun2022'], False)
            self.assertEqual(run.call_args.args[0], ['/usr/bin/pkexec', components.BACKEND,
                                                     '--install', 'vcrun2022', 'accepted-sha'])
            self.assertEqual(run.call_count, 1)

    def test_backend_rejects_unsupported_or_changed_terms(self):
        with patch.object(components.subprocess, 'run') as run:
            with self.assertRaises(ValueError):
                components.install('dotnet48', 'sha')
            with patch.object(components.os, 'getuid', return_value=0), \
                    patch.object(components, 'license_text', return_value=('actual terms', 'new-sha')):
                with self.assertRaisesRegex(ValueError, 'terms changed'):
                    components.install('vcrun2022', 'old-sha')
            run.assert_not_called()

    def test_protected_files_restored_even_when_installer_fails(self):
        with tempfile.TemporaryDirectory(prefix='wex-components-test-') as scratch:
            machine = Path(scratch) / 'machine'
            for directory in ('system32', 'syswow64'):
                (machine / 'drive_c/windows' / directory).mkdir(parents=True)
            dll = machine / 'drive_c/windows/system32/ucrtbase.dll'
            original = Path(scratch) / 'wine-ucrtbase.dll'
            original.write_text('open builtin')
            dll.symlink_to(original)

            def fake_reg(wine, env, operation, key, *args, **kwargs):
                return subprocess.CompletedProcess([], 0 if key.endswith('\\Software') else 1)

            with patch.object(components, 'reg', side_effect=fake_reg):
                with self.assertRaisesRegex(ValueError, 'fixture failure'):
                    with components.protect_builtins(machine, {}):
                        dll.unlink()
                        dll.write_text('native replacement')
                        (dll.parent / 'msxml99.dll').write_text('forbidden new library')
                        raise ValueError('fixture failure')
            self.assertTrue(dll.is_symlink())
            self.assertEqual(dll.read_text(), 'open builtin')
            self.assertFalse((dll.parent / 'msxml99.dll').exists())

    def test_backend_failure_has_no_success_receipt_and_sanitizes_environment(self):
        with tempfile.TemporaryDirectory(prefix='wex-backend-test-') as scratch:
            machine = Path(scratch) / 'machine'
            machine.mkdir()
            cache = Path(scratch) / 'cache'
            calls = []

            def fake_run(command, **kwargs):
                calls.append((command, kwargs))
                return subprocess.CompletedProcess(command, 42 if 'winetricks' in ' '.join(command) else 0)

            with patch.object(components, 'MACHINE', machine), patch.object(components, 'CACHE', cache), \
                    patch.object(components.os, 'getuid', return_value=0), \
                    patch.object(components, 'license_text', return_value=('vendor terms', 'sha')), \
                    patch.object(components, 'protect_builtins', return_value=nullcontext()), \
                    patch.object(components.subprocess, 'run', side_effect=fake_run), \
                    patch.dict(os.environ, WINEDLLOVERRIDES='ntdll=n', WINEPREFIX='/untrusted', DISPLAY=':0'):
                with self.assertRaisesRegex(ValueError, 'exit 42'):
                    components.install('vcrun2022', 'sha')
            install = next(call for call in calls if 'winetricks' in ' '.join(call[0]))
            self.assertEqual(install[0], ['/usr/bin/xvfb-run', '-a', '/usr/bin/winetricks', '-q', '-f', 'vcrun2022'])
            self.assertEqual(install[1]['env']['WINEPREFIX'], str(machine))
            self.assertEqual(install[1]['env']['WINEDLLOVERRIDES'], 'winemenubuilder.exe=d')
            self.assertNotIn('DISPLAY', install[1]['env'])
            self.assertFalse((machine.parent / 'component-receipts/vcrun2022.sha256').exists())


class PackagingTests(unittest.TestCase):
    def test_font_fallback_does_not_count_as_exact_family(self):
        mappings = '"Segoe UI Symbol"="Noto Sans Symbols2"\n"Arial"="Liberation Sans"\n'
        self.assertEqual(fonts.missing_families(mappings, 'DejaVu Sans\nLiberation Sans\n'), ['Noto Sans Symbols2'])
        self.assertEqual(fonts.missing_families(mappings, 'Noto Sans Symbols2\nLiberation Sans,Arial\n'), [])

    def test_corrupt_offline_asset_rejected_without_network(self):
        with tempfile.TemporaryDirectory(prefix='wex-cache-test-') as scratch:
            cache = Path(scratch)
            (cache / 'payload').write_text('corrupt')
            with patch.object(staging.urllib.request, 'urlopen') as network:
                with self.assertRaises(ValueError):
                    staging.fetch(cache, 'payload', 'https://example.invalid', '0' * 64, True)
                network.assert_not_called()

    def test_preserve_master_trebuchet_and_builtin_tahoma(self):
        mappings = (ROOT / 'src/resources/fonts/font-substitutes.reg').read_text()
        self.assertIn('"Trebuchet MS"="DejaVu Sans"', mappings)
        self.assertNotIn('"Tahoma"=', mappings)
        self.assertIn('"Segoe UI Symbol"="Noto Sans Symbols2"', mappings)


if __name__ == '__main__':
    unittest.main(verbosity=2)
