#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
"""MachineWine (wex-components) and wex-maint-wineserver: component installs never run Wine as root when there is a
session 0 (docs/KNOWN_BUGS.md B11, B34)."""
import importlib.machinery
import importlib.util
import os
from pathlib import Path
import pwd
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

HERE = Path(__file__).resolve().parent


def load(name, filename):
    loader = importlib.machinery.SourceFileLoader(name, str(HERE / filename))
    spec = importlib.util.spec_from_loader(name, loader)
    module = importlib.util.module_from_spec(spec)
    loader.exec_module(module)
    return module


c = load('wex_components_machine_wine', 'wex-components.py')
shim = load('wex_maint_wineserver', 'wex-maint-wineserver.py')
ACCOUNT = pwd.getpwnam('nobody')


class Fixture(unittest.TestCase):
    def setUp(self):
        # /run: the run directory's parents must be root's and not writable by others
        temp = tempfile.TemporaryDirectory(prefix='wex-machine-wine-', dir='/run' if os.getuid() == 0 else None)
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        os.chmod(self.root, 0o755)
        self.machine = self.root / 'machine'
        self.machine.mkdir()
        self.conf = self.root / 'sysrun.conf'
        self.addCleanup(patch.stopall)

    def reader(self, values=None, status=0):
        """A stand-in for wex-is-admin --print-value: VALUES (a dict) answered, others absent (3); STATUS for all."""
        values = values or {}
        script = self.root / 'reader'
        cases = ''.join(f'    {key}) echo "{value}"; exit 0 ;;\n' for key, value in values.items())
        script.write_text('#!/bin/sh\n[ "$1" = --print-value ] || exit 2\n'
                          + (f'exit {status}\n' if status else '') + 'case "$2" in\n' + cases + '    *) exit 3 ;;\nesac\n')
        script.chmod(0o755)
        patch.object(c, 'SYSRUN_CONF_READER', str(script)).start()
        self.conf.write_text('present\n')
        patch.object(c, 'SYSRUN_CONF', self.conf).start()

    def session0(self):
        socket_path = self.root / 'session0'
        self.reader({'session0_socket': str(socket_path), 'session0_user': 'nobody'})
        return socket_path


class SysrunValueTests(Fixture):
    def test_absent_file_needs_no_reader(self):
        patch.object(c, 'SYSRUN_CONF', self.root / 'missing.conf').start()
        with patch.object(c.subprocess, 'run', side_effect=AssertionError('no reader for an absent file')):
            self.assertIsNone(c.sysrun_value('session0_socket'))

    def test_value_absent_and_invalid(self):
        self.reader({'session0_user': 'wexsys'})
        self.assertEqual(c.sysrun_value('session0_user'), 'wexsys')
        self.assertIsNone(c.sysrun_value('session0_socket'))
        self.reader(status=1)
        with self.assertRaisesRegex(ValueError, 'invalid or unprotected'):
            c.sysrun_value('session0_socket')

    def test_without_session0_root_runs_wine_as_before(self):
        self.reader({'group': 'wexsys'})
        wine = c.MachineWine(self.machine)
        self.assertFalse(wine.session0)
        self.assertEqual(wine.command(['/usr/bin/xvfb-run', '-a', 'x'], timeout=1800, isolate=True),
                         ['/usr/bin/unshare', '--net', '/usr/bin/timeout', '--signal=TERM', '--kill-after=10s', '1800s',
                          '/usr/bin/xvfb-run', '-a', 'x'])
        self.assertEqual(wine.environment()['WINESERVER'], '/usr/local/bin/wineserver')
        self.assertEqual(wine.environment()['HOME'], '/root')


