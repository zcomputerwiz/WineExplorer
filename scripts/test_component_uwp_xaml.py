#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""UWP XAML component (uwp-xaml-extract-register-v1): synthetic archives and hives; no download, root or Wine needed."""
import contextlib
import copy
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import stat
import tempfile
import types
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location('components', Path(__file__).with_name('wex-components.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)

CLASSES = ['Windows.UI.Xaml.Application', 'Windows.UI.Xaml.Controls.Page']
REAL_MACHINE_GROUP_GID = c.machine_group_gid


def archive(path, members=None, classes=CLASSES, symlink=None):
    members = members if members is not None else {
        'XamlShim/XamlShim.exe': b'exe', 'XamlShim/XamlShim.dll': b'dll', 'XamlShim/hostfxr.dll': b'fxr',
        'XamlShim/LICENSE.txt': b'Apache', 'XamlShim/NOTICE.txt': b'notices', 'XamlShim/Assets/a.png': b'png'}
    with zipfile.ZipFile(path, 'w') as z:
        for name, data in members.items():
            z.writestr(name, data)
        if classes is not None:
            z.writestr('XamlShim/classes.txt', '\n'.join(classes) + '\n')
        if symlink:
            info = zipfile.ZipInfo(symlink)
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            z.writestr(info, '/etc/passwd')
    return path


class UwpXamlTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.machine = self.root / 'machine'
        (self.machine / 'drive_c/Program Files').mkdir(parents=True)
        os.chmod(self.machine / 'drive_c/Program Files', 0o755)
        patcher = patch.object(c, 'UWP_XAML_OWNER', os.getuid())
        patcher.start()
        self.addCleanup(patcher.stop)
        # the configured machine group is the test machine's own group
        patcher = patch.object(c, 'machine_group_gid', lambda: self.machine.stat().st_gid)
        patcher.start()
        self.addCleanup(patcher.stop)

    def unpack(self, path):
        return c.uwp_xaml_unpack(path, self.machine, 'x64')

    def test_unpack_publishes_by_rename(self):
        files, classes = self.unpack(archive(self.root / 'a.zip'))
        target = self.machine / c.UWP_XAML_ROOT / 'x64'
        self.assertEqual(classes, CLASSES)
        self.assertEqual(files['XamlShim.dll'], [3, hashlib.sha256(b'dll').hexdigest()])
        self.assertEqual((target / 'Assets/a.png').read_bytes(), b'png')
        self.assertEqual(sorted(p.name for p in target.parent.iterdir()), ['x64'])   # no staging left

    def test_reinstall_replaces_and_removes_old(self):
        self.unpack(archive(self.root / 'a.zip'))
        (self.machine / c.UWP_XAML_ROOT / 'x64' / 'stale.txt').write_text('old')
        self.unpack(archive(self.root / 'b.zip'))
        target = self.machine / c.UWP_XAML_ROOT / 'x64'
        self.assertFalse((target / 'stale.txt').exists())
        self.assertEqual(sorted(p.name for p in target.parent.iterdir()), ['x64'])

    def test_machine_root_modes(self):
        # setup-machine-root.sh: every directory 2775, group the machine's administrators
        program_files = self.machine / 'drive_c/Program Files'
        for path in (self.machine, self.machine / 'drive_c', program_files):
            os.chmod(path, 0o2775)
        self.unpack(archive(self.root / 'a.zip'))
        os.chmod(self.machine / c.UWP_XAML_ROOT / 'x64', 0o2775)
        self.unpack(archive(self.root / 'b.zip'))                     # a reinstall after the script ran again
        os.chmod(program_files, 0o2777)                               # writable by everybody: refused
        with self.assertRaises(ValueError):
            self.unpack(archive(self.root / 'c.zip'))
        os.chmod(program_files, 0o2775)
        real_fstat, inode = os.fstat, program_files.stat().st_ino

        def fstat(fd):
            info = real_fstat(fd)
            if info.st_ino == inode:      # the directory is group-writable for a group that is not the machine's
                return os.stat_result((info.st_mode, info.st_ino, info.st_dev, info.st_nlink, info.st_uid,
                                       info.st_gid + 1, info.st_size, 0, 0, 0))
            return info
        with patch.object(c.os, 'fstat', fstat):
            with self.assertRaises(ValueError):                      # group-writable for another group: refused
                self.unpack(archive(self.root / 'd.zip'))

    def test_machine_root_of_another_group_is_refused(self):
        # root:users or root:sudo (not migrated, or made by hand): group write there would mean desktop users may write
        with patch.object(c, 'machine_group_gid', lambda: self.machine.stat().st_gid + 1):
            with self.assertRaises(ValueError):
                self.unpack(archive(self.root / 'a.zip'))
        self.assertFalse((self.machine / c.UWP_XAML_ROOT).exists())
        with patch.dict(os.environ, {'WEX_MACHINE_GROUP': 'no-such-group-for-wex-tests'}):
            with self.assertRaises(ValueError):
                REAL_MACHINE_GROUP_GID()                              # a configured group that does not exist

    def test_new_entries_follow_the_machine_layout(self):
        self.unpack(archive(self.root / 'a.zip', {
            'XamlShim/XamlShim.exe': b'exe', 'XamlShim/XamlShim.dll': b'dll', 'XamlShim/hostfxr.dll': b'fxr',
            'XamlShim/LICENSE.txt': b'Apache', 'XamlShim/NOTICE.txt': b'notices', 'XamlShim/Assets/a.png': b'png'}))
        root = self.machine / c.UWP_XAML_ROOT / 'x64'
        for directory in (root.parent.parent, root.parent, root, root / 'Assets'):
            self.assertEqual(stat.S_IMODE(directory.stat().st_mode), 0o2775, directory)
        self.assertEqual(stat.S_IMODE((root / 'Assets/a.png').stat().st_mode), 0o664)
        self.assertEqual(stat.S_IMODE((root / 'XamlShim.dll').stat().st_mode), 0o775)

    def test_rejects_unsafe_members(self):
        for name in ('../evil', 'XamlShim/../evil', 'XamlShim/a/../../evil', 'other/file', 'XamlShim/a\\b',
                     'XamlShim/c:x', '/abs'):
            with self.subTest(name=name):
                path = archive(self.root / 'bad.zip', {'XamlShim/XamlShim.exe': b'x', name: b'x'})
                with self.assertRaises(ValueError):
                    self.unpack(path)
                self.assertFalse((self.root / 'evil').exists())
                self.assertFalse((self.machine / c.UWP_XAML_ROOT / 'x64').exists())

    def test_rejects_symlink_member(self):
        with self.assertRaises(ValueError):
            self.unpack(archive(self.root / 'l.zip', symlink='XamlShim/link'))

    def test_rejects_missing_required_file_and_bad_classes(self):
        members = {'XamlShim/XamlShim.exe': b'exe'}
        with self.assertRaises(ValueError):
            self.unpack(archive(self.root / 'm.zip', members))
        for classes in (['Windows.ApplicationModel.Activation.LaunchActivatedEventArgs'], ['Windows.UI.Xaml.'],
                        ['Windows.UI.Xaml.A;rm'], []):
            with self.subTest(classes=classes):
                with self.assertRaises(ValueError):
                    self.unpack(archive(self.root / 'c.zip', classes=classes))

    def hive(self, classes, path='C:\\\\Program Files\\\\WineExplorer\\\\Xaml\\\\x64'):
        lines = ['WINE REGISTRY Version 2', ';; All keys relative to \\\\Machine', '',
                 '[Software\\\\Wine\\\\Xaml] 1731600000', '#time=1db', '"ComponentPath"="' + path + '"', '']
        for name in classes:
            lines += ['[Software\\\\Microsoft\\\\WindowsRuntime\\\\ActivatableClassId\\\\' + name + '] 1731600000',
                      '#time=1db', '"ActivationType"=dword:00000000',
                      '"DllPath"="C:\\\\windows\\\\system32\\\\windows.ui.xaml.dll"', '"Threading"=dword:00000000', '']
        (self.machine / 'system.reg').write_text('\n'.join(lines) + '\n')

    def test_registry_text_and_hive_check(self):
        text = c.uwp_xaml_registry_text('x64', CLASSES)
        self.assertTrue(text.startswith('Windows Registry Editor Version 5.00\r\n'))
        self.assertIn('"ComponentPath"="C:\\\\Program Files\\\\WineExplorer\\\\Xaml\\\\x64"', text)
        self.assertIn('[HKEY_LOCAL_MACHINE\\Software\\Microsoft\\WindowsRuntime\\ActivatableClassId\\Windows.UI.Xaml.Application]', text)
        self.hive(CLASSES)
        self.assertTrue(c.uwp_xaml_hive_has(self.machine, 'x64', CLASSES))
        self.hive(CLASSES[:1])
        self.assertFalse(c.uwp_xaml_hive_has(self.machine, 'x64', CLASSES))
        self.hive(CLASSES, 'C:\\\\elsewhere')
        self.assertFalse(c.uwp_xaml_hive_has(self.machine, 'x64', CLASSES))

    def test_verify_detects_changes(self):
        files, classes = self.unpack(archive(self.root / 'a.zip'))
        self.hive(classes)
        data = {'payload_architecture': 'x64', 'files': files, 'classes': classes}
        c.verify_uwp_xaml(self.machine, data, full=True)
        (self.machine / c.UWP_XAML_ROOT / 'x64' / 'XamlShim.dll').write_bytes(b'DLL')   # same size, other bytes
        with self.assertRaises(ValueError):
            c.verify_uwp_xaml(self.machine, data)
        (self.machine / c.UWP_XAML_ROOT / 'x64' / 'XamlShim.dll').write_bytes(b'dll')
        (self.machine / c.UWP_XAML_ROOT / 'x64' / 'hostfxr.dll').unlink()
        with self.assertRaises(ValueError):
            c.verify_uwp_xaml(self.machine, data)

    def test_verify_does_not_hang_on_a_fifo_or_follow_a_link(self):
        import signal
        files, classes = self.unpack(archive(self.root / 'a.zip'))
        self.hive(classes)
        data = {'payload_architecture': 'x64', 'files': files, 'classes': classes}
        target = self.machine / c.UWP_XAML_ROOT / 'x64' / 'hostfxr.dll'

        def alarm(signum, frame):
            raise AssertionError('blocked (a FIFO was opened or read)')
        previous = signal.signal(signal.SIGALRM, alarm)
        self.addCleanup(signal.signal, signal.SIGALRM, previous)
        self.addCleanup(signal.alarm, 0)
        signal.alarm(30)
        target.unlink()
        os.mkfifo(target)
        with self.assertRaisesRegex(ValueError, 'missing or changed'):
            c.verify_uwp_xaml(self.machine, data)
        target.unlink()
        outside = self.root / 'outside'
        outside.write_bytes(b'x' * files['hostfxr.dll'][0])
        target.symlink_to(outside)
        with self.assertRaisesRegex(ValueError, 'missing or changed'):
            c.verify_uwp_xaml(self.machine, data)

    def test_plain_text_terms_only_for_this_policy(self):
        text = 'Apache License\n' + 'x' * 600
        data = text.encode()
        digest = hashlib.sha256(data).hexdigest()
        metadata = copy.deepcopy(c.COMPONENTS_METADATA)
        metadata['uwp-xaml'].update(terms_sha256=digest, terms_text_sha256=digest)

        class Response(io.BytesIO):
            url = metadata['uwp-xaml']['terms_url']
            def __enter__(self): return self
            def __exit__(self, *a): return False

        with patch.object(c, 'COMPONENTS_METADATA', metadata), \
                patch.object(c.urllib.request, 'urlopen', lambda *a, **k: Response(data)), \
                patch.object(c.os, 'getuid', lambda: 1000):
            got, _ = c.fetch_vendor_terms('uwp-xaml', self.root / 'terms')
            self.assertEqual(got, text)
            metadata['vcrun2022']['terms_url'] = 'https://example.invalid/terms.txt'
            metadata['vcrun2022'].update(terms_sha256=digest, terms_text_sha256=digest)
            with self.assertRaises(ValueError):
                c.fetch_vendor_terms('vcrun2022', self.root / 'terms2')
            metadata['uwp-xaml']['terms_sha256'] = '0' * 64
            with self.assertRaises(ValueError):
                c.fetch_vendor_terms('uwp-xaml', self.root / 'terms3')

    def test_rollback_removes_unpacked_files_and_keeps_going(self):
        self.unpack(archive(self.root / 'a.zip'))
        calls = []

        class FailingWine:
            def reg(self, *args, **kwargs):
                calls.append(args)
                raise OSError('wine is not available here')

        c.uwp_xaml_rollback(self.machine, 'x64', CLASSES, FailingWine())   # registry cleanup fails, directory still goes
        self.assertTrue(calls and calls[0][0] == 'delete')
        self.assertEqual(list((self.machine / c.UWP_XAML_ROOT).iterdir()), [])

    def test_previous_classes_from_receipt(self):
        receipts = self.root / 'receipts'
        receipts.mkdir()
        self.assertEqual(c.uwp_xaml_previous_classes(receipts, 'uwp-xaml'), [])
        (receipts / 'uwp-xaml.json').write_text('{"classes": ["Windows.UI.Xaml.Old"]}')
        self.assertEqual(c.uwp_xaml_previous_classes(receipts, 'uwp-xaml'), ['Windows.UI.Xaml.Old'])
        for bad in ('{"classes": ["Windows.UI.Xaml.A;rm"]}', '{"classes": "x"}', 'not json', '[1]'):
            with self.subTest(receipt=bad):
                (receipts / 'uwp-xaml.json').write_text(bad)
                self.assertEqual(c.uwp_xaml_previous_classes(receipts, 'uwp-xaml'), [])

    def run_install(self, previous, verify_error=None):
        """install_uwp_xaml over an earlier receipt listing PREVIOUS, with Wine, the download and the lock stubbed."""
        receipts = self.machine.parent / 'component-receipts'
        receipts.mkdir(exist_ok=True)
        (receipts / 'uwp-xaml.json').write_text('{"classes": ' + str(previous).replace("'", '"') + '}')
        (receipts / 'uwp-xaml.sha256').write_text('old\n')
        calls = []

        def verify(*args, **kwargs):
            if verify_error:
                raise verify_error

        class FakeWine:
            """MachineWine without Wine: reg.exe calls are recorded, the keys are missing."""
            def reg(self, *args, **kwargs):
                calls.append(args)
                return types.SimpleNamespace(returncode=1, stdout=b'')

            def import_file(self, path):
                calls.append(('import', str(path)))

            def require_idle(self):
                pass

            def wait_idle(self):
                pass

        payload = archive(self.root / 'new.zip')
        with patch.object(c, 'MACHINE', self.machine), patch.object(c, 'CACHE', self.root / 'cache'), \
                patch.object(c, '_machine_maintenance', lambda: contextlib.nullcontext(FakeWine())), \
                patch.object(c, 'license_text', lambda verb: ('terms', 'digest')), \
                patch.object(c, 'is_installed', lambda *a, **k: False), \
                patch.object(c, 'uwp_xaml_payload', lambda verb: {'architecture': 'x64', 'sha256': 'p'}), \
                patch.object(c, 'cache_uwp_xaml_payload', lambda verb, cache: payload), \
                patch.object(c.subprocess, 'run', lambda *a, **k: None), \
                patch.object(c, 'verify_uwp_xaml', verify):
            if verify_error:
                with self.assertRaises(type(verify_error)):
                    c.install_uwp_xaml('uwp-xaml', 'digest')
            else:
                c.install_uwp_xaml('uwp-xaml', 'digest')
        deleted = [a[1].rsplit('\\', 1)[1] for a in calls if a[0] == 'delete' and 'ActivatableClassId' in a[1]]
        return receipts, deleted

    @unittest.skipUnless(os.getuid() == 0, 'the cache and receipt checks want root-owned directories')
    def test_upgrade_unregisters_dropped_classes(self):
        receipts, deleted = self.run_install(CLASSES + ['Windows.UI.Xaml.Controls.Old'])
        self.assertEqual(deleted, ['Windows.UI.Xaml.Controls.Old'])
        self.assertEqual(c.uwp_xaml_previous_classes(receipts, 'uwp-xaml'), CLASSES)
        self.assertEqual((receipts / 'uwp-xaml.sha256').read_text(), 'digest\n')

    @unittest.skipUnless(os.getuid() == 0, 'the cache and receipt checks want root-owned directories')
    def test_failed_upgrade_leaves_nothing_installed(self):
        receipts, deleted = self.run_install(['Windows.UI.Xaml.Controls.Old'], verify_error=ValueError('broken'))
        self.assertEqual(sorted(deleted), sorted(CLASSES + ['Windows.UI.Xaml.Controls.Old']))
        self.assertFalse((receipts / 'uwp-xaml.json').exists())
        self.assertFalse((receipts / 'uwp-xaml.sha256').exists())
        self.assertFalse((self.machine / c.UWP_XAML_ROOT / 'x64').exists())

    def test_no_winetricks_recipe(self):
        c.validate_recipe_files('uwp-xaml', self.root / 'no-winetricks')
        self.assertEqual(c.uwp_xaml_payload('uwp-xaml')['architecture'], 'x64')
        self.assertIn('uwp-xaml', c.COMPONENTS)


if __name__ == '__main__':
    unittest.main()
