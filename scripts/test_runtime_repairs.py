#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Isolated regression checks: no real prefixes, servers, downloads or installers."""
from contextlib import nullcontext, redirect_stdout
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
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


def with_enabled_component(verb='corefonts'):
    meta = dict(components.COMPONENTS_METADATA)
    meta[verb] = dict(meta[verb], disabled=False, disabled_reason='')
    return patch.object(components, 'COMPONENTS_METADATA', meta)


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
            with self.assertRaisesRegex(ValueError, 'administrators only'):
                components.frontend(['corefonts'], False)
            run.assert_not_called()

    def test_frontend_rejects_disabled_components_before_elevation(self):
        with patch.object(components, 'is_admin', return_value=True), \
                patch.object(components.subprocess, 'run') as run:
            for verb in components.COMPONENTS:
                if components.COMPONENTS_METADATA[verb].get('disabled'):
                    with self.subTest(verb=verb), self.assertRaisesRegex(ValueError, 'is disabled'):
                        components.frontend([verb], False)
            run.assert_not_called()

    def test_backend_rejects_disabled_components_even_with_valid_terms(self):
        with patch.object(components.subprocess, 'run') as run, \
                patch.object(components.os, 'getuid', return_value=0), \
                patch.object(components, 'license_text', return_value=('actual terms', 'sha')):
            for verb in components.COMPONENTS:
                if components.COMPONENTS_METADATA[verb].get('disabled'):
                    with self.subTest(verb=verb), self.assertRaisesRegex(ValueError, 'is disabled'):
                        components.install(verb, 'sha')
            run.assert_not_called()

    def test_missing_terms_do_not_download_or_elevate(self):
        with with_enabled_component('corefonts'), \
                patch.object(components, 'is_admin', return_value=True), \
                patch.object(components, 'license_text', side_effect=FileNotFoundError('vendor terms missing')), \
                patch.object(components.subprocess, 'run') as run:
            with self.assertRaises(FileNotFoundError):
                components.frontend(['corefonts'], False)
            run.assert_not_called()

    def test_decline_does_not_download_or_elevate(self):
        with with_enabled_component('corefonts'), \
                patch.object(components, 'is_admin', return_value=True), \
                patch.object(components, 'consent', return_value=None), \
                patch.object(components.subprocess, 'run') as run:
            components.frontend(['corefonts'], False)
            run.assert_not_called()

    def test_admin_elevates_and_failed_install_is_not_success(self):
        with with_enabled_component('corefonts'), \
                patch.object(components, 'is_admin', return_value=True), \
                patch.object(components.os, 'getuid', return_value=1000), \
                patch.object(components, 'consent', return_value='accepted-sha'), \
                patch.object(components.subprocess, 'run', return_value=
                             subprocess.CompletedProcess([], 42)) as run:
            with self.assertRaisesRegex(ValueError, 'exit 42'):
                components.frontend(['corefonts'], False)
            self.assertEqual(run.call_args.args[0], ['/usr/bin/pkexec', components.BACKEND,
                                                     '--install', 'corefonts', 'accepted-sha'])
            self.assertEqual(run.call_count, 1)

    def test_backend_rejects_unsupported_or_changed_terms(self):
        with with_enabled_component('corefonts'), \
                patch.object(components, 'validate_winetricks_integrity'), \
                patch.object(components, 'validate_recipe_files'), \
                patch.object(components.subprocess, 'run') as run:
            with self.assertRaises(ValueError):
                components.install('dotnet48', 'sha')
            with patch.object(components.os, 'getuid', return_value=0), \
                    patch.object(components, 'license_text', return_value=('actual terms', 'new-sha')):
                with self.assertRaisesRegex(ValueError, 'terms changed'):
                    components.install('corefonts', 'old-sha')
            run.assert_not_called()

    def test_winetricks_integrity_validation(self):
        with tempfile.TemporaryDirectory(prefix='wex-wt-test-') as scratch:
            wt_path = Path(scratch) / 'winetricks'
            # Missing file
            with self.assertRaisesRegex(ValueError, 'missing'):
                components.validate_winetricks_integrity(wt_path)

            wt_path.write_bytes(b'#!/bin/sh\n')
            # Hash mismatch
            with self.assertRaisesRegex(ValueError, 'integrity violation'):
                components.validate_winetricks_integrity(wt_path)

            # Valid hash
            dummy_hash = hashlib.sha256(b'#!/bin/sh\n').hexdigest()
            with patch.object(components, 'APPROVED_WINETRICKS_SHA256', dummy_hash):
                components.validate_winetricks_integrity(wt_path)

            # Symlink refused
            symlink_path = Path(scratch) / 'wt_symlink'
            symlink_path.symlink_to(wt_path)
            with self.assertRaisesRegex(ValueError, 'symlinked'):
                components.validate_winetricks_integrity(symlink_path)

    def test_recipe_files_validation(self):
        with tempfile.TemporaryDirectory(prefix='wex-rec-test-') as scratch:
            wt_path = Path(scratch) / 'winetricks'
            wt_path.write_text('dummy content without recipe files', encoding='utf-8')
            with self.assertRaisesRegex(ValueError, 'integrity violation'):
                components.validate_recipe_files('corefonts', path=wt_path)

            dependencies = ('andale', 'arial', 'comicsans', 'courier', 'georgia', 'impact',
                            'times', 'trebuchet', 'verdana', 'webdings')
            valid_content = 'load_corefonts()\n{\n' + ''.join('    w_call ' + d + '\n' for d in dependencies) + '}\n'
            packages = iter(components.COMPONENTS_METADATA['corefonts']['recipe_files'])
            for dependency in dependencies:
                valid_content += 'load_' + dependency + '()\n{\n'
                for _ in range(2 if dependency == 'arial' else 1):
                    name, sha = next(packages)
                    valid_content += f'    w_download_to corefonts "https://example.invalid/{name}" {sha}\n'
                valid_content += '}\n'
            wt_path.write_text(valid_content, encoding='utf-8')
            components.validate_recipe_files('corefonts', path=wt_path)
            # Finding the right hash elsewhere in a script must not mask a
            # missing dependency download (the old validator allowed this).
            changed = valid_content.replace('arialb32.exe', 'unexpected.exe')
            changed += '\n# arialb32.exe ' + components.COMPONENTS_METADATA['corefonts']['recipe_files'][2][1]
            wt_path.write_text(changed)
            with self.assertRaisesRegex(ValueError, 'package manifest'):
                components.validate_recipe_files('corefonts', path=wt_path)

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
                        self.assertFalse(dll.is_symlink())
                        dll.write_text('write-through attempt stays in the prefix')
                        self.assertEqual(original.read_text(), 'open builtin')
                        dll.unlink()
                        dll.write_text('native replacement')
                        (dll.parent / 'msxml99.dll').write_text('forbidden new library')
                        raise ValueError('fixture failure')
            self.assertTrue(dll.is_symlink())
            self.assertEqual(dll.read_text(), 'open builtin')
            self.assertFalse((dll.parent / 'msxml99.dll').exists())

            # If stopping Wine itself fails, do not restore over live modules.
            with patch.object(components, 'reg', side_effect=fake_reg):
                with self.assertRaisesRegex(RuntimeError, 'administrator recovery required'):
                    with components.protect_builtins(machine, {}) as recovery:
                        recovery['safe_to_restore'] = False
                        dll.unlink()
                        dll.write_text('possibly live module')
            self.assertFalse(dll.is_symlink())
            self.assertEqual(dll.read_text(), 'possibly live module')
            marker = machine.parent / 'component-recovery-required'
            self.assertTrue(marker.is_file())
            self.assertTrue(Path(marker.read_text().strip()).is_dir())

    def test_backend_failure_has_no_success_receipt_and_sanitizes_environment(self):
        with tempfile.TemporaryDirectory(prefix='wex-backend-test-') as scratch:
            machine = Path(scratch) / 'machine'
            machine.mkdir()
            cache = Path(scratch) / 'cache'
            calls = []

            def fake_run(command, **kwargs):
                calls.append((command, kwargs))
                return subprocess.CompletedProcess(command, 42 if 'winetricks' in ' '.join(command) else 0)

            with with_enabled_component('corefonts'), \
                    patch.object(components, 'validate_winetricks_integrity'), \
                    patch.object(components, 'validate_recipe_files'), \
                    patch.object(components, 'MACHINE', machine), patch.object(components, 'CACHE', cache), \
                    patch.object(components.os, 'getuid', return_value=0), \
                    patch.object(components, 'license_text', return_value=('vendor terms', 'sha')), \
                    patch.object(components, 'protect_builtins', return_value=nullcontext()), \
                    patch.object(components.subprocess, 'run', side_effect=fake_run), \
                    patch.dict(os.environ, WINEDLLOVERRIDES='ntdll=n', WINEPREFIX='/untrusted', DISPLAY=':0'):
                with self.assertRaisesRegex(ValueError, 'exit 42'):
                    components.install('corefonts', 'sha')
            install = next(call for call in calls if 'winetricks' in ' '.join(call[0]))
            self.assertEqual(install[0], ['/usr/bin/timeout', '--signal=TERM',
                                         '--kill-after=10s', '1800s', '/usr/bin/xvfb-run',
                                         '-a', '/usr/bin/winetricks', '-q', 'corefonts'])
            self.assertEqual(install[1]['env']['WINEPREFIX'], str(machine))
            self.assertEqual(install[1]['env']['WINEDLLOVERRIDES'], 'winemenubuilder.exe=d')
            self.assertNotIn('DISPLAY', install[1]['env'])
            self.assertFalse((machine.parent / 'component-receipts/corefonts.sha256').exists())
            self.assertFalse((machine.parent / 'component-receipts/corefonts.json').exists())

    def test_backend_deadline_stops_only_owned_machine_server_before_restore(self):
        with tempfile.TemporaryDirectory(prefix='wex-deadline-') as scratch:
            machine = Path(scratch) / 'machine'
            machine.mkdir()
            events = []
            from contextlib import contextmanager
            @contextmanager
            def protected(*args):
                events.append('snapshot')
                yield
                events.append('restore')
            def run(command, **kwargs):
                events.append(command)
                self.assertEqual(kwargs['env']['WINEPREFIX'], str(machine))
                return subprocess.CompletedProcess(command, 124 if '/usr/bin/timeout' in command else 0)
            with with_enabled_component('corefonts'), \
                    patch.object(components, 'validate_winetricks_integrity'), \
                    patch.object(components, 'validate_recipe_files'), \
                    patch.object(components, 'MACHINE', machine), \
                    patch.object(components, 'CACHE', Path(scratch) / 'cache'), \
                    patch.object(components.os, 'getuid', return_value=0), \
                    patch.object(components, 'license_text', return_value=('terms', 'sha')), \
                    patch.object(components, 'protect_builtins', side_effect=protected), \
                    patch.object(components.subprocess, 'run', side_effect=run):
                with self.assertRaisesRegex(ValueError, 'exit 124'):
                    components.install('corefonts', 'sha')
            self.assertLess(events.index(['/usr/local/bin/wineserver', '-k']), events.index('restore'))
            self.assertEqual(events[-2:], [['/usr/local/bin/wineserver', '-w'], 'restore'])
            self.assertFalse((Path(scratch) / 'component-receipts/corefonts.json').exists())

    def test_backend_success_creates_structured_json_and_sha_receipt(self):
        with tempfile.TemporaryDirectory(prefix='wex-backend-success-') as scratch:
            machine = Path(scratch) / 'machine'
            machine.mkdir()
            cache = Path(scratch) / 'cache'

            with with_enabled_component('corefonts'), \
                    patch.object(components, 'validate_winetricks_integrity'), \
                    patch.object(components, 'validate_recipe_files'), \
                    patch.object(components, 'MACHINE', machine), patch.object(components, 'CACHE', cache), \
                    patch.object(components.os, 'getuid', return_value=0), \
                    patch.object(components, 'license_text', return_value=('vendor terms', 'test-sha')), \
                    patch.object(components, 'protect_builtins', return_value=nullcontext()), \
                    patch.object(components.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)):
                components.install('corefonts', 'test-sha')

            receipt_sha = machine.parent / 'component-receipts/corefonts.sha256'
            receipt_json = machine.parent / 'component-receipts/corefonts.json'
            self.assertTrue(receipt_sha.is_file())
            self.assertEqual(receipt_sha.read_text().strip(), 'test-sha')
            self.assertTrue(receipt_json.is_file())
            data = json.loads(receipt_json.read_text(encoding='utf-8'))
            self.assertEqual(data['verb'], 'corefonts')
            self.assertEqual(data['sha256'], 'test-sha')
            self.assertEqual(data['winetricks_version'], components.APPROVED_WINETRICKS_VERSION)
            self.assertIn('installed_at', data)

    def test_backend_duplicate_install_idempotent(self):
        with tempfile.TemporaryDirectory(prefix='wex-backend-dup-') as scratch:
            machine = Path(scratch) / 'machine'
            machine.mkdir()
            receipts = machine.parent / 'component-receipts'
            receipts.mkdir(parents=True)
            (receipts / 'corefonts.sha256').write_text('test-sha\n')
            (receipts / 'corefonts.json').write_text(json.dumps({
                'verb': 'corefonts', 'terms_sha256': 'test-sha',
                'winetricks_sha256': components.APPROVED_WINETRICKS_SHA256,
                'winetricks_version': components.APPROVED_WINETRICKS_VERSION,
                'architecture': 'wow64',
                'recipe_files': components.COMPONENTS_METADATA['corefonts']['recipe_files'],
            }))

            with with_enabled_component('corefonts'), \
                    patch.object(components, 'validate_winetricks_integrity'), \
                    patch.object(components, 'validate_recipe_files'), \
                    patch.object(components, 'MACHINE', machine), \
                    patch.object(components.os, 'getuid', return_value=0), \
                    patch.object(components, 'license_text', return_value=('vendor terms', 'test-sha')), \
                    patch.object(components.subprocess, 'run') as run:
                components.install('corefonts', 'test-sha')
                run.assert_not_called()


