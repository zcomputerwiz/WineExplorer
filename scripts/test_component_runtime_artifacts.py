#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Synthetic extraction tests; no vendor binaries, downloads, or Wine needed."""
import copy
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


class CorefontsArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.machine = self.root / 'machine'
        self.fonts_dir = self.machine / 'drive_c/windows/Fonts'
        self.fonts_dir.mkdir(parents=True)
        self.cache = self.root / 'cache'
        self.review_cache = self.root / 'review'
        (self.review_cache / 'corefonts').mkdir(parents=True)
        self.addCleanup(patch.stopall)

    def test_real_artifact_table_matches_inventory(self):
        inventory_file = Path(__file__).resolve().parents[1] / 'docs/evidence/oobe-corefonts-inventory-20261018.json'
        if not inventory_file.is_file():
            self.skipTest('Font inventory evidence missing')
        inventory = json.loads(inventory_file.read_text(encoding='utf-8'))
        expected_fonts = {}
        for pkg in inventory['corefonts']:
            for fname, info in pkg['fonts'].items():
                expected_fonts[fname.lower()] = (info['size'], info['sha256'])
        self.assertEqual(len(c.COREFONTS_ARTIFACTS), 30)
        self.assertEqual(c.COREFONTS_ARTIFACTS, expected_fonts)

    def test_verification_positive_and_tampering(self):
        test_manifest = {}
        for name in ('arial.ttf', 'times.ttf', 'cour.ttf'):
            content = f'font-content-for-{name}'.encode('ascii')
            target = self.fonts_dir / name
            target.write_bytes(content)
            target.chmod(0o644)
            test_manifest[name] = (len(content), hashlib.sha256(content).hexdigest())
        with patch.object(c, 'COREFONTS_ARTIFACTS', test_manifest):
            # 1. Positive: all fonts match
            c.verify_corefonts_artifacts(self.machine)

            # 2. Tampered content fails
            (self.fonts_dir / 'arial.ttf').write_bytes(b'font-content-for-arial.xyz')
            with self.assertRaisesRegex(ValueError, 'Approved core fonts artifact changed'):
                c.verify_corefonts_artifacts(self.machine)
            (self.fonts_dir / 'arial.ttf').write_bytes(b'font-content-for-arial.ttf')
            c.verify_corefonts_artifacts(self.machine)

            # 3. Wrong size fails
            (self.fonts_dir / 'times.ttf').write_bytes(b'short')
            with self.assertRaisesRegex(ValueError, 'size mismatch'):
                c.verify_corefonts_artifacts(self.machine)
            (self.fonts_dir / 'times.ttf').write_bytes(b'font-content-for-times.ttf')
            c.verify_corefonts_artifacts(self.machine)

            # 4. Missing font fails
            (self.fonts_dir / 'cour.ttf').unlink()
            with self.assertRaisesRegex(ValueError, 'missing'):
                c.verify_corefonts_artifacts(self.machine)
            (self.fonts_dir / 'cour.ttf').write_bytes(b'font-content-for-cour.ttf')
            c.verify_corefonts_artifacts(self.machine)

            # 5. Symlinked font fails
            ext = self.root / 'external.ttf'
            ext.write_bytes(b'font-content-for-arial.ttf')
            (self.fonts_dir / 'arial.ttf').unlink()
            (self.fonts_dir / 'arial.ttf').symlink_to(ext)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                c.verify_corefonts_artifacts(self.machine)
            (self.fonts_dir / 'arial.ttf').unlink()
            (self.fonts_dir / 'arial.ttf').write_bytes(b'font-content-for-arial.ttf')
            c.verify_corefonts_artifacts(self.machine)

            # 6. Unreadable font fails
            (self.fonts_dir / 'cour.ttf').chmod(0o000)
            with self.assertRaisesRegex(ValueError, 'not readable'):
                c.verify_corefonts_artifacts(self.machine)
            (self.fonts_dir / 'cour.ttf').chmod(0o644)
            c.verify_corefonts_artifacts(self.machine)

    def test_cache_corefonts_payloads_staging(self):
        packages = c.review_packages('corefonts')
        rev_core = self.review_cache / 'corefonts'
        for pkg in packages:
            dummy = b'P' * pkg['size']
            (rev_core / pkg['name']).write_bytes(dummy)

        # Hash mismatch in review cache
        with patch.object(c.os, 'getuid', return_value=0):
            with self.assertRaisesRegex(ValueError, 'integrity mismatch'):
                c.cache_corefonts_payloads(self.cache, self.review_cache)

        # Mock matching hashes
        mock_packages = []
        for pkg in packages:
            content = f"dummy-pkg-{pkg['name']}".encode()
            (rev_core / pkg['name']).write_bytes(content)
            mock_packages.append({'name': pkg['name'], 'size': len(content),
                                  'sha256': hashlib.sha256(content).hexdigest()})
        with patch.object(c, 'review_packages', return_value=mock_packages), \
             patch.object(c.os, 'getuid', return_value=0):
            c.cache_corefonts_payloads(self.cache, self.review_cache)
            for pkg in mock_packages:
                staged = self.cache / 'corefonts' / pkg['name']
                self.assertTrue(staged.is_file())
                self.assertFalse(staged.is_symlink())
                self.assertEqual(hashlib.sha256(staged.read_bytes()).hexdigest(), pkg['sha256'])
                self.assertEqual(staged.stat().st_mode & 0o777, 0o600)

            # Missing package in review cache raises ReviewRequired
            (rev_core / mock_packages[0]['name']).unlink()
            (self.cache / 'corefonts' / mock_packages[0]['name']).unlink()
            with self.assertRaises(c.ReviewRequired):
                c.cache_corefonts_payloads(self.cache, self.review_cache)

    def test_corefonts_identity_bound_to_manifest_and_receipt(self):
        before = c.component_identity('corefonts')
        test_manifest = dict(c.COREFONTS_ARTIFACTS)
        test_manifest['arial.ttf'] = (1234, '0' * 64)
        with patch.object(c, 'COREFONTS_ARTIFACTS', test_manifest):
            self.assertNotEqual(before, c.component_identity('corefonts'))

        # Receipt verification requires component_identity and artifacts
        receipts = self.machine.parent / 'component-receipts'
        receipts.mkdir()
        record = {
            'verb': 'corefonts',
            'winetricks_sha256': c.APPROVED_WINETRICKS_SHA256,
            'winetricks_version': c.APPROVED_WINETRICKS_VERSION,
            'architecture': 'wow64',
            'recipe_files': [list(item) for item in c.COMPONENTS_METADATA['corefonts']['recipe_files']],
            'component_identity': c.component_identity('corefonts'),
        }
        receipt = receipts / 'corefonts.json'
        receipt.write_text(json.dumps(record))

        with patch.object(c, 'verify_corefonts_artifacts') as mock_verify:
            self.assertTrue(c.is_installed('corefonts', machine=self.machine))
            mock_verify.assert_called_once_with(self.machine)

            record['component_identity'] = 'tampered_identity'
            receipt.write_text(json.dumps(record))
            self.assertFalse(c.is_installed('corefonts', machine=self.machine))


