#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""The older components (vcrun2022, corefonts, d3dcompiler_47, webview2, the builtin protection and the regedit repair)
against a machine group that swaps names while root works: planted links, links in the place of a temporary, restored
or font file, FIFOs in the place of a verified file. Nothing outside the machine root may be read into it, changed or
waited on. Synthetic fixtures under /run (the installed Wine's directories must be root's up to /); no Wine, no
network. Run as root."""
import hashlib
import importlib.util
import os
from pathlib import Path
import signal
import stat
import struct
import subprocess
import tempfile
import threading
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('components', Path(__file__).with_name('wex-components.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


def pe(machine, size=256):
    data = bytearray(size)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 128)
    data[128:132] = b'PE\0\0'
    struct.pack_into('<H', data, 132, machine)
    return bytes(data)


class FakeWine:
    """MachineWine without Wine: no override keys exist, the hive root does."""
    file_owner = 0
    def reg(self, operation, key, *args, **kwargs):
        return subprocess.CompletedProcess([], 0 if key.endswith('\\Software') else 1)

    def export_key(self, key, backup):
        raise AssertionError('no override key exists to export')

    def import_file(self, path):
        raise AssertionError('no override key existed to import')


class Timeout(Exception):
    pass


class MachineLinkTests(unittest.TestCase):
    def setUp(self):
        if os.getuid() != 0:
            self.skipTest('needs a root-owned machine root and installed-Wine fixture')
        temp = tempfile.TemporaryDirectory(prefix='wex-machine-links-', dir='/run')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.machine = self.root / 'machine'
        self.windows = self.machine / 'drive_c/windows'
        for directory in ('system32', 'syswow64', 'Fonts'):
            (self.windows / directory).mkdir(parents=True)
        for directory in (self.machine, self.machine / 'drive_c', self.windows, self.windows / 'system32',
                          self.windows / 'syswow64', self.windows / 'Fonts'):
            os.chmod(directory, 0o2775)                     # as setup-machine-root.sh leaves them
        self.libraries = self.root / 'lib-wine'
        for arch in ('x86_64-windows', 'i386-windows'):
            (self.libraries / arch).mkdir(parents=True)
        self.addCleanup(patch.stopall)
        patch.object(c, 'machine_group_gid', lambda: self.machine.stat().st_gid).start()
        # a hang is reported as a failure, not waited on forever
        previous = signal.signal(signal.SIGALRM, self.alarm)
        self.addCleanup(signal.signal, signal.SIGALRM, previous)
        self.addCleanup(signal.alarm, 0)
        signal.alarm(30)

    @staticmethod
    def alarm(signum, frame):
        raise Timeout('blocked (a FIFO was opened or read)')

    def outside(self):
        """A root-only file outside the machine root: (path, content, mode)."""
        secret = self.root / 'outside' / 'secret'
        secret.parent.mkdir(exist_ok=True)
        secret.write_text('root only')
        secret.chmod(0o600)
        return secret

    def assertUntouched(self, path, text='root only', mode=0o600):
        info = path.lstat()
        self.assertTrue(stat.S_ISREG(info.st_mode))
        self.assertEqual((path.read_text(), stat.S_IMODE(info.st_mode), info.st_uid, info.st_gid), (text, mode, 0, 0))

    def assertNoCopyOf(self, directory, content):
        for entry in directory.iterdir():
            if entry.is_file() and not entry.is_symlink():
                self.assertNotEqual(entry.read_bytes(), content, entry)

    # ---- protect_builtins: detached links ----

    def test_planted_dll_link_to_a_root_only_file_is_refused_and_not_copied(self):
        secret = self.outside()
        system32 = self.windows / 'system32'
        for target in (secret, Path('/dev/zero'), Path('/etc/shadow')):
            with self.subTest(target=str(target)):
                link = system32 / 'evil.dll'
                link.symlink_to(target)
                with self.assertRaisesRegex(ValueError, 'Unexpected builtin DLL symlink'):
                    with c.protect_builtins(self.machine, FakeWine(), self.libraries):
                        self.fail('the installer must not run')
                self.assertTrue(link.is_symlink())
                self.assertEqual(os.readlink(link), str(target))
                self.assertNoCopyOf(system32, b'root only')
                link.unlink()
        self.assertUntouched(secret)

    def test_link_out_of_the_libraries_through_dotdot_is_refused(self):
        secret = self.outside()
        (self.windows / 'syswow64/evil.dll').symlink_to(self.libraries / 'i386-windows/../../outside/secret')
        with self.assertRaisesRegex(ValueError, 'Unexpected builtin DLL symlink'):
            with c.protect_builtins(self.machine, FakeWine(), self.libraries):
                self.fail('the installer must not run')
        self.assertNoCopyOf(self.windows / 'syswow64', b'root only')
        self.assertUntouched(secret)

    def test_a_good_link_with_a_bad_one_detaches_nothing(self):
        good = self.libraries / 'x86_64-windows/good.dll'
        good.write_bytes(pe(0x8664))
        (self.windows / 'system32/good.dll').symlink_to(good)
        (self.windows / 'system32/zzz-bad.dll').symlink_to(self.outside())
        with self.assertRaises(ValueError):
            with c.protect_builtins(self.machine, FakeWine(), self.libraries):
                self.fail('the installer must not run')
        self.assertTrue((self.windows / 'system32/good.dll').is_symlink())   # every link is checked before any is replaced

    def test_builtin_link_is_detached_and_put_back(self):
        good = self.libraries / 'x86_64-windows/good.dll'
        good.write_bytes(pe(0x8664))
        good.chmod(0o644)
        link = self.windows / 'system32/good.dll'
        link.symlink_to('../../../../lib-wine/x86_64-windows/good.dll')          # relative, as Wine may make them
        with c.protect_builtins(self.machine, FakeWine(), self.libraries):
            info = link.lstat()
            self.assertTrue(stat.S_ISREG(info.st_mode))
            # the machine root's DLL mode, group-writable: the installer (session 0's account) may replace it
            self.assertEqual((link.read_bytes(), stat.S_IMODE(info.st_mode)), (pe(0x8664), 0o775))
            self.assertEqual((info.st_uid, info.st_gid), (0, self.machine.stat().st_gid))
            self.assertEqual(info.st_mtime_ns, good.stat().st_mtime_ns)
        self.assertEqual(os.readlink(link), '../../../../lib-wine/x86_64-windows/good.dll')
        self.assertEqual(good.read_bytes(), pe(0x8664))

    def test_detached_copy_belongs_to_the_installs_account(self):
        """With session 0 the install runs as its account: the copy is that account's, so that an installer can
        replace it and set its times (only an owner may)."""
        good = self.libraries / 'x86_64-windows/good.dll'
        good.write_bytes(pe(0x8664))
        link = self.windows / 'system32/good.dll'
        link.symlink_to(good)
        wine = FakeWine()
        wine.file_owner = 65534
        with c.protect_builtins(self.machine, wine, self.libraries):
            info = link.lstat()
            self.assertEqual((info.st_uid, info.st_gid, stat.S_IMODE(info.st_mode)),
                             (65534, self.machine.stat().st_gid, 0o775))
        self.assertTrue(link.is_symlink())

    def test_detached_copy_removed_by_the_installer_gets_its_link_back(self):
        good = self.libraries / 'i386-windows/good.dll'
        good.write_bytes(pe(0x14c))
        link = self.windows / 'syswow64/good.dll'
        link.symlink_to(good)
        with c.protect_builtins(self.machine, FakeWine(), self.libraries):
            link.unlink()                                   # a failed MSI's rollback
        self.assertEqual(os.readlink(link), str(good))

    def test_a_linked_system_directory_is_refused(self):
        secret = self.outside()
        system32 = self.windows / 'system32'
        system32.rmdir()
        system32.symlink_to(secret.parent)
        with self.assertRaises(ValueError):
            with c.protect_builtins(self.machine, FakeWine(), self.libraries):
                self.fail('the installer must not run')
        self.assertEqual(sorted(p.name for p in secret.parent.iterdir()), ['secret'])
        self.assertUntouched(secret)

    # ---- protect_builtins: snapshot and restore ----

    def test_restore_targets_swapped_for_links_leave_the_outside_alone(self):
        secret = self.outside()
        dll = self.windows / 'system32/ucrtbase.dll'
        dll.write_bytes(b'protected builtin')
        dll.chmod(0o775)
        good = self.libraries / 'x86_64-windows/good.dll'
        good.write_bytes(pe(0x8664))
        detached = self.windows / 'system32/good.dll'
        detached.symlink_to(good)
        with self.assertRaisesRegex(ValueError, 'fixture failure'):
            with c.protect_builtins(self.machine, FakeWine(), self.libraries):
                dll.unlink()
                dll.symlink_to(secret)                         # the restore target, now a link out
                (self.windows / 'system32/msxml99.dll').symlink_to(secret)   # an unexpected protected name
                detached.unlink()
                detached.symlink_to(secret)                    # the detached copy, now a link out
                raise ValueError('fixture failure')
        info = dll.lstat()
        self.assertTrue(stat.S_ISREG(info.st_mode))
        self.assertEqual((dll.read_bytes(), stat.S_IMODE(info.st_mode)), (b'protected builtin', 0o775))
        self.assertFalse((self.windows / 'system32/msxml99.dll').exists() or
                         (self.windows / 'system32/msxml99.dll').is_symlink())
        self.assertEqual(os.readlink(detached), str(secret))   # not ours any more: left as it is, not followed
        self.assertUntouched(secret)
        self.assertEqual(good.read_bytes(), pe(0x8664))

    def test_restore_over_a_fifo_or_a_link_to_a_device(self):
        kernel32 = self.windows / 'syswow64/kernel32.dll'
        kernel32.write_bytes(pe(0x14c))
        builtin = self.libraries / 'i386-windows/user32.dll'
        builtin.write_bytes(pe(0x14c, 512))
        user32 = self.windows / 'syswow64/user32.dll'
        user32.symlink_to(builtin)                             # a protected name that is a library link
        with c.protect_builtins(self.machine, FakeWine(), self.libraries):
            kernel32.unlink()
            os.mkfifo(kernel32)
            user32.unlink()
            user32.symlink_to('/dev/zero')
        self.assertEqual(kernel32.read_bytes(), pe(0x14c))
        self.assertEqual(os.readlink(user32), str(builtin))
        self.assertEqual(builtin.read_bytes(), pe(0x14c, 512))

    def test_a_fifo_in_a_protected_name_does_not_hang(self):
        os.mkfifo(self.windows / 'system32/kernel32.dll')
        with self.assertRaises(ValueError):
            with c.protect_builtins(self.machine, FakeWine(), self.libraries):
                self.fail('the installer must not run')

    # ---- publication of verified libraries (vcrun2022, d3dcompiler_47) ----

    def test_published_library_replaces_a_link_and_never_writes_through_it(self):
        secret = self.outside()
        target = self.windows / 'system32/msvcp140.dll'
        target.symlink_to(secret)
        c.publish_system_files(self.machine, [('system32', 'msvcp140.dll', pe(0x8664))])
        info = target.lstat()
        self.assertTrue(stat.S_ISREG(info.st_mode))
        self.assertEqual((target.read_bytes(), stat.S_IMODE(info.st_mode), info.st_gid),
                         (pe(0x8664), 0o775, self.machine.stat().st_gid))
        self.assertUntouched(secret)
        self.assertEqual([p.name for p in target.parent.iterdir()], ['msvcp140.dll'])   # no temporary left

    def test_publication_into_a_linked_directory_is_refused(self):
        secret = self.outside()
        syswow64 = self.windows / 'syswow64'
        syswow64.rmdir()
        syswow64.symlink_to(secret.parent)
        with self.assertRaises(ValueError):
            c.publish_system_files(self.machine, [('system32', 'a.dll', b'x'), ('syswow64', 'a.dll', b'x')])
        self.assertEqual(sorted(p.name for p in secret.parent.iterdir()), ['secret'])
        self.assertFalse((self.windows / 'system32/a.dll').exists())     # both directories checked first

    # ---- corefonts ----

    def test_font_names_swapped_for_links_leave_the_outside_alone(self):
        secret = self.outside()
        hard = self.root / 'outside' / 'hard'
        hard.write_text('root only')
        hard.chmod(0o600)
        fonts = self.windows / 'Fonts'
        (fonts / 'Arial.TTF').symlink_to(secret)
        os.link(hard, fonts / 'times.ttf')
        (fonts / 'cour.ttf').write_bytes(b'font')
        (fonts / 'cour.ttf').chmod(0o600)
        os.mkfifo(fonts / 'verdana.ttf')
        c.corefonts_publish_modes(self.machine)
        self.assertUntouched(secret)
        self.assertUntouched(hard)                             # a hard link is not changed either
        info = (fonts / 'cour.ttf').stat()
        self.assertEqual((stat.S_IMODE(info.st_mode), info.st_gid), (0o644, fonts.stat().st_gid))
        c.corefonts_drop_links(self.machine)
        self.assertFalse((fonts / 'Arial.TTF').is_symlink() or (fonts / 'Arial.TTF').exists())
        self.assertTrue((fonts / 'cour.ttf').is_file())
        self.assertUntouched(secret)

    def test_a_linked_fonts_directory_is_refused(self):
        secret = self.outside()
        fonts = self.windows / 'Fonts'
        fonts.rmdir()
        fonts.symlink_to(secret.parent)
        (secret.parent / 'arial.ttf').symlink_to(secret)
        for step in (c.corefonts_drop_links, c.corefonts_publish_modes):
            with self.subTest(step=step.__name__), self.assertRaises(ValueError):
                step(self.machine)
        self.assertTrue((secret.parent / 'arial.ttf').is_symlink())
        self.assertUntouched(secret)

    # ---- verification: FIFOs and links in verified names ----

    def test_fifo_in_place_of_a_verified_file_does_not_hang(self):
        fonts = self.windows / 'Fonts'
        manifest = {'arial.ttf': (4, hashlib.sha256(b'font').hexdigest())}
        os.mkfifo(fonts / 'arial.ttf')
        with patch.object(c, 'COREFONTS_ARTIFACTS', manifest), self.assertRaisesRegex(ValueError, 'missing'):
            c.verify_corefonts_artifacts(self.machine)
        runtime = {'x86': {'msvcp140': '0' * 64}, 'x64': {'msvcp140': '0' * 64}}
        os.mkfifo(self.windows / 'system32/msvcp140.dll')
        os.mkfifo(self.windows / 'syswow64/msvcp140.dll')
        with patch.object(c, 'RUNTIME_ARTIFACTS', runtime), self.assertRaisesRegex(ValueError, 'missing or changed'):
            c.verify_runtime_artifacts(self.machine)
        os.mkfifo(self.windows / 'syswow64/d3dcompiler_47.dll')
        with self.assertRaisesRegex(ValueError, 'missing'):
            c.verify_d3dcompiler_artifacts(self.machine)
        os.mkfifo(self.machine / 'system.reg')
        self.assertEqual(c.read_machine_hive(self.machine), {})
        meta = c.COMPONENTS_METADATA['webview2']
        base = self.machine / 'drive_c/Program Files (x86)/Microsoft/EdgeWebView/Application' / meta['release']
        base.mkdir(parents=True)
        os.mkfifo(base / 'msedgewebview2.exe')
        with self.assertRaisesRegex(ValueError, 'WebView2 runtime file missing'):
            c.verify_webview2_artifacts(self.machine)
        for name in ('msedgewebview2.exe', 'msedge.dll'):
            (base / name).unlink(missing_ok=True)
            with open(base / name, 'wb') as out:
                out.truncate(2 * 1024 * 1024)
        with self.assertRaisesRegex(ValueError, 'Machine registry missing'):   # the FIFO hive
            c.verify_webview2_artifacts(self.machine)

    def test_verification_racing_a_swap_to_a_fifo_never_blocks(self):
        """The name is swapped between a font and a FIFO while verification runs (a check then a read by path
        would block in the read): every run ends, with success or a refusal."""
        fonts = self.windows / 'Fonts'
        manifest = {'arial.ttf': (4, hashlib.sha256(b'font').hexdigest())}
        target = fonts / 'arial.ttf'
        target.write_bytes(b'font')
        stop = threading.Event()

        def swap():
            while not stop.is_set():
                os.mkfifo(fonts / '.fifo')
                os.rename(fonts / '.fifo', target)
                (fonts / '.font').write_bytes(b'font')
                os.rename(fonts / '.font', target)

        swapper = threading.Thread(target=swap)
        swapper.start()
        outcomes = set()
        try:
            with patch.object(c, 'COREFONTS_ARTIFACTS', manifest):
                for _ in range(3000):
                    try:
                        c.verify_corefonts_artifacts(self.machine)
                        outcomes.add('ok')
                    except ValueError:
                        outcomes.add('refused')
        finally:
            stop.set()
            swapper.join(10)
        self.assertFalse(swapper.is_alive())
        self.assertTrue(outcomes)

    def test_device_or_link_in_place_of_a_verified_file_is_refused(self):
        secret = self.outside()
        runtime = {'x86': {}, 'x64': {'msvcp140': hashlib.sha256(b'root only').hexdigest()}}
        target = self.windows / 'system32/msvcp140.dll'
        target.symlink_to(secret)                              # the right bytes, behind a link
        with patch.object(c, 'RUNTIME_ARTIFACTS', runtime), self.assertRaisesRegex(ValueError, 'missing or changed'):
            c.verify_runtime_artifacts(self.machine)
        target.unlink()
        target.symlink_to('/dev/zero')
        with patch.object(c, 'RUNTIME_ARTIFACTS', runtime), self.assertRaisesRegex(ValueError, 'missing or changed'):
            c.verify_runtime_artifacts(self.machine)
        (self.machine / 'system.reg').symlink_to('/dev/zero')
        self.assertEqual(c.read_machine_hive(self.machine), {})

    def test_verification_works_for_a_desktop_user(self):
        data = pe(0x8664)
        runtime = {'x86': {'msvcp140': hashlib.sha256(data).hexdigest()},
                   'x64': {'msvcp140': hashlib.sha256(data).hexdigest()}}
        with patch.object(c, 'RUNTIME_ARTIFACTS', runtime):
            c.publish_system_files(self.machine, [('system32', 'msvcp140.dll', data), ('syswow64', 'msvcp140.dll', data)])
            os.chmod(self.root, 0o755)                         # the fixture's parent; /var/lib/wineexplorer is 755
            pid = os.fork()
            if pid == 0:                                       # nobody: not root, not in the machine group
                code = 1
                try:
                    os.setgroups([])
                    os.setgid(65534)
                    os.setuid(65534)
                    c.verify_runtime_artifacts(self.machine)
                    code = 0
                finally:
                    os._exit(code)
            _, status = os.waitpid(pid, 0)
        self.assertEqual(os.waitstatus_to_exitcode(status), 0)

    # ---- regedit repair ----

    def regedit_sources(self):
        sources = {}
        for arch, machine in (('x86_64', 0x8664), ('i386', 0x14c)):
            source = self.libraries / (arch + '-windows') / 'regedit.exe'
            source.write_bytes(pe(machine))
            sources[arch] = source
        return sources

    def test_regedit_fifo_or_planted_link_does_not_hang_or_follow(self):
        sources = self.regedit_sources()
        secret = self.outside()
        target = self.windows / 'syswow64/regedit.exe'
        os.mkfifo(target)
        with self.assertRaisesRegex(ValueError, 'explicit recovery'):
            c.ensure_installer_regedit(self.machine, self.libraries)
        target.unlink()
        target.symlink_to(secret)
        with self.assertRaisesRegex(ValueError, 'unexpected invalid regedit link'):
            c.ensure_installer_regedit(self.machine, self.libraries)
        self.assertUntouched(secret)
        # the legacy link's target a FIFO: not waited on; the link is replaced by one to the installed image
        target.unlink()
        target.symlink_to('../regedit.exe')
        os.mkfifo(self.windows / 'regedit.exe')
        c.ensure_installer_regedit(self.machine, self.libraries)
        self.assertEqual(os.readlink(target), str(sources['i386']))
        self.assertEqual(os.readlink(self.windows / 'system32/regedit.exe'), str(sources['x86_64']))
        c.ensure_installer_regedit(self.machine, self.libraries)            # a second run keeps them
        self.assertEqual(os.readlink(target), str(sources['i386']))

    def test_regedit_into_a_linked_directory_is_refused(self):
        self.regedit_sources()
        secret = self.outside()
        system32 = self.windows / 'system32'
        system32.rmdir()
        system32.symlink_to(secret.parent)
        with self.assertRaises(ValueError):
            c.ensure_installer_regedit(self.machine, self.libraries)
        self.assertEqual(sorted(p.name for p in secret.parent.iterdir()), ['secret'])


if __name__ == '__main__':
    unittest.main()