class ReceiptIdentityTests(unittest.TestCase):
    def test_legacy_hash_is_not_current_installation(self):
        with tempfile.TemporaryDirectory(prefix='wex-legacy-receipt-') as scratch:
            machine = Path(scratch) / 'machine'
            receipts = machine.parent / 'component-receipts'
            receipts.mkdir()
            (receipts / 'corefonts.sha256').write_text('a' * 64)
            self.assertFalse(components.is_installed('corefonts', 'a' * 64, machine))

    def test_changed_recipe_or_architecture_invalidates_receipt(self):
        with tempfile.TemporaryDirectory(prefix='wex-receipt-identity-') as scratch:
            machine = Path(scratch) / 'machine'
            receipts = machine.parent / 'component-receipts'
            receipts.mkdir()
            record = {
                'verb': 'corefonts', 'terms_sha256': 'a' * 64,
                'winetricks_sha256': components.APPROVED_WINETRICKS_SHA256,
                'winetricks_version': components.APPROVED_WINETRICKS_VERSION,
                'architecture': 'wow64',
                'recipe_files': components.COMPONENTS_METADATA['corefonts']['recipe_files'],
            }
            target = receipts / 'corefonts.json'
            target.write_text(json.dumps(record))
            self.assertTrue(components.is_installed('corefonts', 'a' * 64, machine))
            for key, value in (('recipe_files', []), ('architecture', 'win32'),
                               ('winetricks_version', 'changed')):
                target.write_text(json.dumps(dict(record, **{key: value})))
                self.assertFalse(components.is_installed('corefonts', 'a' * 64, machine), key)


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


class OobeCoordinatorTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix='wex-oobe-test-')
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        self.sock_path = self.root / 'coordinator.sock'
        self.stage_file = self.root / 'stage'
        self.stage_file.write_text('oobe\n', encoding='ascii')
        self.addCleanup(patch.stopall)
        patch.object(components, 'OPERATION_STATE', self.root / 'operation-state').start()
        patch.dict(os.environ, {
            'WEX_OOBE_SOCKET': str(self.sock_path),
            'WEX_OOBE_STAGE_FILE': str(self.stage_file),
        }).start()
        patch.object(components.os, 'getuid', return_value=0).start()
        patch.object(components.pwd, 'getpwnam', return_value=unittest.mock.Mock(pw_uid=os.getuid())).start()
        self.coord_thread = None

    def tearDown(self):
        if self.stage_file.exists():
            self.stage_file.write_text('done\n', encoding='ascii')
        if self.sock_path.exists():
            try:
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as s:
                    s.settimeout(0.5)
                    s.connect(str(self.sock_path))
            except OSError:
                pass
        if self.coord_thread and self.coord_thread.is_alive():
            self.coord_thread.join(timeout=3)

    def start_coordinator(self):
        self.coord_thread = threading.Thread(target=components.oobe_coordinator, daemon=True)
        self.coord_thread.start()
        # bind creates the path before listen; existence is not readiness.
        for _ in range(100):
            try:
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as probe:
                    probe.settimeout(0.1)
                    probe.connect(str(self.sock_path))
                return
            except OSError:
                time.sleep(0.01)
        self.fail('Coordinator did not start listening')

    def send_request(self, command_str):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(3.0)
            connected = False
            for _ in range(30):
                try:
                    sock.connect(str(self.sock_path))
                    connected = True
                    break
                except OSError:
                    time.sleep(0.05)
            if not connected:
                sock.connect(str(self.sock_path))
            sock.sendall((command_str.strip() + '\n').encode('utf-8'))
            response = b''
            while True:
                try:
                    chunk = sock.recv(4096)
                    if not chunk:
                        break
                    response += chunk
                except (socket.timeout, ConnectionResetError):
                    break
            return response.decode('utf-8')

    def test_coordinator_requires_exact_command_arity(self):
        self.start_coordinator()
        self.assertIn('ERR', self.send_request('GET_COMPONENTS extra'))
        self.assertIn('ERR', self.send_request('START_INSTALL corefonts sha extra'))
        self.assertIn('ERR', self.send_request('QUERY_INSTALL missing extra'))

    def test_stage_symlink_and_writable_stage_are_not_authority(self):
        self.assertTrue(components.active_oobe_stage(self.stage_file))
        self.stage_file.chmod(0o666)
        self.assertFalse(components.active_oobe_stage(self.stage_file))
        self.stage_file.chmod(0o644)
        link = self.root / 'stage-link'
        link.symlink_to(self.stage_file)
        self.assertFalse(components.active_oobe_stage(link))

    def test_restart_preserves_unknown_outcome_and_rejects_blind_retry(self):
        state = components.OPERATION_STATE
        state.mkdir(mode=0o700)
        (state / 'operations.json').write_text(json.dumps({
            'op_corefonts_fixture': {'verb': 'corefonts', 'sha': 'sha',
                                     'status': 'RUNNING', 'error': '', 'cancelled': False}
        }))
        (state / 'operations.json').chmod(0o600)
        with with_enabled_component('corefonts'), \
                patch.object(components, 'license_text', return_value=('terms', 'sha')), \
                patch.object(components, 'is_installed', return_value=False), \
                patch.object(components, 'install') as install:
            self.start_coordinator()
            self.assertIn('STATUS UNKNOWN', self.send_request('QUERY_INSTALL op_corefonts_fixture'))
            self.assertIn('reconciliation', self.send_request('START_INSTALL corefonts sha'))
            install.assert_not_called()
        record = json.loads((state / 'operations.json').read_text())
        self.assertEqual(record['op_corefonts_fixture']['status'], 'UNKNOWN')

    def test_shutdown_waits_for_active_worker_without_false_failure(self):
        entered, release = threading.Event(), threading.Event()
        self.addCleanup(release.set)
        def install(verb, digest):
            entered.set()
            release.wait(10)
        with with_enabled_component('corefonts'), \
                patch.object(components, 'license_text', return_value=('terms', 'sha')), \
                patch.object(components, 'is_installed', return_value=False), \
                patch.object(components, 'install', side_effect=install):
            self.start_coordinator()
            operation = self.send_request('START_INSTALL corefonts sha').split()[1]
            self.assertTrue(entered.wait(1))
            self.stage_file.write_text('done')
            # The endpoint may already be revoked. Wake accept if still open,
            # without treating successful revocation as a connection failure.
            try:
                with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as wake:
                    wake.settimeout(0.2)
                    wake.connect(str(self.sock_path))
            except OSError:
                pass
            for _ in range(250):
                if not self.sock_path.exists():
                    break
                time.sleep(0.01)
            self.assertFalse(self.sock_path.exists())
            self.assertTrue(self.coord_thread.is_alive())
            journal = components.OPERATION_STATE / 'operations.json'
            self.assertEqual(json.loads(journal.read_text())[operation]['status'], 'RUNNING')
            release.set()
            self.coord_thread.join(2)
            self.assertFalse(self.coord_thread.is_alive())
            self.assertEqual(json.loads(journal.read_text())[operation]['status'], 'SUCCESS')

    def test_coordinator_requires_root(self):
        patch.stopall()
        with patch.object(components.os, 'getuid', return_value=1000):
            with self.assertRaises(PermissionError):
                components.oobe_coordinator()

    def test_coordinator_fails_closed_when_wexsetup_missing(self):
        with patch.object(components.pwd, 'getpwnam', side_effect=KeyError('wexsetup')):
            with self.assertRaises(RuntimeError) as ctx:
                components.oobe_coordinator()
            self.assertIn('wexsetup', str(ctx.exception))

    def test_coordinator_requires_active_oobe_stage(self):
        self.stage_file.write_text('finished\n', encoding='ascii')
        with self.assertRaises(PermissionError):
            components.oobe_coordinator()

    def test_get_components_returns_metadata_without_network(self):
        self.start_coordinator()
        with patch('urllib.request.urlopen') as net_mock:
            resp = self.send_request('GET_COMPONENTS')
            net_mock.assert_not_called()
        lines = resp.strip().split('\n')
        self.assertEqual(lines[0], 'OK')
        verbs = [line.split('\t')[0] for line in lines[1:] if line]
        for expected in ('vcrun2022', 'corefonts', 'tahoma', 'd3dcompiler_47', 'xact', 'physx'):
            self.assertIn(expected, verbs)

    def test_get_terms_retrieves_vendor_terms_and_sha256(self):
        self.start_coordinator()
        fake_text = "Standard Microsoft License Terms " * 20
        fake_sha = "abcdef0123456789" * 4
        with with_enabled_component('corefonts'), \
                patch.object(components, 'license_text', return_value=(fake_text, fake_sha)):
            resp = self.send_request('GET_TERMS corefonts')
            first_line = resp.split('\n', 1)[0]
            self.assertTrue(first_line.startswith(f'OK {fake_sha}'))
            self.assertIn(fake_text, resp)

    def test_get_terms_offline_failure_returns_clean_error(self):
        self.start_coordinator()
        with with_enabled_component('corefonts'), \
                patch.object(components, 'license_text', side_effect=ValueError('offline')), \
                patch.object(components, 'fetch_vendor_terms', side_effect=OSError('Network unreachable')):
            resp = self.send_request('GET_TERMS corefonts')
            self.assertTrue(resp.startswith('ERR'))
            self.assertIn('Network unreachable', resp)

    def test_get_terms_rejects_disabled_component(self):
        self.start_coordinator()
        resp = self.send_request('GET_TERMS physx')
        self.assertTrue(resp.startswith('ERR'))
        self.assertIn('disabled', resp)
        resp2 = self.send_request('GET_TERMS corefonts')
        self.assertTrue(resp2.startswith('ERR'))
        self.assertIn('disabled', resp2)

    def test_install_rejects_disabled_component(self):
        self.start_coordinator()
        resp = self.send_request('START_INSTALL corefonts aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa')
        self.assertTrue(resp.startswith('ERR'))
        self.assertIn('disabled', resp)

    def test_install_rejects_terms_mismatch(self):
        self.start_coordinator()
        with with_enabled_component('corefonts'), \
                patch.object(components, 'license_text', return_value=('license terms', 'actual_sha256')):
            resp = self.send_request('START_INSTALL corefonts wrong_sha256')
            self.assertTrue(resp.startswith('ERR'))
            self.assertIn('terms changed', resp)

    def test_install_rejects_expired_stage_after_connection(self):
        self.start_coordinator()
        fake_sha = 'a' * 64
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock:
            sock.settimeout(3.0)
            connected = False
            for _ in range(30):
                try:
                    sock.connect(str(self.sock_path))
                    connected = True
                    break
                except OSError:
                    time.sleep(0.05)
            if not connected:
                sock.connect(str(self.sock_path))
            # Invalidate stage file after connection is established
            self.stage_file.write_text('finished\n', encoding='ascii')
            try:
                sock.sendall(f'START_INSTALL corefonts {fake_sha}\n'.encode('utf-8'))
                resp = sock.recv(1024).decode('utf-8')
                self.assertTrue(resp.startswith('ERR'))
                self.assertIn('expired', resp)
            except (ConnectionResetError, BrokenPipeError):
                # Coordinator stopped and closed socket immediately upon stage expiration
                pass

    def test_coordinator_dispatches_install_and_lifecycle(self):
        self.start_coordinator()
        fake_sha = 'a' * 64
        with with_enabled_component('corefonts'), \
                patch.object(components, 'license_text', return_value=('license terms', fake_sha)), \
                patch.object(components, 'install') as install_mock:
            resp = self.send_request(f'START_INSTALL corefonts {fake_sha}')
            self.assertTrue(resp.startswith('OK op_corefonts_'))
            self.assertIn('STARTED', resp)
            op_id = resp.strip().split()[1]

            status_resp = ''
            for _ in range(50):
                status_resp = self.send_request(f'QUERY_INSTALL {op_id}')
                if status_resp.strip() == 'STATUS SUCCESS':
                    break
                time.sleep(0.05)
            self.assertEqual(status_resp.strip(), 'STATUS SUCCESS')
            install_mock.assert_called_once_with('corefonts', fake_sha)

    def test_coordinator_long_running_install_polling_no_timeout(self):
        self.start_coordinator()
        fake_sha = 'c' * 64
        def slow_install(v, s):
            time.sleep(0.3)

        with with_enabled_component('corefonts'), \
                patch.object(components, 'license_text', return_value=('terms', fake_sha)), \
                patch.object(components, 'install', side_effect=slow_install):
            resp = self.send_request(f'START_INSTALL corefonts {fake_sha}')
            op_id = resp.strip().split()[1]

            for _ in range(50):
                q = self.send_request(f'QUERY_INSTALL {op_id}')
                if q.strip() == 'STATUS SUCCESS':
                    break
                time.sleep(0.05)
            self.assertEqual(q.strip(), 'STATUS SUCCESS')

    def test_coordinator_deduplicates_running_install(self):
        self.start_coordinator()
        fake_sha = 'd' * 64
        evt = threading.Event()
        def blocking_install(v, s):
            evt.wait(timeout=5)

        with with_enabled_component('corefonts'), \
                patch.object(components, 'license_text', return_value=('terms', fake_sha)), \
                patch.object(components, 'install', side_effect=blocking_install) as installer:
            resp1 = self.send_request(f'START_INSTALL corefonts {fake_sha}')
            op_id1 = resp1.strip().split()[1]

            resp2 = self.send_request(f'START_INSTALL corefonts {fake_sha}')
            self.assertTrue(resp2.startswith('OK'))
            self.assertIn(op_id1, resp2)
            evt.set()

    def test_coordinator_idempotent_when_already_installed(self):
        self.start_coordinator()
        fake_sha = 'e' * 64
        with with_enabled_component('corefonts'), \
                patch.object(components, 'is_installed', return_value=True), \
                patch.object(components, 'license_text', return_value=('terms', fake_sha)):
            resp = self.send_request(f'START_INSTALL corefonts {fake_sha}')
            self.assertEqual(resp.strip(), 'OK ALREADY_INSTALLED')

    def test_coordinator_cancel_install(self):
        self.start_coordinator()
        fake_sha = 'f' * 64
        evt = threading.Event()
        def blocking_install(v, s):
            evt.wait(timeout=5)

        with with_enabled_component('corefonts'), \
                patch.object(components, 'license_text', return_value=('terms', fake_sha)), \
                patch.object(components, 'install', side_effect=blocking_install) as installer:
            resp = self.send_request(f'START_INSTALL corefonts {fake_sha}')
            op_id = resp.strip().split()[1]

            cancel_resp = self.send_request(f'CANCEL_INSTALL {op_id}')
            self.assertTrue(cancel_resp.startswith('OK DEFERRED'))

            query_resp = self.send_request(f'QUERY_INSTALL {op_id}')
            self.assertEqual(query_resp.strip(), 'STATUS RUNNING')
            duplicate = self.send_request(f'START_INSTALL corefonts {fake_sha}')
            self.assertIn(op_id, duplicate)
            evt.set()
            for _ in range(50):
                if self.send_request(f'QUERY_INSTALL {op_id}').strip() == 'STATUS SUCCESS':
                    break
                time.sleep(0.01)
            installer.assert_called_once_with('corefonts', fake_sha)

    def test_coordinator_reconnect_query(self):
        self.start_coordinator()
        fake_sha = '1' * 64
        with with_enabled_component('corefonts'), \
                patch.object(components, 'license_text', return_value=('terms', fake_sha)), \
                patch.object(components, 'install'):
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock1:
                sock1.connect(str(self.sock_path))
                sock1.sendall(f'START_INSTALL corefonts {fake_sha}\n'.encode('utf-8'))
                resp1 = sock1.recv(1024).decode('utf-8')
            op_id = resp1.strip().split()[1]

            time.sleep(0.05)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as sock2:
                sock2.connect(str(self.sock_path))
                sock2.sendall(f'QUERY_INSTALL {op_id}\n'.encode('utf-8'))
                resp2 = sock2.recv(1024).decode('utf-8')
            self.assertEqual(resp2.strip(), 'STATUS SUCCESS')

    def test_install_creates_receipt_on_success(self):
        fake_sha = 'b' * 64
        with tempfile.TemporaryDirectory(prefix='wex-receipt-test-') as test_dir:
            test_path = Path(test_dir)
            machine_dir = test_path / 'machine-prefix'
            machine_dir.mkdir(parents=True, exist_ok=True)
            cache_dir = test_path / 'cache'
            cache_dir.mkdir(parents=True, exist_ok=True)

            with with_enabled_component('corefonts'), \
                 patch.object(components, 'validate_winetricks_integrity'), \
                 patch.object(components, 'validate_recipe_files'), \
                 patch.object(components, 'MACHINE', machine_dir), \
                 patch.object(components, 'CACHE', cache_dir), \
                 patch.object(components, 'license_text', return_value=('dummy terms', fake_sha)), \
                 patch.object(components, 'protect_builtins', return_value=nullcontext()), \
                 patch.object(components.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)):
                components.install('corefonts', fake_sha)
                receipt_file = test_path / 'component-receipts' / 'corefonts.sha256'
                self.assertTrue(receipt_file.is_file(), 'Receipt file was not created')
                self.assertEqual(receipt_file.read_text(encoding='ascii').strip(), fake_sha)
                receipt_json = test_path / 'component-receipts' / 'corefonts.json'
                self.assertTrue(receipt_json.is_file(), 'Receipt json was not created')
                data = json.loads(receipt_json.read_text(encoding='utf-8'))
                self.assertEqual(data['verb'], 'corefonts')
                self.assertEqual(data['sha256'], fake_sha)

    def test_coordinator_rejects_unauthorized_uid(self):
        with patch.object(components.pwd, 'getpwnam', return_value=unittest.mock.Mock(pw_uid=1000)), \
             patch.object(components.struct, 'unpack', return_value=(1234, 1001, 1001)):
            self.start_coordinator()
            resp = self.send_request('GET_COMPONENTS')
            self.assertEqual(resp, '')


