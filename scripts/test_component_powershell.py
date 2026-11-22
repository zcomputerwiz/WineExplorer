#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""PowerShell 7 component (powershell-extract-v1): synthetic archives; no download, root or Wine needed."""
import hashlib
import importlib.util
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

MODULE = {'WexComputer.psd1': b"@{ RootModule = 'WexComputer.psm1' }", 'WexComputer.psm1': b'function Add-Computer {}'}
BASE = {'pwsh.exe': b'exe', 'pwsh.dll': b'dll', 'System.Management.Automation.dll': b'sma',
        'Microsoft.PowerShell.ConsoleHost.dll': b'host', 'pwsh.runtimeconfig.json': b'{}', 'LICENSE.txt': b'MIT',
        'ThirdPartyNotices.txt': b'notices', 'Modules/CimCmdlets/CimCmdlets.psd1': b'cim',
        'en-US/pwsh.xml': b'help'}


def archive(path, members=None, symlink=None, directories=()):
    with zipfile.ZipFile(path, 'w') as z:
        for name, data in (BASE if members is None else members).items():
            z.writestr(name, data)
        for name in directories:
            z.writestr(name, b'')
        if symlink:
            info = zipfile.ZipInfo(symlink)
            info.external_attr = (stat.S_IFLNK | 0o777) << 16
            z.writestr(info, '/etc/passwd')
    return path