@unittest.skipUnless(os.getuid() == 0, 'session 0 mode hands directories to another account')
class Session0Tests(Fixture):
    def test_programs_run_as_the_account_in_a_unit(self):
        self.session0()
        with c.MachineWine(self.machine) as wine:
            command = wine.command(['/usr/local/bin/wine', 'reg.exe', 'query', 'HKLM'], timeout=90, isolate=True)
            self.assertEqual(command[0], '/usr/bin/systemd-run')
            self.assertIn(f'--uid={ACCOUNT.pw_uid}', command)
            self.assertIn(f'--gid={ACCOUNT.pw_gid}', command)
            self.assertIn('NoNewPrivileges=yes', command)
            self.assertIn('PrivateNetwork=yes', command)
            self.assertNotIn('PrivateNetwork=yes', wine.command(['x'], timeout=90))
            inner = command[command.index('--') + 1:]
            self.assertEqual(inner[:4], ['/usr/bin/timeout', '--signal=TERM', '--kill-after=10s', '90s'])
            settings = dict(arg[len('--setenv='):].split('=', 1) for arg in command if arg.startswith('--setenv='))
            self.assertEqual(settings['WINEPREFIX'], str(self.machine))
            self.assertEqual(settings['WINESERVER'], c.MAINTENANCE_WINESERVER)
            self.assertEqual(settings['USER'], 'nobody')
            self.assertTrue(settings['HOME'].startswith(str(self.machine.parent / 'component-runs')))
            self.assertNotIn('/root', ' '.join(command))
            home = Path(settings['HOME'])
            self.assertEqual((home.stat().st_uid, stat.S_IMODE(home.stat().st_mode)), (ACCOUNT.pw_uid, 0o700))
            run_dir = wine.run_dir
            self.assertEqual((run_dir.stat().st_uid, stat.S_IMODE(run_dir.stat().st_mode)), (0, 0o711))
        self.assertFalse(run_dir.exists())

    def test_session0_must_be_serving_and_no_earlier_install_running(self):
        socket_path = self.session0()
        wine = c.MachineWine(self.machine)
        with self.assertRaisesRegex(ValueError, 'Session 0 is not running'):
            wine.require_idle()
        import socket
        listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.addCleanup(listener.close)
        listener.bind(str(socket_path))
        listed = subprocess.CompletedProcess([], 0, stdout='wex-components-0123456789ab.service loaded active running\n')
        with patch.object(c.subprocess, 'run', return_value=listed):
            with self.assertRaisesRegex(ValueError, 'still running: wex-components-0123456789ab.service'):
                wine.require_idle()
        with patch.object(c.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0, stdout='')):
            wine.require_idle()

    def test_root_account_refused(self):
        self.reader({'session0_socket': '/run/s0', 'session0_user': 'root'})
        with self.assertRaisesRegex(ValueError, 'must not run as root'):
            c.MachineWine(self.machine)

    def test_stage_copies_for_the_account_and_never_follows_links(self):
        self.session0()
        cache = self.root / 'cache'
        (cache / 'vcrun2022').mkdir(parents=True)
        (cache / 'vcrun2022' / 'payload.exe').write_bytes(b'MZ payload')
        os.chmod(cache / 'vcrun2022' / 'payload.exe', 0o600)
        secret = self.root / 'secret'
        secret.write_text('root only')
        secret.chmod(0o600)
        (cache / 'vcrun2022' / 'link').symlink_to(secret)
        script = cache / 'approved-vcrun2022.sh'
        script.write_text('#!/bin/sh\n')
        script.chmod(0o700)
        with c.MachineWine(self.machine) as wine:
            staged = wine.stage(cache / 'vcrun2022', script)
            self.assertEqual(Path(wine.environment()['W_CACHE']), staged)
            self.assertEqual(staged.parent, wine.run_dir)
            for path in (staged, staged / 'vcrun2022', staged / 'vcrun2022/payload.exe', staged / 'approved-vcrun2022.sh'):
                self.assertEqual(path.lstat().st_uid, ACCOUNT.pw_uid, path)
            self.assertEqual((staged / 'vcrun2022/payload.exe').read_bytes(), b'MZ payload')
            self.assertTrue((staged / 'vcrun2022/link').is_symlink())
            self.assertEqual(os.readlink(staged / 'vcrun2022/link'), str(secret))
            self.assertEqual(stat.S_IMODE((staged / 'approved-vcrun2022.sh').stat().st_mode), 0o700)
            # a second stage is a new directory: nothing the account put in the first is in root's way
            self.assertNotEqual(wine.stage(script), staged)
        self.assertEqual((secret.read_text(), secret.stat().st_uid), ('root only', 0))
        self.assertEqual((cache / 'vcrun2022/payload.exe').stat().st_uid, 0)

    def test_export_is_copied_by_root_without_following_a_link(self):
        self.session0()
        secret = self.root / 'secret'
        secret.write_text('root only')
        secret.chmod(0o600)
        with c.MachineWine(self.machine) as wine:
            def account_exports(*args, **kwargs):
                target = Path(args[2])
                target.write_text('REGEDIT4\n')
                return subprocess.CompletedProcess(args, 0)
            with patch.object(wine, 'reg', side_effect=account_exports):
                wine.export_key('HKLM\\Software\\Wine\\DllOverrides', self.root / 'backup.reg')
            self.assertEqual((self.root / 'backup.reg').read_text(), 'REGEDIT4\n')

            def account_plants_a_link(*args, **kwargs):
                Path(args[2]).symlink_to(secret)
                return subprocess.CompletedProcess(args, 0)
            with patch.object(wine, 'reg', side_effect=account_plants_a_link):
                with self.assertRaisesRegex(ValueError, 'Cannot open export.reg'):
                    wine.export_key('HKLM\\Software\\Wine\\DllOverrides', self.root / 'backup2.reg')
            self.assertFalse((self.root / 'backup2.reg').exists())

    def test_import_hands_a_copy_to_the_account(self):
        self.session0()
        backup = self.root / 'backup.reg'
        backup.write_text('REGEDIT4\n')
        backup.chmod(0o600)
        with c.MachineWine(self.machine) as wine, patch.object(wine, 'reg') as reg:
            wine.import_file(backup)
            imported = Path(reg.call_args.args[1])
            self.assertNotEqual(imported, backup)
            self.assertEqual((imported.read_text(), imported.stat().st_uid), ('REGEDIT4\n', ACCOUNT.pw_uid))

    def test_stop_never_touches_session0(self):
        self.session0()
        with c.MachineWine(self.machine) as wine, \
                patch.object(c.subprocess, 'run', side_effect=AssertionError('no wineserver -k on session 0')):
            wine.stop()


