#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""UWP application frameworks component (uwp-frameworks-extract-register-v1): synthetic framework packages in
synthetic containers (a zip and an uncompressed cabinet), through the real download cache, extraction, unpack, catalog
records, verification, upgrade, rollback and uninstall; no network and no Wine. Run as root (the cache and receipt
checks want root-owned directories); cabextract is needed for the cabinet case."""
import copy
import fcntl
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import shutil
import stat
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

spec = importlib.util.spec_from_file_location('components', Path(__file__).with_name('wex-components.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)

MS = 'CN=Microsoft Corporation, O=Microsoft Corporation, L=Redmond, S=Washington, C=US'


def appx(name, version, arch='x64', publisher=MS, framework=True, extra=None, servers=''):
    manifest = ('<?xml version="1.0" encoding="utf-8"?>'
                '<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10">'
                f'<Identity Name="{name}" ProcessorArchitecture="{arch}" Publisher="{publisher}" Version="{version}"/>'
                f'<Properties><Framework>{"true" if framework else "false"}</Framework></Properties>'
                f'<Extensions>{servers}</Extensions></Package>')
    out = io.BytesIO()
    with zipfile.ZipFile(out, 'w') as z:
        z.writestr('AppxManifest.xml', manifest)
        z.writestr('%5BContent_Types%5D.xml', '<Types/>')
        z.writestr('lib/fw%20one.dll', b'MZ' + name.encode())
        for member, data in (extra or {}).items():
            z.writestr(member, data)
    return out.getvalue()


def cabinet(name, data):
    """One file, one folder, uncompressed blocks, no checksums."""
    blocks = [data[i:i + 32768] for i in range(0, len(data), 32768)] or [b'']
    file_entry = struct.pack('<IIHHHH', len(data), 0, 0, 0x5a21, 0, 0x20) + name.encode() + b'\0'
    header_size, folder_size = 36, 8
    data_start = header_size + folder_size + len(file_entry)
    body = b''.join(struct.pack('<IHH', 0, len(b), len(b)) + b for b in blocks)
    total = data_start + len(body)
    header = struct.pack('<4sIIIIIBBHHHHH', b'MSCF', 0, total, 0, header_size + folder_size, 0, 3, 1, 1, 1, 0, 0, 0)
    folder = struct.pack('<IHH', data_start, len(blocks), 0)
    return header + folder + file_entry + body


class FrameworkTests(unittest.TestCase):
    def setUp(self):
        if os.getuid() != 0:
            self.skipTest('needs root-owned cache and receipt directories')
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.machine = self.root / 'machine'
        for d in ('drive_c/Program Files', 'drive_c/ProgramData'):
            (self.machine / d).mkdir(parents=True)
        for d in (self.machine, self.machine / 'drive_c', self.machine / 'drive_c/Program Files',
                  self.machine / 'drive_c/ProgramData'):
            os.chmod(d, 0o2775)                         # as setup-machine-root.sh leaves them
        self.cache = self.root / 'cache'
        self.cache.mkdir(mode=0o700)
        self.metadata = copy.deepcopy(c.COMPONENTS_METADATA)
        patch.object(c, 'COMPONENTS_METADATA', self.metadata).start()
        patch.object(c, 'machine_group_gid', lambda: self.machine.stat().st_gid).start()
        self.addCleanup(patch.stopall)

    def container(self, name, fmt, packages):
        """A container holding (member, appx bytes, pin overrides) items, put in the cache as if downloaded."""
        if fmt == 'zip':
            out = io.BytesIO()
            with zipfile.ZipFile(out, 'w') as z:
                for member, data, _ in packages:
                    z.writestr(member, data)
            blob = out.getvalue()
        else:
            (member, data, _), = packages
            blob = cabinet(member, data)
        (self.cache / 'uwp-frameworks').mkdir(mode=0o700, exist_ok=True)
        (self.cache / 'uwp-frameworks' / name).write_bytes(blob)
        os.chmod(self.cache / 'uwp-frameworks' / name, 0o600)
        pins = []
        for member, data, pin in packages:
            name_, version, arch = c.appx_identity(data)[:3]
            pins.append(dict({'member': member, 'size': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
                              'name': name_, 'version': version, 'architecture': arch}, **pin))
        return {'name': name, 'format': fmt, 'size': len(blob), 'sha256': hashlib.sha256(blob).hexdigest(),
                'url': 'https://api.nuget.org/v3-flatcontainer/x/' + name, 'packages': pins}

    def set_containers(self, *containers):
        self.metadata['uwp-frameworks']['containers'] = list(containers)

    def install(self):
        c.install_uwp_frameworks_into('uwp-frameworks', self.machine, self.cache, 'digest')

    def receipt(self):
        return json.loads((self.root / 'component-receipts' / 'uwp-frameworks.json').read_text())

    def test_publisher_id_and_full_name(self):
        self.assertEqual(c.appx_publisher_id(MS), '8wekyb3d8bbwe')
        self.assertEqual(c.appx_full_name('Microsoft.VCLibs.140.00', '14.0.33519.0', 'x64', MS),
                         'Microsoft.VCLibs.140.00_14.0.33519.0_x64__8wekyb3d8bbwe')

    def test_member_paths(self):
        self.assertEqual(c.appx_member_path('%5BContent_Types%5D.xml'), '[Content_Types].xml')
        self.assertEqual(c.appx_member_path('lib/fw%20one.dll'), 'lib/fw one.dll')
        for bad in ('../x', 'a/../b', 'a%2Fb', 'a%5Cb', 'a\\b', 'c:x', 'a//b', '', 'x.', 'a%ZZ', '%00'):
            with self.subTest(member=bad):
                self.assertIsNone(c.appx_member_path(bad))

    def test_install_zip_and_cabinet(self):
        xaml = appx('Microsoft.UI.Xaml.2.8', '8.2501.31001.0')
        vclibs = appx('Microsoft.VCLibs.140.00', '14.0.33519.0')
        self.set_containers(self.container('a.nupkg', 'zip', [('tools/AppX/x64/Release/x.appx', xaml, {})]),
                            self.container('b.cab', 'cab', [('Microsoft.VCLibs.x64.14.00.appx', vclibs, {})]))
        self.install()
        data = self.receipt()
        self.assertEqual(sorted(data['packages']), ['Microsoft.UI.Xaml.2.8_8.2501.31001.0_x64__8wekyb3d8bbwe',
                                                    'Microsoft.VCLibs.140.00_14.0.33519.0_x64__8wekyb3d8bbwe'])
        full = 'Microsoft.VCLibs.140.00_14.0.33519.0_x64__8wekyb3d8bbwe'
        root = self.machine / c.FRAMEWORK_STORE / full
        self.assertEqual((root / 'lib/fw one.dll').read_bytes(), b'MZMicrosoft.VCLibs.140.00')
        self.assertTrue((root / '[Content_Types].xml').is_file())
        record = (self.machine / c.FRAMEWORK_CATALOG / (full + '.wexappx')).read_bytes().decode('utf-16-le')
        self.assertEqual(record, 'WEXAPPX 1\nroot\tC:\\Program Files\\WindowsApps\\' + full +
                         '\npublisher\t' + MS + '\nframework\t1\n')
        c.verify_uwp_frameworks(self.machine, data, full=True)
        # the machine root's layout: directories 2775, files 664, .exe and .dll 775
        for directory in (self.machine / c.FRAMEWORK_STORE, root, root / 'lib', self.machine / c.FRAMEWORK_CATALOG,
                          (self.machine / c.FRAMEWORK_CATALOG).parent):
            self.assertEqual(stat.S_IMODE(directory.stat().st_mode), 0o2775, directory)
        self.assertEqual(stat.S_IMODE((root / 'AppxManifest.xml').stat().st_mode), 0o664)
        self.assertEqual(stat.S_IMODE((root / 'lib/fw one.dll').stat().st_mode), 0o775)
        self.assertEqual(stat.S_IMODE((self.machine / c.FRAMEWORK_CATALOG / (full + '.wexappx')).stat().st_mode), 0o664)
        self.assertEqual(sorted(p.name for p in (self.machine / c.FRAMEWORK_STORE).iterdir()),
                         sorted(data['packages']))              # no staging or work directories left
        self.assertEqual(sorted(p.name for p in (self.cache / 'uwp-frameworks').iterdir()), ['a.nupkg', 'b.cab'])

    def test_record_carries_the_classes_of_the_in_process_servers(self):
        servers = ('<Extension Category="windows.activatableClass.inProcessServer"><InProcessServer>'
                   '<Path>Microsoft.UI.Xaml.dll</Path>'
                   '<ActivatableClass ActivatableClassId="Microsoft.UI.Xaml.Controls.Button" ThreadingModel="both"/>'
                   '<ActivatableClass ActivatableClassId="Microsoft.UI.Xaml.Controls.Button" ThreadingModel="both"/>'
                   '<ActivatableClass ActivatableClassId="bad id with spaces" ThreadingModel="both"/>'
                   '</InProcessServer></Extension>'
                   '<Extension Category="windows.activatableClass.inProcessServer"><InProcessServer>'
                   '<Path>../escape.dll</Path>'
                   '<ActivatableClass ActivatableClassId="Escapes.Class" ThreadingModel="both"/>'
                   '</InProcessServer></Extension>'
                   '<Extension Category="windows.activatableClass.inProcessServer"><InProcessServer>'
                   '<Path>bin/Other.dll</Path>'
                   '<ActivatableClass ActivatableClassId="Other.Thing" ThreadingModel="both"/>'
                   '</InProcessServer></Extension>')
        package = appx('Microsoft.UI.Xaml.2.8', '8.2501.31001.0', servers=servers)
        self.assertEqual(c.appx_manifest_classes(zipfile.ZipFile(io.BytesIO(package)).read('AppxManifest.xml')),
                         [('Microsoft.UI.Xaml.Controls.Button', 'Microsoft.UI.Xaml.dll'),
                          ('Other.Thing', 'bin\\Other.dll')])
        self.set_containers(self.container('a.nupkg', 'zip', [('tools/AppX/x64/Release/x.appx', package, {})]))
        self.install()
        full = 'Microsoft.UI.Xaml.2.8_8.2501.31001.0_x64__8wekyb3d8bbwe'
        record = (self.machine / c.FRAMEWORK_CATALOG / (full + '.wexappx')).read_bytes().decode('utf-16-le')
        self.assertEqual(record, 'WEXAPPX 1\nroot\tC:\\Program Files\\WindowsApps\\' + full +
                         '\npublisher\t' + MS + '\nframework\t1\n'
                         'class\tMicrosoft.UI.Xaml.Controls.Button\tMicrosoft.UI.Xaml.dll\n'
                         'class\tOther.Thing\tbin\\Other.dll\n')
        c.verify_uwp_frameworks(self.machine, self.receipt(), full=True)

    def test_refuses_wrong_identity_hash_and_non_framework(self):
        good = appx('Microsoft.UI.Xaml.2.8', '8.2501.31001.0')
        cases = {
            'identity': [('x.appx', appx('Microsoft.UI.Xaml.2.8', '8.2501.31001.0', publisher='CN=Someone'), {})],
            'version pin': [('x.appx', good, {'version': '8.2501.31002.0'})],
            'hash pin': [('x.appx', good, {'sha256': '0' * 64})],
            'not a framework': [('x.appx', appx('Microsoft.UI.Xaml.2.8', '8.2501.31001.0', framework=False), {})],
            'unsafe member': [('x.appx', appx('Microsoft.UI.Xaml.2.8', '8.2501.31001.0',
                                              extra={'a/%2E%2E/b': b'x'}), {})],
        }
        for label, packages in cases.items():
            with self.subTest(case=label):
                self.set_containers(self.container(label + '.nupkg', 'zip', packages))
                with self.assertRaises(ValueError):
                    self.install()
                store = self.machine / c.FRAMEWORK_STORE
                self.assertEqual(list(store.iterdir()) if store.exists() else [], [])
                self.assertFalse((self.root / 'component-receipts' / 'uwp-frameworks.json').exists())

    def test_machine_root_of_another_group_is_refused(self):
        self.set_containers(self.container('a.nupkg', 'zip', [('x.appx', appx('A.Fw', '1.0.0.0'), {})]))
        with patch.object(c, 'machine_group_gid', lambda: self.machine.stat().st_gid + 1):
            with self.assertRaises(ValueError):
                self.install()
        self.assertFalse((self.machine / c.FRAMEWORK_STORE).exists())

    # ---- planted links: the machine root is writable by the machine group, whose members can put a link where root
    # is about to work; nothing outside the machine root may be touched ----

    FULL = 'Microsoft.UI.Xaml.2.8_8.2501.31001.0_x64__8wekyb3d8bbwe'

    def outside(self):
        """A directory outside the machine root with a file in it, and its state: (directory, file, mode, names)."""
        directory = self.root / 'outside'
        directory.mkdir(exist_ok=True)
        sentinel = directory / 'sentinel'
        sentinel.write_text('keep')
        sentinel.chmod(0o600)
        return directory, sentinel

    def assertOutsideUntouched(self, directory, sentinel):
        self.assertEqual(sorted(p.name for p in directory.iterdir()), ['sentinel'])
        self.assertEqual((stat.S_IMODE(sentinel.stat().st_mode), sentinel.read_text()), (0o600, 'keep'))
        self.assertEqual(stat.S_IMODE(directory.stat().st_mode), 0o755 & ~os.umask(0) | stat.S_IMODE(directory.stat().st_mode))

    def unpack_package(self):
        gid = self.machine.stat().st_gid
        return c.uwp_frameworks_unpack(appx('Microsoft.UI.Xaml.2.8', '8.2501.31001.0'), self.machine, self.FULL, gid)

    def test_a_linked_store_is_refused(self):
        directory, sentinel = self.outside()
        before = stat.S_IMODE(directory.stat().st_mode)
        (self.machine / 'drive_c/Program Files/WindowsApps').symlink_to(directory)
        with self.assertRaises(ValueError):
            self.unpack_package()
        self.assertOutsideUntouched(directory, sentinel)
        self.assertEqual(stat.S_IMODE(directory.stat().st_mode), before)

    def test_a_linked_program_files_is_refused(self):
        directory, sentinel = self.outside()
        program_files = self.machine / 'drive_c/Program Files'
        program_files.rmdir()
        program_files.symlink_to(directory)
        with self.assertRaises(ValueError):
            self.unpack_package()
        self.assertOutsideUntouched(directory, sentinel)

    def test_a_link_in_the_name_of_the_package_is_replaced_not_followed(self):
        directory, sentinel = self.outside()
        before = stat.S_IMODE(directory.stat().st_mode)
        self.unpack_package()
        package = self.machine / c.FRAMEWORK_STORE / self.FULL
        shutil.rmtree(package)
        package.symlink_to(directory)                       # planted where the package directory goes
        self.unpack_package()
        self.assertFalse(package.is_symlink())
        self.assertTrue((package / 'AppxManifest.xml').is_file())
        self.assertOutsideUntouched(directory, sentinel)
        self.assertEqual(stat.S_IMODE(directory.stat().st_mode), before)

    def test_a_linked_catalog_is_refused(self):
        directory, sentinel = self.outside()
        gid = self.machine.stat().st_gid
        parent = self.machine / 'drive_c/ProgramData/WineExplorer'
        parent.mkdir(parents=True)
        os.chmod(parent, 0o2775)
        (parent / 'appx-catalog').symlink_to(directory)
        with self.assertRaises(ValueError):
            c.uwp_frameworks_publish_record(self.machine, gid, self.FULL, MS, b'')
        self.assertOutsideUntouched(directory, sentinel)

    def installed_old_format(self):
        """One installed framework whose record is in the old format, with its receipt."""
        package = appx('Microsoft.UI.Xaml.2.8', '8.2501.31001.0', servers=(
            '<Extension Category="windows.activatableClass.inProcessServer"><InProcessServer><Path>X.dll</Path>'
            '<ActivatableClass ActivatableClassId="X.Y" ThreadingModel="both"/></InProcessServer></Extension>'))
        gid = self.machine.stat().st_gid
        files = c.uwp_frameworks_unpack(package, self.machine, self.FULL, gid)
        catalog = c.uwp_frameworks_catalog(self.machine, gid)
        record = catalog / (self.FULL + '.wexappx')
        record.write_bytes(c.uwp_frameworks_record(self.FULL, MS))
        receipts = self.root / 'component-receipts'
        receipts.mkdir(mode=0o755, exist_ok=True)
        (receipts / 'uwp-frameworks.json').write_text(json.dumps({
            'verb': 'uwp-frameworks', 'execution_policy': c.UWP_FRAMEWORKS_POLICY, 'publisher': MS,
            'packages': {self.FULL: files}}))
        os.chmod(receipts / 'uwp-frameworks.json', 0o644)
        return record, self.machine / c.FRAMEWORK_STORE / self.FULL

    def test_refresh_does_not_follow_links_or_wait_for_a_fifo(self):
        directory, sentinel = self.outside()
        record, package = self.installed_old_format()
        old = record.read_bytes()
        manifest = package / 'AppxManifest.xml'
        original = manifest.read_bytes()
        # the manifest swapped for a link to a file that has the same bytes: refused, nothing refreshed
        copy = directory / 'copy'
        copy.write_bytes(original)
        manifest.unlink()
        manifest.symlink_to(copy)
        refreshed, skipped = c.refresh_uwp_frameworks_records(self.machine)
        self.assertEqual((refreshed, [n for n, _ in skipped]), ([], [self.FULL]))
        self.assertEqual(record.read_bytes(), old)
        # and for a FIFO (a read of it would never return)
        manifest.unlink()
        os.mkfifo(manifest)
        refreshed, skipped = c.refresh_uwp_frameworks_records(self.machine)
        self.assertEqual((refreshed, [n for n, _ in skipped]), ([], [self.FULL]))
        # the record swapped for a link to a file outside: it is not read, not written
        manifest.unlink()
        manifest.write_bytes(original)
        manifest.chmod(0o664)
        target = directory / 'record'
        target.write_bytes(old)
        record.unlink()
        record.symlink_to(target)
        refreshed, skipped = c.refresh_uwp_frameworks_records(self.machine)
        self.assertEqual((refreshed, [n for n, _ in skipped]), ([], [self.FULL]))
        self.assertEqual(target.read_bytes(), old)
        self.assertEqual(sorted(p.name for p in directory.iterdir()), ['copy', 'record', 'sentinel'])
        self.assertEqual(stat.S_IMODE(sentinel.stat().st_mode), 0o600)

    def test_refresh_with_a_linked_store_touches_nothing_outside(self):
        directory, sentinel = self.outside()
        record, package = self.installed_old_format()
        store = package.parent
        moved = self.root / 'moved-store'
        store.rename(moved)
        store.symlink_to(moved)
        old = record.read_bytes()
        refreshed, skipped = c.refresh_uwp_frameworks_records(self.machine)
        self.assertEqual((refreshed, [n for n, _ in skipped]), ([], [self.FULL]))
        self.assertEqual(record.read_bytes(), old)

    def test_refresh_takes_the_components_lock(self):
        record, package = self.installed_old_format()
        lock = os.open(self.root / '.components.lock', os.O_CREAT | os.O_RDWR, 0o600)
        try:
            fcntl.flock(lock, fcntl.LOCK_EX)
            with self.assertRaises(OSError):                 # an install holds it: the repair stops instead of racing
                c.refresh_uwp_frameworks_records(self.machine)
        finally:
            os.close(lock)
        self.assertEqual(c.refresh_uwp_frameworks_records(self.machine)[0], [self.FULL])

    def test_remove_does_not_follow_a_linked_store(self):
        directory, sentinel = self.outside()
        victim = directory / self.FULL
        victim.mkdir()
        (victim / 'keep').write_text('keep')
        program_files = self.machine / 'drive_c/Program Files'
        (program_files / 'WindowsApps').symlink_to(directory)
        with self.assertRaises(ValueError):
            c.uwp_frameworks_remove(self.machine, [self.FULL])
        self.assertEqual((victim / 'keep').read_text(), 'keep')

    def test_corrupt_cache_is_refused(self):
        self.set_containers(self.container('a.nupkg', 'zip', [('x.appx', appx('A.Fw', '1.0.0.0'), {})]))
        (self.cache / 'uwp-frameworks' / 'a.nupkg').write_bytes(b'not the pinned file')
        with self.assertRaises(ValueError):
            self.install()

    def test_verify_upgrade_rollback_uninstall(self):
        self.set_containers(self.container('v1.nupkg', 'zip', [('x.appx', appx('A.Fw', '1.0.0.0'), {})]))
        self.install()
        v1 = 'A.Fw_1.0.0.0_x64__8wekyb3d8bbwe'
        record = self.machine / c.FRAMEWORK_CATALOG / (v1 + '.wexappx')
        record.write_bytes(record.read_bytes().replace('framework\t1'.encode('utf-16-le'), b''))
        with self.assertRaises(ValueError):
            c.verify_uwp_frameworks(self.machine, self.receipt())
        # an upgrade publishes 2.0 and takes 1.0 away
        self.set_containers(self.container('v2.nupkg', 'zip', [('x.appx', appx('A.Fw', '2.0.0.0'), {})]))
        self.install()
        v2 = 'A.Fw_2.0.0.0_x64__8wekyb3d8bbwe'
        self.assertEqual(list(self.receipt()['packages']), [v2])
        self.assertFalse((self.machine / c.FRAMEWORK_STORE / v1).exists())
        self.assertFalse(record.exists())
        # a failed upgrade leaves nothing installed and no receipt
        self.set_containers(self.container('v3.nupkg', 'zip', [('x.appx', appx('A.Fw', '3.0.0.0'), {})]))
        with patch.object(c, 'verify_uwp_frameworks', side_effect=ValueError('broken')):
            with self.assertRaises(ValueError):
                self.install()
        self.assertEqual(list((self.machine / c.FRAMEWORK_STORE).iterdir()), [])
        self.assertEqual(list((self.machine / c.FRAMEWORK_CATALOG).iterdir()), [])
        self.assertFalse((self.root / 'component-receipts' / 'uwp-frameworks.json').exists())
        # install again, then uninstall: records and directories go, the cache stays
        self.set_containers(self.container('v2.nupkg', 'zip', [('x.appx', appx('A.Fw', '2.0.0.0'), {})]))
        self.install()
        c.uninstall_uwp_frameworks('uwp-frameworks', self.machine)
        self.assertEqual(list((self.machine / c.FRAMEWORK_STORE).iterdir()), [])
        self.assertEqual(list((self.machine / c.FRAMEWORK_CATALOG).iterdir()), [])
        self.assertFalse((self.root / 'component-receipts' / 'uwp-frameworks.json').exists())

    def test_published_metadata(self):
        meta = c.COMPONENTS_METADATA['uwp-frameworks']
        pins = c.uwp_frameworks_packages('uwp-frameworks')
        self.assertEqual([p['name'] for _, p in pins],
                         ['Microsoft.UI.Xaml.2.8', 'Microsoft.NET.Native.Framework.2.2', 'Microsoft.NET.Native.Runtime.2.2',
                          'Microsoft.VCLibs.140.00', 'Microsoft.VCLibs.140.00.UWPDesktop'])
        self.assertFalse(meta['disabled'])
        self.assertEqual(c.terms_documents_identity(meta['terms_documents']), meta['terms_sha256'])
        self.assertEqual(len(meta['terms_documents']), 3)
        c.validate_recipe_files('uwp-frameworks', self.root / 'no-winetricks')
        with patch.object(c, 'TERMS', self.root / 'terms'):
            with self.assertRaises(ValueError):                  # no accepted terms, nothing downloaded
                c.install('uwp-frameworks', 'digest')
        self.assertFalse((self.cache / 'uwp-frameworks').exists())

    def serve(self, documents):
        """_terms_download over a dict url -> bytes, honouring Range like the real servers."""
        requests = []

        def download(url, headers=None, approved_hosts=None):
            data = documents[url]
            requests.append((url, (headers or {}).get('Range')))
            if headers and 'Range' in headers:
                start, end = map(int, headers['Range'][len('bytes='):].split('-'))
                return data[start:end + 1], {}
            return data, {}
        return patch.object(c, '_terms_download', download), requests

    def terms_fixture(self):
        winui = 'MICROSOFT SOFTWARE LICENSE TERMS\nWINDOWS UI LIBRARY\n' + 'w' * 600
        package = io.BytesIO()
        with zipfile.ZipFile(package, 'w', zipfile.ZIP_DEFLATED) as z:
            z.writestr('lib/big.bin', os.urandom(300000))           # most of the package is never read
            z.writestr('license.txt', winui)
        native = '﻿MICROSOFT SOFTWARE LICENSE TERMS\n.NET NATIVE\n' + 'n' * 600
        nupkg = 'https://api.nuget.org/v3-flatcontainer/x/1/x.nupkg'
        txt = 'https://raw.githubusercontent.com/Microsoft/dotnet/master/releases/UWP/LICENSE.TXT'
        files = {nupkg: package.getvalue(), txt: native.encode('utf-8')}
        documents = [
            {'title': 'WinUI', 'format': 'zip-member', 'url': nupkg, 'size': len(files[nupkg]), 'member': 'license.txt',
             'sha256': hashlib.sha256(winui.encode()).hexdigest()},
            {'title': 'Native', 'format': 'txt', 'url': txt, 'sha256': hashlib.sha256(files[txt]).hexdigest()}]
        text = ('WinUI\n=====\n\n' + winui + '\n\nNative\n======\n\n' + native[1:] + '\n')
        meta = self.metadata['uwp-frameworks']
        meta.update(terms_documents=documents, terms_sha256=c.terms_documents_identity(documents),
                    terms_text_sha256=hashlib.sha256(text.encode()).hexdigest())
        return files, documents, text

    def test_terms_documents_are_shown_in_order(self):
        files, documents, text = self.terms_fixture()
        served, requests = self.serve(files)
        with served, patch.object(c.os, 'getuid', lambda: 1000):     # not root: nothing is cached
            got, digest = c.fetch_vendor_terms('uwp-frameworks', self.root / 'terms')
        self.assertEqual(got, text)
        self.assertEqual(digest, hashlib.sha256((c.component_identity('uwp-frameworks') + '\n' + text).encode()).hexdigest())
        ranged = [r for u, r in requests if u == documents[0]['url']]
        self.assertTrue(ranged and all(ranged))                        # the package only through range requests
        read = sum(int(r.split('-')[1]) - int(r[len('bytes='):].split('-')[0]) + 1 for r in ranged)
        self.assertLess(read, len(files[documents[0]['url']]) // 2)

    def test_terms_redirect_to_another_host_is_refused(self):
        files, documents, _ = self.terms_fixture()

        class Response(io.BytesIO):
            def __init__(self, data, url):
                super().__init__(data)
                self.url, self.headers = url, {}
            def __enter__(self): return self
            def __exit__(self, *a): return False

        def urlopen(req, timeout=None):
            # an approved host that bounces the request elsewhere (same bytes, so only the location can refuse it)
            return Response(files[req.full_url], 'https://example.invalid/LICENSE.TXT')

        for document in documents:
            with self.subTest(format=document['format']):
                with patch.object(c.urllib.request, 'urlopen', urlopen):
                    with self.assertRaisesRegex(ValueError, 'unapproved location'):
                        c.terms_document_text('uwp-frameworks', document, c.TERMS_DOCUMENT_HOSTS)
        with patch.object(c.urllib.request, 'urlopen',
                          lambda req, timeout=None: Response(files[req.full_url], req.full_url)):
            c.terms_document_text('uwp-frameworks', documents[1], c.TERMS_DOCUMENT_HOSTS)   # no redirect: fine

    def test_terms_documents_refused_when_changed(self):
        meta = self.metadata['uwp-frameworks']

        def other_host(files, documents):
            documents[1]['url'] = 'https://example.invalid/LICENSE.TXT'
            files[documents[1]['url']] = b'x'
            meta['terms_sha256'] = c.terms_documents_identity(documents)

        cases = {
            'document bytes': lambda files, documents: files.update({documents[1]['url']: files[documents[1]['url']] + b' '}),
            'set identity': lambda files, documents: meta.update(terms_sha256='0' * 64),
            'text identity': lambda files, documents: meta.update(terms_text_sha256='0' * 64),
            'host': other_host,
        }
        for label, change in cases.items():
            with self.subTest(case=label):
                files, documents, _ = self.terms_fixture()
                change(files, documents)
                served, _ = self.serve(files)
                with served, patch.object(c.os, 'getuid', lambda: 1000):
                    with self.assertRaises(ValueError):
                        c.fetch_vendor_terms('uwp-frameworks', self.root / ('terms-' + label.replace(' ', '-')))


if __name__ == '__main__':
    unittest.main()
