#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""UWP XAML component (uwp-xaml-extract-register-v1): synthetic archives and hives; no download, root or Wine needed."""
import copy
import hashlib
import importlib.util
import io
import os
from pathlib import Path
import stat
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location('components', Path(__file__).with_name('wex-components.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)

CLASSES = ['Windows.UI.Xaml.Application', 'Windows.UI.Xaml.Controls.Page']


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

    def test_no_winetricks_recipe(self):
        c.validate_recipe_files('uwp-xaml', self.root / 'no-winetricks')
        self.assertEqual(c.uwp_xaml_payload('uwp-xaml')['architecture'], 'x64')
        self.assertIn('uwp-xaml', c.COMPONENTS)


if __name__ == '__main__':
    unittest.main()