class HiveSaveTests(unittest.TestCase):
    def test_wait_for_newer_wakes_on_the_rename(self):
        with tempfile.TemporaryDirectory() as directory:
            hive = Path(directory) / 'system.reg'
            hive.write_text('old')
            since = time.time_ns()

            def save():
                time.sleep(0.3)
                temporary = Path(directory) / 'reg1234'
                temporary.write_text('new')
                os.replace(temporary, hive)
            saver = threading.Thread(target=save)
            saver.start()
            started = time.monotonic()
            self.assertTrue(c.wait_for_newer(directory, 'system.reg', since, 10))
            self.assertLess(time.monotonic() - started, 5)
            saver.join()
            self.assertFalse(c.wait_for_newer(directory, 'system.reg', time.time_ns() + 10**9, 0.3))


class MaintenanceWineserverTests(unittest.TestCase):
    def setUp(self):
        temp = tempfile.TemporaryDirectory()
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.bin = self.root / 'wine-bin'
        self.bin.mkdir()
        shutil.copy2(shutil.which('sleep'), self.bin / 'wine-preloader')
        shutil.copy2(shutil.which('sleep'), self.bin / 'not-wine')
        self.cgroup = self.root / 'cgroup'
        self.cgroup.mkdir()
        patch.object(shim, 'WINE_DIRECTORIES', (str(self.bin) + '/',)).start()
        self.addCleanup(patch.stopall)

    def start(self, name, seconds):
        process = subprocess.Popen([str(self.bin / name), str(seconds)])
        self.addCleanup(process.wait)
        self.addCleanup(lambda: process.poll() is None and process.kill())
        return process

    def procs(self, *processes):
        (self.cgroup / 'cgroup.procs').write_text(''.join(f'{p.pid}\n' for p in processes) + f'{os.getpid()}\n')

    def test_wait_returns_when_the_wine_programs_are_gone(self):
        wine, other = self.start('wine-preloader', 0.5), self.start('not-wine', 30)
        self.procs(wine, other)
        started = time.monotonic()
        self.assertEqual(shim.wait_for(str(self.cgroup), shim.ancestors()), 0)
        self.assertLess(time.monotonic() - started, 10)
        wine.wait()
        self.assertIsNone(other.poll(), 'a program that is not Wine is not waited on or ended')

    def test_kill_ends_only_the_cgroups_wine_programs(self):
        wine, other = self.start('wine-preloader', 60), self.start('wine-preloader', 60)
        self.procs(wine)
        self.assertEqual(shim.wait_for(str(self.cgroup), shim.ancestors(), kill=True), 0)
        self.assertEqual(wine.wait(timeout=10), -9)
        self.assertIsNone(other.poll(), 'outside the cgroup: never signalled')

    def test_other_options_do_nothing(self):
        with patch.object(shim, 'wait_for', side_effect=AssertionError('nothing to wait for')):
            self.assertEqual(shim.main(['-p']), 0)
            self.assertEqual(shim.main(['-d1']), 0)


if __name__ == '__main__':
    unittest.main()