class PowerShellTests(unittest.TestCase):
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
        patcher = patch.object(c, 'machine_group_gid', lambda: self.machine.stat().st_gid)
        patcher.start()
        self.addCleanup(patcher.stop)
        self.target = self.machine / 'drive_c/Program Files/PowerShell/7'

    def unpack(self, path, module=MODULE):
        return c.powershell_unpack(path, self.machine, module)

    def test_unpack_publishes_by_rename_and_adds_the_module(self):
        files = self.unpack(archive(self.root / 'a.zip'))
        self.assertEqual(files['pwsh.exe'], [3, hashlib.sha256(b'exe').hexdigest()])
        self.assertEqual((self.target / 'en-US/pwsh.xml').read_bytes(), b'help')
        self.assertEqual((self.target / 'Modules/WexComputer/WexComputer.psm1').read_bytes(), MODULE['WexComputer.psm1'])
        self.assertIn('Modules/WexComputer/WexComputer.psd1', files)
        self.assertEqual(sorted(p.name for p in self.target.parent.iterdir()), ['7'])   # no staging left

    def test_without_a_module_the_component_still_installs(self):
        files = self.unpack(archive(self.root / 'a.zip'), module={})
        self.assertFalse(any(name.startswith('Modules/WexComputer/') for name in files))
        self.assertFalse((self.target / 'Modules/WexComputer').exists())

    def test_reinstall_replaces_and_removes_old(self):
        self.unpack(archive(self.root / 'a.zip'))
        (self.target / 'stale.txt').write_text('old')
        self.unpack(archive(self.root / 'b.zip'))
        self.assertFalse((self.target / 'stale.txt').exists())
        self.assertEqual(sorted(p.name for p in self.target.parent.iterdir()), ['7'])

    def test_directory_entries_are_skipped(self):
        files = self.unpack(archive(self.root / 'd.zip', directories=('Modules/PackageManagement/coreclr/',)))
        self.assertNotIn('Modules/PackageManagement/coreclr/', files)

    def test_rejects_unsafe_members(self):
        for name in ('../evil', 'a/../../evil', 'a\\b', 'c:x', '/abs', 'a b', 'Modules/WexComputer/evil.psm1'):
            with self.subTest(name=name):
                members = dict(BASE)
                members[name] = b'x'
                with self.assertRaises(ValueError):
                    self.unpack(archive(self.root / 'bad.zip', members))
                self.assertFalse((self.root / 'evil').exists())
                self.assertFalse(self.target.exists())

    def test_rejects_symlink_member(self):
        with self.assertRaises(ValueError):
            self.unpack(archive(self.root / 'l.zip', symlink='link'))
        self.assertFalse(self.target.exists())

    def test_rejects_missing_required_file(self):
        for missing in c.POWERSHELL_REQUIRED:
            with self.subTest(missing=missing):
                members = {name: data for name, data in BASE.items() if name != missing}
                with self.assertRaises(ValueError):
                    self.unpack(archive(self.root / 'm.zip', members))
                self.assertFalse(self.target.exists())

    def test_rejects_too_many_members(self):
        with patch.object(c, 'POWERSHELL_MAX_MEMBERS', 3):
            with self.assertRaises(ValueError):
                self.unpack(archive(self.root / 'many.zip'))

    def test_entries_follow_the_machine_layout(self):
        self.unpack(archive(self.root / 'a.zip'))
        for directory in (self.target.parent, self.target, self.target / 'Modules/WexComputer'):
            self.assertEqual(stat.S_IMODE(directory.stat().st_mode), 0o2775, directory)

    def receipt(self, files):
        return {'payload_architecture': 'x64', 'files': files}

    def test_verify_detects_changes(self):
        data = self.receipt(self.unpack(archive(self.root / 'a.zip')))
        c.verify_powershell(self.machine, data, full=True)
        (self.target / 'pwsh.dll').write_bytes(b'DLL')                       # same size, other bytes
        with self.assertRaises(ValueError):
            c.verify_powershell(self.machine, data)
        (self.target / 'pwsh.dll').write_bytes(b'dll')
        (self.target / 'Modules/WexComputer/WexComputer.psm1').write_bytes(b'x' * len(MODULE['WexComputer.psm1']))
        with self.assertRaises(ValueError):                                  # the module is always digest-checked
            c.verify_powershell(self.machine, data)
        (self.target / 'Modules/WexComputer/WexComputer.psm1').write_bytes(MODULE['WexComputer.psm1'])
        (self.target / 'en-US/pwsh.xml').write_bytes(b'HELP')                # only the full check reads this one
        c.verify_powershell(self.machine, data)
        with self.assertRaises(ValueError):
            c.verify_powershell(self.machine, data, full=True)
        (self.target / 'en-US/pwsh.xml').unlink()
        with self.assertRaises(ValueError):
            c.verify_powershell(self.machine, data)

    def test_verify_refuses_a_receipt_without_required_files_or_other_architecture(self):
        data = self.receipt(self.unpack(archive(self.root / 'a.zip')))
        for bad in ({'payload_architecture': 'x86', 'files': data['files']},
                    {'payload_architecture': 'x64', 'files': {'pwsh.exe': data['files']['pwsh.exe']}},
                    {'payload_architecture': 'x64', 'files': []}):
            with self.subTest(bad=bad):
                with self.assertRaises(ValueError):
                    c.verify_powershell(self.machine, bad)

    def test_verify_follows_no_link(self):
        data = self.receipt(self.unpack(archive(self.root / 'a.zip')))
        (self.target / 'pwsh.exe').unlink()
        (self.target / 'pwsh.exe').symlink_to('pwsh.dll')
        with self.assertRaises(ValueError):
            c.verify_powershell(self.machine, data)

    def test_remove_directory(self):
        self.unpack(archive(self.root / 'a.zip'))
        c.powershell_remove_directory(self.machine)
        self.assertFalse(self.target.exists())
        c.powershell_remove_directory(self.machine)                          # nothing there: fine

    def test_module_files_need_a_root_owned_directory(self):
        self.assertEqual(c.powershell_module_files(self.root / 'none'), {})
        source = self.root / 'WexComputer'
        source.mkdir()
        for name in c.POWERSHELL_MODULE_FILES:
            (source / name).write_bytes(b'x')
        os.chmod(source, 0o777)
        with self.assertRaises(ValueError):
            c.powershell_module_files(source)

    def test_catalog_entry(self):
        meta = c.COMPONENTS_METADATA['powershell']
        self.assertEqual(meta['execution_policy'], c.POWERSHELL_POLICY)
        self.assertFalse(meta['disabled'])
        payload = c.powershell_payload('powershell')
        self.assertEqual((payload['architecture'], payload['size']), ('x64', 115330228))
        self.assertTrue(payload['url'].startswith(c.POWERSHELL_REDIRECTS[0]))
        # the terms: one pinned document, and the identity of the set matches
        self.assertEqual(meta['terms_sha256'], c.terms_documents_identity(meta['terms_documents']))
        self.assertTrue(meta['terms_documents'][0]['url'].startswith(c.TERMS_DOCUMENT_HOSTS))

    def test_no_winetricks_recipe(self):
        c.validate_recipe_files('powershell')                                # returns, no recipe is read


if __name__ == '__main__':
    unittest.main()
