# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline R4 branches; no create/boot/SSH/Windows execution."""
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

import g4_capacity
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
        self.assertEqual(approved['commit'],'R4_PROPOSED_PENDING_PARENT_REVIEW')
        with self.assertRaisesRegex(RuntimeError,'Explicit9f86add0'):r3.procedure().native_source_approval()
        for name in approved['sha256']:
            old=p.sha
            with mock.patch.object(p,'sha',side_effect=lambda path:'0'*64 if Path(path)==REPO/name else old(path)):
                with self.assertRaisesRegex(RuntimeError,'R4 proposed'):p.native_source_approval()

    def test_actual_r4_branch_additional_pin_separate_from19_and_failed_pin_no_stage(self):
        with mock.patch.object(refresh,'procedure',side_effect=procedure):
            info,calls,_,error,_,_=refresh.RefreshTests.exercise(self)
            self.assertIsNone(error);self.assertEqual(len(info['observed_binary_sha256']),19)
            self.assertEqual(info['observed_additional_loader_pin']['sha256'],refresh.native_tests.cal.LOADER_SHA)
            self.assertIn(['mkdir','--',refresh.GUEST+'-stage'],calls)
            info,calls,_,error,_,_=refresh.RefreshTests.exercise(self,problem='loaderhash')
            self.assertIn('loader pre-stage hash mismatch',str(error));self.assertFalse(any(c[0] in ('mkdir','tar') for c in calls))
            self.assertIn('archive_skipped',info)
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

    def test_preserved_r3_identity_and_content_negatives_discriminate(self):
        capacity=json.loads((LAB/'capacity-maintenance-r1-20261023.preflight/preflight.json').read_text())
        old=json.loads((LAB/'native-exit-calibration-r2-20261024.host-audit/after.json').read_text())
        new=json.loads((LAB/'native-exit-calibration-r3-20261024.host-audit/after.json').read_text())
        parent=LAB/'system-capacity-r1-20261023.qcow2';failed=LAB/'system-readiness-r1-20261024.qcow2';extra=LAB/'system-native-identity-r3-20261024.qcow2'
        real_id=g4_capacity.file_id;real_open=Path.open
        for problem in ('identity','content'):
            p=procedure()
            def identity(path):
                row=real_id(path)
                if problem=='identity' and path==extra:row['inode']+=1
                return row
            def opening(path,*args,**kwargs):
                if path in (parent,failed,extra):return io.BytesIO(b'controlled')
                return real_open(path,*args,**kwargs)
            def dispatch(argv,**kwargs):
                self.assertEqual(argv[:4],['/usr/bin/qemu-img','info','--output=json','--backing-chain'])
                self.assertIn(argv[-1],(str(failed),str(extra)))
                return subprocess.CompletedProcess(argv,0,json.dumps(old['chain'] if argv[-1]==str(failed) else new['new_chain']).encode())
            digests=[
                mock.Mock(hexdigest=lambda:'c2acebb986bab6e3c6cd6bdd0c7efbe4c4d3b4f7f69ed358a7d53598ba1f9936'),
                mock.Mock(hexdigest=lambda:'da698bec9326daf991f4e30b3b7edc702b7f4dc9db76d7c923c9ba3da2f8f89d'),mock.Mock(hexdigest=lambda:'0'*64)]
            with mock.patch.object(g4_capacity,'file_id',side_effect=identity),mock.patch.object(g4_capacity,'owners',return_value={'idle':True}), \
                 mock.patch.object(p.subprocess,'run',side_effect=dispatch) as calls,mock.patch.object(Path,'open',autospec=True,side_effect=opening), \
                 mock.patch.object(p.hashlib,'sha256',side_effect=digests) as hashing:
                with self.assertRaisesRegex(RuntimeError,'Preserved R3 identity changed' if problem=='identity' else 'content changed'):p.prepare_native_isolation_storage(capacity)
            self.assertEqual(calls.call_count,0 if problem=='identity' else 2);self.assertEqual(hashing.call_count,0 if problem=='identity' else 3)


if __name__=='__main__':unittest.main()