class CatalogParserTests(unittest.TestCase):
    def test_catalog_contains_six_distinct_components_in_order(self):
        expected_verbs = ['vcrun2022', 'corefonts', 'tahoma', 'd3dcompiler_47', 'xact', 'physx']
        actual_verbs = list(components.COMPONENTS_METADATA.keys())
        self.assertEqual(actual_verbs, expected_verbs)

    def test_all_components_have_explicit_reasons(self):
        for verb, meta in components.COMPONENTS_METADATA.items():
            if verb == 'vcrun2022':
                self.assertFalse(meta.get('disabled'), f"{verb} must be enabled for release candidate")
                self.assertEqual(meta.get('disabled_reason', ''), '')
            else:
                self.assertTrue(meta.get('disabled'), f"{verb} must be disabled")
                self.assertTrue(len(meta.get('disabled_reason', '').strip()) > 0, f"{verb} must have disabled_reason")

    def test_catalog_row_formatting(self):
        for verb, meta in components.COMPONENTS_METADATA.items():
            reb = '1' if meta['reboot'] else '0'
            dis = '1' if meta.get('disabled') else '0'
            dis_reason = meta.get('disabled_reason', '')
            row = f"{verb}\t{meta['title']}\t{meta['publisher']}\t{meta['description']}\t{meta['size']}\t{reb}\t0\t{dis}\t{dis_reason}"
            parts = row.split('\t')
            self.assertEqual(len(parts), 9)
            self.assertEqual(parts[0], verb)
            self.assertTrue(len(parts[1]) > 0)


