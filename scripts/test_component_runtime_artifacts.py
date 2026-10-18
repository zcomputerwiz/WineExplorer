#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Synthetic extraction tests; no vendor binaries, downloads, or Wine needed."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('components', Path(__file__).with_name('wex-components.py'))
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


def pe(machine):
    data = bytearray(256)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 128)
    data[128:132] = b'PE\0\0'
    struct.pack_into('<H', data, 132, machine)
    return bytes(data)


class RuntimeArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.machine = self.root / 'machine'
        self.cache = self.root / 'cache'
        (self.cache / 'vcrun2022').mkdir(parents=True)
        self.data = {'x86': pe(0x14c), 'x64': pe(0x8664)}
        self.manifest = {arch: {'msvcp140': hashlib.sha256(data).hexdigest()}
                         for arch, data in self.data.items()}
        self.payloads = []
        for arch, directory in (('x86', 'syswow64'), ('x64', 'system32')):
            (self.machine / 'drive_c/windows' / directory).mkdir(parents=True)
            name = arch + '.exe'
            (self.cache / 'vcrun2022' / name).write_bytes(arch.encode())
            self.payloads.append({'architecture': arch, 'name': name,
                                  'sha256': hashlib.sha256(arch.encode()).hexdigest()})
        self.addCleanup(patch.stopall)
        patch.object(c, 'RUNTIME_ARTIFACTS', self.manifest).start()
        meta = dict(c.COMPONENTS_METADATA)
        meta['vcrun2022'] = dict(meta['vcrun2022'], payloads=self.payloads)
        patch.object(c, 'COMPONENTS_METADATA', meta).start()
        self.omit = None
        self.calls = []

    def extract(self, command, stdout, **kwargs):
        self.calls.append(command)
        member = command[command.index('-F') + 1]
        if member not in ('a10', 'a12'):
            arch = 'x86' if member.endswith('_x86') else 'x64'
            if arch != self.omit:
                stdout.write(self.data[arch])
        return subprocess.CompletedProcess(command, 0)

    def repair(self):
        with patch.object(c.subprocess, 'run', side_effect=self.extract):
            c.repair_runtime_artifacts(self.machine, self.cache)

    def test_both_architectures_and_exact_members(self):
        self.repair()
        c.verify_runtime_artifacts(self.machine)
        self.assertEqual([cmd[3] for cmd in self.calls],
                         ['a10', 'msvcp140.dll_x86', 'a12', 'msvcp140.dll_amd64'])

    def test_zero_exit_missing_member_is_not_success(self):
        self.omit = 'x64'
        with self.assertRaisesRegex(ValueError, 'missing member'):
            self.repair()
        for target, _ in c.runtime_artifact_paths(self.machine):
            self.assertFalse(target.exists(), 'All validation must precede publication')

    def test_even_hash_approved_wrong_architecture_is_rejected(self):
        self.data['x64'] = self.data['x86']
        self.manifest['x64']['msvcp140'] = self.manifest['x86']['msvcp140']
        with self.assertRaisesRegex(ValueError, 'architecture'):
            self.repair()

    def test_destination_link_is_replaced_not_followed(self):
        external = self.root / 'installed-wine.dll'
        external.write_bytes(b'builtin must not change')
        target = next(c.runtime_artifact_paths(self.machine))[0]
        target.symlink_to(external)
        self.repair()
        self.assertFalse(target.is_symlink())
        self.assertEqual(external.read_bytes(), b'builtin must not change')

    def test_published_dll_keeps_shared_directory_group_policy(self):
        for target, _ in c.runtime_artifact_paths(self.machine):
            target.parent.chmod(0o2775)
        self.repair()
        for target, _ in c.runtime_artifact_paths(self.machine):
            self.assertEqual(target.stat().st_mode & 0o777, 0o775)
            self.assertEqual(target.stat().st_gid, target.parent.stat().st_gid)

    def test_changed_payload_rejected_before_extractor(self):
        (self.cache / 'vcrun2022/x86.exe').write_bytes(b'corrupt')
        with self.assertRaisesRegex(ValueError, 'package changed'):
            self.repair()
        self.assertEqual(self.calls, [])

    def test_artifact_tampering_invalidates_verification(self):
        self.repair()
        next(c.runtime_artifact_paths(self.machine))[0].write_bytes(b'wrong')
        with self.assertRaisesRegex(ValueError, 'missing or changed'):
            c.verify_runtime_artifacts(self.machine)

    def test_artifact_manifest_is_bound_to_consent(self):
        before = c.component_identity('vcrun2022')
        self.manifest['x86']['msvcp140'] = '0' * 64
        self.assertNotEqual(before, c.component_identity('vcrun2022'))

    def test_receipt_requires_current_policy_and_unchanged_artifacts(self):
        self.repair()
        receipts = self.machine.parent / 'component-receipts'
        receipts.mkdir()
        record = {
            'verb': 'vcrun2022', 'winetricks_sha256': c.APPROVED_WINETRICKS_SHA256,
            'winetricks_version': c.APPROVED_WINETRICKS_VERSION, 'architecture': 'wow64',
            'recipe_files': [list(item) for item in c.COMPONENTS_METADATA['vcrun2022']['recipe_files']],
            'component_identity': c.component_identity('vcrun2022'),
            'execution_recipe_sha256': hashlib.sha256(b'reviewed recipe').hexdigest(),
        }
        receipt = receipts / 'vcrun2022.json'
        receipt.write_text(json.dumps(record))
        with patch.object(c, 'approved_runtime_recipe', return_value=b'reviewed recipe'):
            self.assertTrue(c.is_installed('vcrun2022', machine=self.machine))
            record['component_identity'] = 'obsolete policy'
            receipt.write_text(json.dumps(record))
            self.assertFalse(c.is_installed('vcrun2022', machine=self.machine))
            record['component_identity'] = c.component_identity('vcrun2022')
            receipt.write_text(json.dumps(record))
            next(c.runtime_artifact_paths(self.machine))[0].write_bytes(b'changed')
            self.assertFalse(c.is_installed('vcrun2022', machine=self.machine))


