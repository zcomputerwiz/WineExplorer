# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline R4 branches; no create/boot/SSH/Windows execution."""
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import test_g4_native_calibration_refresh as refresh
import test_g4_native_identity_isolation as r3

LAB=refresh.owners.LAB;REPO=refresh.REPO;NAME='native-exit-calibration-r4-20261024'

def procedure():
    with mock.patch.dict(os.environ,dict(WEX_G4_CONTROL='native-exit-calibration',WEX_TIMING_RUN_NAME=NAME,WEX_TIMING_BOOT_NAME=NAME)):
        spec=importlib.util.spec_from_file_location('r4_private',refresh.PRIVATE)
        p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p);return p


@unittest.skipUnless(LAB.is_dir() and refresh.PRIVATE.is_file(),'private inputs unavailable')
class LoaderR4Tests(unittest.TestCase):
    def test_new_explicit_approval_and_real_old_approval_refusal(self):
        p=procedure();approved=p.native_source_approval()
        self.assertEqual(approved['policy'],'diagnostic-v1')
        with self.assertRaisesRegex(RuntimeError,'Explicit9f86add0'):r3.procedure().native_source_approval()
        self.assertTrue(any(row['warnings'] for row in approved['files'].values()))

    def test_actual_r4_branch_additional_pin_separate_from19_and_failed_pin_no_stage(self):
        with mock.patch.object(refresh,'procedure',side_effect=procedure):
            info,calls,_,error,_,_=refresh.RefreshTests.exercise(self)
            self.assertIsNone(error);self.assertEqual(len(info['observed_binary_sha256']),19)
            self.assertEqual(info['observed_additional_loader_pin']['sha256'],refresh.native_tests.cal.LOADER_SHA)
            self.assertIn(['mkdir','--',refresh.GUEST+'-stage'],calls)
            info,calls,_,error,_,_=refresh.RefreshTests.exercise(self,problem='loaderhash')
            self.assertIsNone(error);self.assertFalse(info['loader_inventory']['previous_version_tested'])
            self.assertTrue(info['loader_inventory']['warnings'])
            _,calls,_,error,_,_=refresh.RefreshTests.exercise(self,problem='loaderpin')
            self.assertIn('before SSH',str(error));self.assertEqual(calls,[])

    def test_actual_preparation_pins_json_and_fixed_guest_paths_no_dispatch(self):
        p=procedure()
        with tempfile.TemporaryDirectory(prefix='r4-prep-test-',dir=LAB) as temp:
            p.PREP=Path(temp)/'prep';p.RUN=p.BOOT=Path(temp)/NAME
            storage={'new_target_absent':True,'create_argv':['/usr/bin/qemu-img','create','-f','qcow2','-F','qcow2','-b',str(LAB/'system-capacity-r1-20261023.qcow2'),str(LAB/'system-native-identity-r4-20261024.qcow2')]}
            with mock.patch.object(p,'prepare_native_isolation_storage',return_value=storage),mock.patch.object(p,'prepare_durable_credential',return_value={'public_matches_seed':True}), \
                 mock.patch.object(p,'source_head',return_value='synthetic'),mock.patch.object(p,'windows_path',return_value='C:\\owned\\controller.ps1'), \
                 mock.patch.object(p.subprocess,'run',side_effect=AssertionError('No process dispatch')) as calls,mock.patch('builtins.print'):
                p.prepare()
            record=json.loads((p.PREP/'preflight.json').read_text());pins=json.loads((p.PREP/'native-exit-pins.json').read_text())
            self.assertEqual(len(record['source_manifest']),16);self.assertEqual(record['guest_output'],refresh.GUEST)
            self.assertEqual(record['host_run_name'],NAME);self.assertEqual(len(pins['known_binary_sha256']),19)
            self.assertEqual(pins['additional_loader_pin'],record['additional_loader_pin']);self.assertEqual(pins['original_pe_sha256'],refresh.native_tests.cal.PES)
            self.assertIn('tools/wine/wine',record['host_loader_reference']['frontend_canonical_host_path'])
            self.assertIn('NOT_OBSERVED',record['host_loader_reference']['guest_loader_hash']);calls.assert_not_called()

    def test_consumed_storage_preparation_not_selected_by_current_cli(self):
        p=procedure()
        with mock.patch.object(p,'prepare_native_isolation_storage',side_effect=AssertionError('Retired storage gate must not run')) as legacy:
            with self.assertRaisesRegex(RuntimeError,'preparation dispatcher retired'):p.main('prepare')
        legacy.assert_not_called()

if __name__=='__main__':unittest.main()