class NativeSeamTests(unittest.TestCase):
    def test_native_parser_and_worker_lifecycle(self):
        native_source = ROOT / 'scripts/test_oobe_native.c'
        self.assertTrue(native_source.is_file())
        compiler = shutil.which('x86_64-w64-mingw32-gcc') or shutil.which('i686-w64-mingw32-gcc')
        if not compiler:
            self.skipTest('MinGW-w64 compiler not found in environment')

        with tempfile.TemporaryDirectory(prefix='wex-native-test-') as scratch:
            binary = Path(scratch) / 'test_oobe_native.exe'
            res = subprocess.run([compiler, '-Wall', '-Wextra', str(native_source), '-o', str(binary)],
                                 capture_output=True, text=True)
            self.assertEqual(res.returncode, 0, f'Compilation failed:\n{res.stderr}')

            wine = shutil.which('wine')
            if not wine:
                self.skipTest('Compiled native seam, but Wine is unavailable for execution')

            env = os.environ.copy()
            env['WINEDLLOVERRIDES'] = 'mscoree,mshtml=;winemenubuilder.exe=d'
            env['WINEDEBUG'] = '-all'
            env['WINEPREFIX'] = str(Path(scratch) / 'isolated-prefix')
            env['WINEARCH'] = 'win64'
            run_res = subprocess.run([wine, str(binary)], capture_output=True, text=True, env=env, timeout=90)
            self.assertEqual(run_res.returncode, 0, f'Native test run failed:\n{run_res.stdout}\n{run_res.stderr}')
            self.assertIn('ALL 6 NATIVE TESTS PASSED (54 assertions verified)', run_res.stdout)


if __name__ == '__main__':
    unittest.main(verbosity=2)
