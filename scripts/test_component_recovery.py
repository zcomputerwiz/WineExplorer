#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Production reconciliation guards in private fixtures; no real machine writes."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('components', Path(__file__).with_name('wex-components.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        if os.getuid() != 0:
            self.skipTest('Root-owned state checks require isolated root fixture')
        temp = tempfile.TemporaryDirectory(prefix='wex-recovery-test-')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.machine = self.root / 'machine'
        self.machine.mkdir()
        self.state = self.root / 'operations'
        self.state.mkdir(mode=0o700)
        self.op = 'op_vcrun2022_fixture'
        self.journal = self.state / 'operations.json'
        self.journal.write_text(json.dumps({self.op: {'verb': 'vcrun2022', 'sha': 'a' * 64,
                                                     'status': 'UNKNOWN', 'error': 'interrupted'}}))
        self.journal.chmod(0o600)
        self.addCleanup(patch.stopall)
        patch.object(c, 'MACHINE', self.machine).start()
        patch.object(c, 'OPERATION_STATE', self.state).start()
        self.idle = patch.object(c.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)).start()

    def reconcile(self):
        return c.reconcile_operation(self.op, 'Inspected fixture; no live installer or protected-state marker')

    def test_production_cli_dispatches_real_reconciliation(self):
        with patch.object(sys, 'argv', ['wex-components', '--reconcile-operation', self.op,
                                       '--reconcile-action', 'FAILED', '--reconcile-note', 'Inspected synthetic fixture']):
            c.main()
        record = json.loads(self.journal.read_text())[self.op]
        self.assertEqual(record['status'], 'FAILED')
        self.assertEqual(record['reconciliation']['uid'], 0)
        self.assertEqual(self.journal.stat().st_mode & 0o777, 0o600)
        self.assertEqual(self.idle.call_args.kwargs['env']['WINEPREFIX'], str(self.machine))

    def test_live_coordinator_blocks_reconciliation(self):
        with c.coordinator_lease(), self.assertRaisesRegex(ValueError, 'Stop the OOBE coordinator'):
            self.reconcile()
        self.idle.assert_not_called()

    def test_live_machine_blocks_reconciliation(self):
        self.idle.side_effect = subprocess.TimeoutExpired('owned wineserver -w', 2)
        with self.assertRaises(subprocess.TimeoutExpired):
            self.reconcile()
        self.assertEqual(json.loads(self.journal.read_text())[self.op]['status'], 'UNKNOWN')

    def test_protected_recovery_marker_is_not_deleted_or_bypassed(self):
        marker = self.root / 'component-recovery-required'
        marker.write_text('keep protected snapshot')
        with self.assertRaisesRegex(ValueError, 'Protected-state recovery'):
            self.reconcile()
        self.assertEqual(marker.read_text(), 'keep protected snapshot')
        self.idle.assert_not_called()

    def test_unprivileged_and_missing_inspection_refused(self):
        with patch.object(c.os, 'getuid', return_value=1000), self.assertRaises(PermissionError):
            self.reconcile()
        with self.assertRaisesRegex(ValueError, 'inspection note'):
            c.reconcile_operation(self.op, '')

    def test_untrusted_journal_and_invalid_operation_refused(self):
        self.journal.chmod(0o666)
        with self.assertRaises(PermissionError):
            self.reconcile()
        with self.assertRaisesRegex(ValueError, 'Invalid operation ID'):
            c.reconcile_operation('../anything', 'Inspected synthetic fixture')

    def test_completed_or_missing_operation_not_rewritten(self):
        self.reconcile()
        before = self.journal.read_bytes()
        with self.assertRaisesRegex(ValueError, 'Only an interrupted'):
            self.reconcile()
        self.assertEqual(self.journal.read_bytes(), before)

    def test_no_fake_success_action(self):
        with patch.object(sys, 'argv', ['wex-components', '--reconcile-operation', self.op,
                                       '--reconcile-action', 'SUCCESS', '--reconcile-note', 'Not a real installation']):
            with self.assertRaises(SystemExit) as error:
                c.main()
        self.assertEqual(error.exception.code, 2)

    def test_install_later_cannot_bypass_unknown_oobe_record(self):
        with patch.object(c, 'validate_winetricks_integrity'), patch.object(c, 'validate_recipe_files'), \
                patch.object(c, 'license_text', return_value=('terms', 'sha')), \
                patch.object(c, 'is_installed', return_value=True):
            with self.assertRaisesRegex(ValueError, 'reconciliation before install later'):
                c.install('vcrun2022', 'sha')
        self.idle.assert_not_called()


class RegeditBootstrapTests(unittest.TestCase):
    def setUp(self):
        if os.getuid() != 0:
            self.skipTest('Requires private root-owned /run fixture')
        temp = tempfile.TemporaryDirectory(prefix='wex-regedit-test-', dir='/run')
        self.addCleanup(temp.cleanup)
        self.root = Path(temp.name)
        self.machine, self.libraries = self.root / 'machine', self.root / 'libraries'
        self.targets, self.sources = [], []
        for directory, arch, machine in (('system32', 'x86_64', 0x8664), ('syswow64', 'i386', 0x14c)):
            source = self.libraries / (arch + '-windows') / 'regedit.exe'
            target = self.machine / 'drive_c/windows' / directory / 'regedit.exe'
            source.parent.mkdir(parents=True)
            target.parent.mkdir(parents=True)
            data = bytearray(256)
            data[:2] = b'MZ'
            struct.pack_into('<I', data, 0x3c, 128)
            data[128:132] = b'PE\0\0'
            struct.pack_into('<H', data, 132, machine)
            source.write_bytes(data)
            self.targets.append(target)
            self.sources.append(source)

    def repair(self):
        c.ensure_installer_regedit(self.machine, self.libraries)

    def test_missing_and_empty_get_correct_architecture_links(self):
        self.targets[1].touch()
        self.repair()
        self.assertEqual([t.resolve() for t in self.targets], self.sources)

    def test_legacy_64_bit_link_repaired_only_for_32_bit_target(self):
        old = self.machine / 'drive_c/windows/regedit.exe'
        old.write_bytes(self.sources[0].read_bytes())
        for target in self.targets:
            target.symlink_to('../regedit.exe')
        self.repair()
        self.assertEqual(os.readlink(self.targets[0]), '../regedit.exe')
        self.assertEqual(self.targets[1].resolve(), self.sources[1])
        self.assertEqual(old.read_bytes(), self.sources[0].read_bytes())

    def test_nonempty_invalid_file_not_silently_destroyed(self):
        self.targets[1].write_bytes(b'unknown installed application')
        with self.assertRaisesRegex(ValueError, 'explicit recovery'):
            self.repair()
        self.assertFalse(self.targets[0].exists(), 'Validate all replacements before publishing')
        self.assertEqual(self.targets[1].read_bytes(), b'unknown installed application')

    def test_bad_source_architecture_or_ownership_blocks_publication(self):
        self.sources[1].write_bytes(self.sources[0].read_bytes())
        with self.assertRaisesRegex(ValueError, 'wrong PE architecture'):
            self.repair()
        self.assertTrue(all(not t.exists() for t in self.targets))
        self.sources[1].chmod(0o666)
        with self.assertRaisesRegex(ValueError, 'Untrusted installed'):
            self.repair()

    def test_existing_correct_images_preserved(self):
        for target, source in zip(self.targets, self.sources):
            target.write_bytes(source.read_bytes())
        before = [t.stat().st_ino for t in self.targets]
        self.repair()
        self.assertEqual([t.stat().st_ino for t in self.targets], before)


if __name__ == '__main__':
    unittest.main()