class RuntimeDefaultsTests(unittest.TestCase):
    header = 'WINE REGISTRY Version 2\n'

    def test_missing_section_and_repeat_are_idempotent(self):
        result = c.runtime_defaults_text(self.header)
        self.assertIn(r'[Software\\Wine\\DllOverrides]', result)
        self.assertIn('"msvcp140"="native,builtin"', result)
        self.assertEqual(c.runtime_defaults_text(result), result)

    def test_user_and_application_choices_are_preserved(self):
        existing = (self.header + '\n' + r'[Software\\Wine\\DllOverrides]' + '\n'
                    '"msvcp140"="builtin"\n"*vcruntime140"=""\n\n' +
                    r'[Software\\Wine\\AppDefaults\\game.exe\\DllOverrides]' + '\n'
                    '"concrt140"="builtin"\n')
        result = c.runtime_defaults_text(existing)
        self.assertIn('"msvcp140"="builtin"', result)
        self.assertNotIn('"msvcp140"="native,builtin"', result)
        self.assertNotIn('"vcruntime140"="native,builtin"', result)
        self.assertTrue(result.endswith(r'[Software\\Wine\\AppDefaults\\game.exe\\DllOverrides]' +
                                        '\n"concrt140"="builtin"\n'))

    def test_unrelated_keys_and_values_unchanged(self):
        existing = self.header + '\n[Unrelated]\n"Value"="keep exactly"\n'
        self.assertTrue(c.runtime_defaults_text(existing).startswith(existing))

    def test_case_insensitive_existing_override(self):
        text = self.header + '\n' + r'[Software\\Wine\\DllOverrides]' + '\n"MSVCP140"="builtin"\n'
        self.assertNotIn('"msvcp140"="native,builtin"', c.runtime_defaults_text(text))

    def test_unsupported_or_ambiguous_hive_rejected(self):
        for text in ('REGEDIT4\n', self.header + '\0', self.header +
                     (r'[Software\\Wine\\DllOverrides]' + '\n') * 2):
            with self.subTest(text=text), self.assertRaises(ValueError):
                c.runtime_defaults_text(text)


if __name__ == '__main__':
    unittest.main()