class D3DCompiler47ArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.machine = self.root / 'machine'
        (self.machine / 'drive_c/windows/syswow64').mkdir(parents=True)
        (self.machine / 'drive_c/windows/system32').mkdir(parents=True)
        self.cache = self.root / 'cache'
        self.review_cache = self.root / 'review'
        (self.cache / 'd3dcompiler_47').mkdir(parents=True)
        (self.review_cache / 'd3dcompiler_47').mkdir(parents=True)
        self.addCleanup(patch.stopall)

    def test_artifact_table_identities(self):
        self.assertEqual(len(c.D3DCOMPILER_47_ARTIFACTS), 2)
        x86 = c.D3DCOMPILER_47_ARTIFACTS['x86']
        x64 = c.D3DCOMPILER_47_ARTIFACTS['x64']
        self.assertEqual(x86['path'], 'syswow64/d3dcompiler_47.dll')
        self.assertEqual(x86['member'], 'fila319f706acfa16d6707473ebf29bdc7f')
        self.assertEqual(x86['machine'], 0x14c)
        self.assertEqual(x86['size'], 3466856)
        self.assertEqual(x86['sha256'], 'efbdbbcd0d954f8fdc53467de5d89ad525e4e4a9cfff8a15d07c6fdb350c407f')

        self.assertEqual(x64['path'], 'system32/d3dcompiler_47.dll')
        self.assertEqual(x64['member'], 'fil3585cb2ea5db13cc0838f8d06b5c9679')
        self.assertEqual(x64['machine'], 0x8664)
        self.assertEqual(x64['size'], 4173928)
        self.assertEqual(x64['sha256'], 'e994847e01a6f1e4cbdc5a864616ac262f67ee4f14db194984661a8d927ab7f4')

    def test_verification_positive_and_tampering(self):
        test_manifest = {
            'x86': {
                'path': 'syswow64/d3dcompiler_47.dll',
                'member': 'test_x86',
                'cab': 'test_x86.cab',
                'size': 256,
                'sha256': hashlib.sha256(pe(0x14c)).hexdigest(),
                'machine': 0x14c,
            },
            'x64': {
                'path': 'system32/d3dcompiler_47.dll',
                'member': 'test_x64',
                'cab': 'test_x64.cab',
                'size': 256,
                'sha256': hashlib.sha256(pe(0x8664)).hexdigest(),
                'machine': 0x8664,
            },
        }
        with patch.object(c, 'D3DCOMPILER_47_ARTIFACTS', test_manifest):
            p32 = self.machine / 'drive_c/windows/syswow64/d3dcompiler_47.dll'
            p64 = self.machine / 'drive_c/windows/system32/d3dcompiler_47.dll'
            p32.write_bytes(pe(0x14c))
            p64.write_bytes(pe(0x8664))
            p32.chmod(0o644)
            p64.chmod(0o644)

            # Positive
            c.verify_d3dcompiler_artifacts(self.machine)

            # Tampered hash
            p32.write_bytes(b'x' * 256)
            with self.assertRaisesRegex(ValueError, 'hash mismatch'):
                c.verify_d3dcompiler_artifacts(self.machine)
            p32.write_bytes(pe(0x14c))

            # Wrong size
            p64.write_bytes(pe(0x8664) + b'more')
            with self.assertRaisesRegex(ValueError, 'size mismatch'):
                c.verify_d3dcompiler_artifacts(self.machine)
            p64.write_bytes(pe(0x8664))

            # Missing member
            p32.unlink()
            with self.assertRaisesRegex(ValueError, 'missing'):
                c.verify_d3dcompiler_artifacts(self.machine)
            p32.write_bytes(pe(0x14c))

            # Symlink rejected
            ext = self.root / 'external.dll'
            ext.write_bytes(pe(0x14c))
            p32.unlink()
            p32.symlink_to(ext)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                c.verify_d3dcompiler_artifacts(self.machine)
            p32.unlink()
            p32.write_bytes(pe(0x14c))

            # Wrong architecture
            p32.write_bytes(pe(0x8664))
            test_manifest['x86']['sha256'] = hashlib.sha256(pe(0x8664)).hexdigest()
            with self.assertRaisesRegex(ValueError, 'architecture'):
                c.verify_d3dcompiler_artifacts(self.machine)
            p32.write_bytes(pe(0x14c))
            test_manifest['x86']['sha256'] = hashlib.sha256(pe(0x14c)).hexdigest()

            # Unreadable
            p32.chmod(0o000)
            with self.assertRaisesRegex(ValueError, 'not readable'):
                c.verify_d3dcompiler_artifacts(self.machine)
            p32.chmod(0o644)
            c.verify_d3dcompiler_artifacts(self.machine)

    def test_repair_d3dcompiler_artifacts_positive_and_isolation(self):
        test_manifest = {
            'x86': {
                'path': 'syswow64/d3dcompiler_47.dll',
                'member': 'fila319f706acfa16d6707473ebf29bdc7f',
                'cab': '2630bae9681db6a9f6722366f47d055c.cab',
                'size': 256,
                'sha256': hashlib.sha256(pe(0x14c)).hexdigest(),
                'machine': 0x14c,
            },
            'x64': {
                'path': 'system32/d3dcompiler_47.dll',
                'member': 'fil3585cb2ea5db13cc0838f8d06b5c9679',
                'cab': '61d57a7a82309cd161a854a6f4619e52.cab',
                'size': 256,
                'sha256': hashlib.sha256(pe(0x8664)).hexdigest(),
                'machine': 0x8664,
            },
        }
        cab32 = self.cache / 'd3dcompiler_47/2630bae9681db6a9f6722366f47d055c.cab'
        cab64 = self.cache / 'd3dcompiler_47/61d57a7a82309cd161a854a6f4619e52.cab'
        cab32.write_bytes(b'cab32')
        cab64.write_bytes(b'cab64')

        def fake_extract(command, stdout, **kwargs):
            member = command[command.index('-F') + 1]
            if member == test_manifest['x86']['member']:
                stdout.write(pe(0x14c))
            elif member == test_manifest['x64']['member']:
                stdout.write(pe(0x8664))
            return subprocess.CompletedProcess(command, 0)

        meta = copy.deepcopy(c.COMPONENTS_METADATA)
        meta['d3dcompiler_47']['recipe_files'] = [
            (cab32.name, hashlib.sha256(b'cab32').hexdigest()),
            (cab64.name, hashlib.sha256(b'cab64').hexdigest()),
        ]

        with patch.object(c, 'COMPONENTS_METADATA', meta), \
             patch.object(c, 'D3DCOMPILER_47_ARTIFACTS', test_manifest), \
             patch.object(c.subprocess, 'run', side_effect=fake_extract):
            c.repair_d3dcompiler_artifacts(self.machine, self.cache)
            c.verify_d3dcompiler_artifacts(self.machine)

            p32 = self.machine / 'drive_c/windows/syswow64/d3dcompiler_47.dll'
            p64 = self.machine / 'drive_c/windows/system32/d3dcompiler_47.dll'
            self.assertTrue(p32.is_file())
            self.assertTrue(p64.is_file())
            self.assertEqual(p32.read_bytes(), pe(0x14c))
            self.assertEqual(p64.read_bytes(), pe(0x8664))

    def test_identity_and_receipt_verification(self):
        before = c.component_identity('d3dcompiler_47')
        test_manifest = dict(c.D3DCOMPILER_47_ARTIFACTS)
        test_manifest['x86'] = dict(test_manifest['x86'], size=999)
        with patch.object(c, 'D3DCOMPILER_47_ARTIFACTS', test_manifest):
            self.assertNotEqual(before, c.component_identity('d3dcompiler_47'))

        receipts = self.machine.parent / 'component-receipts'
        receipts.mkdir()
        record = {
            'verb': 'd3dcompiler_47',
            'winetricks_sha256': c.APPROVED_WINETRICKS_SHA256,
            'winetricks_version': c.APPROVED_WINETRICKS_VERSION,
            'architecture': 'wow64',
            'recipe_files': [list(item) for item in c.COMPONENTS_METADATA['d3dcompiler_47']['recipe_files']],
            'component_identity': c.component_identity('d3dcompiler_47'),
            'execution_recipe_sha256': hashlib.sha256(b'd3d-recipe').hexdigest(),
        }
        receipt = receipts / 'd3dcompiler_47.json'
        receipt.write_text(json.dumps(record))

        with patch.object(c, 'approved_d3dcompiler_recipe', return_value=b'd3d-recipe'), \
             patch.object(c, 'verify_d3dcompiler_artifacts') as mock_verify:
            self.assertTrue(c.is_installed('d3dcompiler_47', machine=self.machine))
            mock_verify.assert_called_once_with(self.machine)

            record['execution_recipe_sha256'] = 'wrong'
            receipt.write_text(json.dumps(record))
            self.assertFalse(c.is_installed('d3dcompiler_47', machine=self.machine))

            record['execution_recipe_sha256'] = hashlib.sha256(b'd3d-recipe').hexdigest()
            record['component_identity'] = 'wrong'
            receipt.write_text(json.dumps(record))
            self.assertFalse(c.is_installed('d3dcompiler_47', machine=self.machine))

    def test_approved_d3dcompiler_recipe_patching(self):
        fake_winetricks = self.root / 'winetricks'
        sample = (
            'load_d3dcompiler_47()\n{\n'
            '    w_download https://raw.githubusercontent.com/mozilla/fxc2/master/dll/d3dcompiler_47_32.dll 2ad0d4987fc4624566b190e747c9d95038443956ed816abfd1e2d389b5ec0851\n'
            '    w_try_cp_dll "${W_CACHE}/d3dcompiler_47/d3dcompiler_47_32.dll" "${W_SYSTEM32_DLLS}/d3dcompiler_47.dll"\n'
            '    if [ "${W_ARCH}" = "win64" ]; then\n'
            '        w_download https://raw.githubusercontent.com/mozilla/fxc2/master/dll/d3dcompiler_47.dll 4432bbd1a390874f3f0a503d45cc48d346abc3a8c0213c289f4b615bf0ee84f3\n'
            '        w_try_cp_dll "${W_CACHE}/d3dcompiler_47/d3dcompiler_47.dll" "${W_SYSTEM64_DLLS}/d3dcompiler_47.dll"\n'
            '    fi\n'
            '    w_override_dlls native d3dcompiler_47\n}\n'
        )
        fake_winetricks.write_text(sample, encoding='utf-8')
        with patch.object(c, 'validate_winetricks_integrity'):
            patched = c.approved_d3dcompiler_recipe(fake_winetricks).decode('utf-8')
            self.assertNotIn('raw.githubusercontent.com/mozilla/fxc2', patched)
            self.assertIn('download.microsoft.com', patched)
            self.assertIn('2630bae9681db6a9f6722366f47d055c.cab', patched)
            self.assertIn('61d57a7a82309cd161a854a6f4619e52.cab', patched)
            self.assertIn('efbdbbcd0d954f8fdc53467de5d89ad525e4e4a9cfff8a15d07c6fdb350c407f d3dcompiler_47_32.dll', patched)
            self.assertIn('e994847e01a6f1e4cbdc5a864616ac262f67ee4f14db194984661a8d927ab7f4 d3dcompiler_47.dll', patched)


if __name__ == '__main__':
    unittest.main()
