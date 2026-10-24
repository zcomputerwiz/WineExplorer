# SPDX-License-Identifier: GPL-2.0-or-later
"""R3 isolation/preparation with fakes; no create/boot/SSH/native execution."""
import ast
import copy
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

LAB=refresh.owners.LAB
REPO=refresh.REPO
NAME='native-exit-calibration-r3-20261024'
NEW='system-native-identity-r3-20261024.qcow2'


def procedure():
    with mock.patch.dict(os.environ,dict(WEX_G4_CONTROL='native-exit-calibration',WEX_TIMING_RUN_NAME=NAME,WEX_TIMING_BOOT_NAME=NAME)):
        spec=importlib.util.spec_from_file_location('r3_private',refresh.PRIVATE)
        p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p);return p


@unittest.skipUnless(refresh.PRIVATE.is_file() and LAB.is_dir(),'owned private inputs unavailable')
class IsolationTests(unittest.TestCase):
    def test_actual_launcher_missing_old_only_symlink_noncanonical_before_outputs(self):
        path=REPO/'build/g4-virgl-20261022/launch.py';tree=ast.parse(path.read_text())
        fn=next(n for n in tree.body if isinstance(n,ast.FunctionDef) and n.name=='writable_overlay')
        env={'Path':Path};exec(compile(ast.Module(body=[fn],type_ignores=[]),str(path),'exec'),env)
        with tempfile.TemporaryDirectory(prefix='r3-launch-test-',dir=LAB) as temp:
            root=Path(temp)
            for old in ('system-readiness-r1-20261024.qcow2','system-capacity-r1-20261023.qcow2'):(root/old).write_bytes(b'plain fixture, not image')
            # Execute actual module prefix up to the guarded disk assignment.
            prefix=[]
            for node in tree.body:
                prefix.append(node)
                if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='disk' for t in node.targets):break
            for missing in (True,False):
                if not missing:(root/NEW).symlink_to(root/'system-readiness-r1-20261024.qcow2')
                env2={};code=compile(ast.Module(body=prefix,type_ignores=[]),str(path),'exec')
                # Replace only the fixed storage-root literal in this test AST.
                for node in ast.walk(prefix[-2]):
                    if isinstance(node,ast.Constant) and node.value=='/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022':node.value=str(root)
                code=compile(ast.Module(body=prefix,type_ignores=[]),str(path),'exec')
                with mock.patch.object(subprocess,'Popen',side_effect=AssertionError('No child')) as child, \
                     mock.patch.object(Path,'mkdir',side_effect=AssertionError('No output')) as mkdir:
                    with self.assertRaises(RuntimeError):exec(code,env2)
                child.assert_not_called();mkdir.assert_not_called()
            (root/NEW).unlink();(root/NEW).write_bytes(b'plain fixture')
            self.assertEqual(env['writable_overlay'](root),root/NEW)
            with self.assertRaises(RuntimeError):env['writable_overlay'](root/'..'/root.name)

    def test_explicit_source_approval_and_old_approval_remains_rejected(self):
        p=procedure();approved=p.native_source_approval()
        self.assertEqual(approved['commit'],'9f86add0');self.assertEqual(len(approved['sha256']),2)
        for name in approved['sha256']:
            real=p.sha
            with mock.patch.object(p,'sha',side_effect=lambda path:'0'*64 if Path(path)==REPO/name else real(path)):
                with self.assertRaisesRegex(RuntimeError,'Explicit9f86add0'):p.native_source_approval()

    def test_actual_r3_branch_only_complete_empty_then_fixed_guest_stage(self):
        with mock.patch.object(refresh,'procedure',side_effect=procedure):
            info,calls,guards,error,_,_=refresh.RefreshTests.exercise(self)
        self.assertIsNone(error);self.assertEqual(guards,[674,650,435,435]);self.assertEqual(info['consumer_ssh_exit'],0)
        self.assertIn(['mkdir','--',refresh.GUEST+'-stage'],calls)
        self.assertNotIn(NAME+'-stage',' '.join(' '.join(c) for c in calls))

    def test_actual_r3_pre_stage_failures_and_nonempty_never_write(self):
        with mock.patch.object(refresh,'procedure',side_effect=procedure):
            for problem in ('ready','helper','archive','input','probe','path','key','space','binary','identity','exists','approval','overlay'):
                with self.subTest(problem=problem):
                    info,calls,_,error,_,_=refresh.RefreshTests.exercise(self,problem=problem)
                    self.assertIsNotNone(error);self.assertFalse(any(c[0] in ('mkdir','tar') for c in calls));self.assertIn('archive_skipped',info)
            for raw in (refresh.owners.wire(refresh.owners.complete(True)),b'bad\n',refresh.owners.wire(refresh.owners.complete(),70000)):
                _,calls,_,error,_,_=refresh.RefreshTests.exercise(self,raw=raw)
                self.assertIsNotNone(error);self.assertFalse(any(c[0] in ('mkdir','tar') for c in calls))
            for guard in (674,650,435):
                _,calls,_,error,_,_=refresh.RefreshTests.exercise(self,guard=guard)
                self.assertIsNotNone(error);self.assertFalse(any(c[0] in ('mkdir','tar') for c in calls))

    def test_preserved_storage_negative_gates_no_mutation_or_network(self):
        retained=json.loads((LAB/'native-exit-calibration-r2-20261024.host-audit/after.json').read_text())
        capacity=json.loads((LAB/'capacity-maintenance-r1-20261023.preflight/preflight.json').read_text())
        target=LAB/NEW;parent=LAB/'system-capacity-r1-20261023.qcow2';failed=LAB/'system-readiness-r1-20261024.qcow2'
        real_id=g4_capacity.file_id;real_open=Path.open
        for problem in ('exists','symlink','parent-id','failed-id','parent-content','failed-content','backing','reserve'):
            p=procedure();rows=copy.deepcopy(retained['chain'])
            if problem=='backing':rows[0]['full-backing-filename']='/wrong'
            def identity(path):
                value=real_id(path)
                if (problem=='parent-id' and path==parent) or (problem=='failed-id' and path==failed):value['inode']+=1
                return value
            def dispatch(argv,**kwargs):
                self.assertEqual(argv,['/usr/bin/qemu-img','info','--output=json','--backing-chain',str(failed)])
                return subprocess.CompletedProcess(argv,0,json.dumps(rows).encode())
            def opening(path,*args,**kwargs):
                if path in (parent,failed):return io.BytesIO(b'controlled hash fixture')
                return real_open(path,*args,**kwargs)
            digests=[mock.Mock(hexdigest=lambda:'c2acebb986bab6e3c6cd6bdd0c7efbe4c4d3b4f7f69ed358a7d53598ba1f9936'),mock.Mock(hexdigest=lambda:'0'*64)]
            if problem=='parent-content':digests[0]=mock.Mock(hexdigest=lambda:'0'*64)
            with mock.patch.object(p.os.path,'lexists',side_effect=lambda path:problem=='exists' if path==target else False), \
                 mock.patch.object(Path,'resolve',autospec=True,side_effect=lambda path,*a,**k: Path('/wrong') if path==target and problem=='symlink' else path), \
                 mock.patch.object(g4_capacity,'file_id',side_effect=identity),mock.patch.object(g4_capacity,'owners',return_value={'idle':True}), \
                 mock.patch.object(p.shutil,'disk_usage',return_value=mock.Mock(free=1 if problem=='reserve' else 100*1024**3)), \
                 mock.patch.object(p.subprocess,'run',side_effect=dispatch) as calls,mock.patch.object(Path,'open',autospec=True,side_effect=opening), \
                 mock.patch.object(p.hashlib,'sha256',side_effect=digests) as hashing:
                reason={'exists':'Fresh canonical R3 target must be absent','symlink':'Fresh canonical R3 target must be absent',
                    'parent-id':'Frozen parent/lower identities changed','failed-id':'Preserved failed layer identity changed',
                    'parent-content':'Frozen parent/preserved failed layer content changed','failed-content':'Frozen parent/preserved failed layer content changed',
                    'backing':'Preserved branch metadata changed','reserve':'D: reserve below80GiB'}[problem]
                with self.subTest(problem=problem),self.assertRaisesRegex(RuntimeError,reason):p.prepare_native_isolation_storage(capacity)
                self.assertEqual(calls.call_count,1 if problem in ('parent-content','failed-content','backing') else 0)
                self.assertEqual(hashing.call_count,2 if problem=='failed-content' else 1 if problem=='parent-content' else 0)

    def test_actual_r3_preparation_synthetic_storage_no_create_network_native(self):
        p=procedure()
        with tempfile.TemporaryDirectory(prefix='r3-prep-test-',dir=LAB) as temp:
            p.PREP=Path(temp)/'prep';p.RUN=p.BOOT=Path(temp)/NAME
            storage=dict(new_target=str(LAB/NEW),new_target_absent=True,create_argv=['/usr/bin/qemu-img','create','-f','qcow2','-F','qcow2','-b',str(LAB/'system-capacity-r1-20261023.qcow2'),str(LAB/NEW)])
            with mock.patch.object(p,'prepare_native_isolation_storage',return_value=storage), \
                 mock.patch.object(p,'prepare_durable_credential',return_value={'public_matches_seed':True}), \
                 mock.patch.object(p,'source_head',return_value='synthetic source fixture'),mock.patch.object(p,'windows_path',return_value='C:\\owned\\controller.ps1'), \
                 mock.patch.object(p.subprocess,'run',side_effect=AssertionError('No dispatch')) as calls,mock.patch('builtins.print'):
                p.prepare()
            r=json.loads((p.PREP/'preflight.json').read_text())
            self.assertEqual(r['host_run_name'],NAME);self.assertEqual(r['guest_output'],refresh.GUEST)
            self.assertEqual(r['current_overlay'],str(LAB/NEW));self.assertTrue(r['overlay_identity']['absent'])
            self.assertEqual(r['native_python_approval'],p.native_source_approval());self.assertEqual(len(r['source_manifest']),16)
            self.assertEqual(r['original_pe_sha256'],refresh.native_tests.cal.PES);self.assertEqual(len(r['host_helpers_sha256']),8)
            self.assertNotIn('--gpu-recovery',r['guest_test_argv']);self.assertFalse(any('vendor/' in n for n in r['source_manifest']))
            self.assertEqual(r['storage']['create_argv'][-1],str(LAB/NEW));calls.assert_not_called()


if __name__=='__main__':unittest.main()
