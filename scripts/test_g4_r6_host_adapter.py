# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual private R6 adapter, mocked remote operations; no guest execution."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shlex
import struct
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock
import test_g4_prefix_ownership_integration as ownership

REPO=Path(__file__).resolve().parents[1]
PRIVATE=REPO/'build/g4-virgl-20261022/chromium-registration-r6-trial.py'
def adapter():
    spec=importlib.util.spec_from_file_location('r6_private',PRIVATE);p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p);return p

class R6HostTests(unittest.TestCase):
    def test_current_complete_inventory_names_mapping_and_concrete_artifacts(self):
        p=adapter();files=p.transfer_inputs();self.assertEqual(p.NAME,'chromium-registration-r6-20261026')
        for name in ('g4_userenv_r6_probe.py','g4_userenv_probe.py','g4_userenv_view.py','graphics_wexcomp.py','check-g4-native-exit-calibration.py'):
            self.assertIn('scripts/'+name,files)
        self.assertEqual(files['original/native-exit.exe'],p.PE)
        self.assertEqual(files['original/userenv-r6-probe.exe'],p.BUILD_DIR/'userenv-r6-probe.exe')
        for dll in ('kernelbase','userenv'):
            for arch,machine in (('x86_64',0x8664),('i386',0x14c)):
                self.assertEqual(files[f'modules/{arch}/{dll}.dll'],p.MODULE_BUILD/f'dlls/{dll}/{arch}-windows/{dll}.dll')
                self.assertEqual(p.artifact_architecture(files[f'modules/{arch}/{dll}.dll']),machine)
        self.assertEqual(p.artifact_architecture(p.PE),0x8664);p.compiled_contract()
        argv=p.guest_consumer();self.assertIn('--private-loopback',argv);self.assertEqual(argv[-2:],[p.STAGE+'/scripts/g4_userenv_view.py','--userenv-r6'])
        code=p.outside_argv()[-1];self.assertIn('dlls/kernelbase/',code);self.assertIn('dlls/userenv/',code)
        self.assertEqual(code.count('-windows/'),4)

    def test_real_transfer_bytes_and_malformed_members_fail(self):
        for fault in (None,'checksum','member','symlink'):
            with tempfile.TemporaryDirectory(prefix='r6-bytes-',dir=ownership.LAB) as folder:
                p=adapter();p.PREP=Path(folder);data=b'original';h=hashlib.sha256(data).hexdigest()
                with tarfile.open(p.PREP/'source-stage.tar','w') as archive:
                    t=tarfile.TarInfo('own.py');t.size=len(data)
                    if fault=='symlink':t.type=tarfile.SYMTYPE;t.linkname='/foreign';t.size=0;archive.addfile(t)
                    else:archive.addfile(t,io.BytesIO(data))
                record={'archive_sha256':p.sha(p.PREP/'source-stage.tar'),'transfer_files':{'own.py':{'sha256':h}}}
                if fault=='checksum':record['archive_sha256']='0'*64
                if fault=='member':record['transfer_files']['own.py']['sha256']='1'*64
                if fault:
                    with self.assertRaises((ValueError,RuntimeError)):p.validate_transfer(record)
                else:p.validate_transfer(record)

    def test_prepare_collision_precedes_all_procedure_actions(self):
        p=adapter()
        with tempfile.TemporaryDirectory(prefix='r6-collision-',dir=ownership.LAB) as folder,mock.patch.object(p,'RUN',Path(folder)),mock.patch.object(p,'reviewed') as reviewed:
            with self.assertRaisesRegex(RuntimeError,'HOST output exists'):p.prepare()
            reviewed.assert_not_called()

    def test_real_run_success_and_guard_failures_no_unlawful_followup(self):
        for fault in (None,'ready','budget','disk','transfer','runtime','outside'):
            with self.subTest(fault=fault),tempfile.TemporaryDirectory(prefix='r6-flow-',dir=ownership.LAB) as folder:
                p=adapter();root=Path(folder);p.LAB=root;p.RUN=root/'run';p.RUN.mkdir();p.PREP=root/'prep';p.PREP.mkdir()
                data=b'x';digest=hashlib.sha256(data).hexdigest()
                with tarfile.open(p.PREP/'source-stage.tar','w') as archive:
                    t=tarfile.TarInfo('own.py');t.size=1;archive.addfile(t,io.BytesIO(data))
                record=dict(disk=str(root/'system-native-identity-r4-20261024.qcow2'),archive_sha256=p.sha(p.PREP/'source-stage.tar'),guest_stage=p.STAGE,
                    compiled_output=p.OUTPUT,compiled_profile=p.OUTPUT+'/profile',compiled_runtime=p.RUNTIME,guest_consumer_argv=p.guest_consumer(),transfer_files={'own.py':{'sha256':digest}})
                if fault=='transfer':record['archive_sha256']='0'*64
                p.save(p.PREP/'preflight.json',record);p.save(p.RUN/'launcher-identity.json',dict(qemu_pid=1,qemu_start_ticks=1,xvfb_pid=2,xvfb_start_ticks=2))
                (root/(p.NAME+'.wsl-session.json')).write_text('{}');calls=[];outside_count=[0]
                def dispatch(argv,**kwargs):
                    c=shlex.split(argv[-1]);calls.append(c)
                    if c==p.guest_consumer():return subprocess.CompletedProcess(argv,1)
                    if c[:3]==['tar','-C',p.OUTPUT]:
                        with tarfile.open(fileobj=kwargs['stdout'],mode='w:gz') as archive:
                            value=b'{"diagnostic_exit":1,"passed":false}';t=tarfile.TarInfo('results.json');t.size=len(value);archive.addfile(t,io.BytesIO(value))
                        return subprocess.CompletedProcess(argv,0)
                    if c==p.outside_argv():
                        outside_count[0]+=1;out=json.dumps({'module':2 if fault=='outside' and outside_count[0]==2 else 1}).encode()
                    elif c[0]=='sudo':
                        v=ownership.complete();v['nonce']=c[-1];out=ownership.wire(v)
                    elif c[0]=='sha256sum':out=(digest+'  '+p.STAGE+'/own.py\n').encode()
                    elif '--runtime-probe' in c:
                        self.assertIn('--userenv-r6',c);out=json.dumps({'state':'absent' if fault=='runtime' else 'complete'}).encode()
                    elif c[0]=='python3' and 'disk_usage' in c[-1]:out=b'2000000000\n'
                    elif c[0] in ('mkdir','tar','python3'):out=b''
                    else:raise AssertionError(c)
                    return subprocess.CompletedProcess(argv,0,out,b'')
                identity=lambda pid:dict(start_ticks=pid,cmdline=['file='+('wrong' if fault=='disk' else record['disk'])+',format=qcow2,if=virtio'])
                with mock.patch.object(p,'validate_identity',return_value={}),mock.patch.object(p,'proc_identity',side_effect=identity),mock.patch.object(p,'wait_ready',return_value={'ready':fault!='ready'}),mock.patch.object(p,'check_budget',return_value={'passed':fault!='budget','remaining_seconds':880,'error':'budget'}),mock.patch.object(p.subprocess,'run',side_effect=dispatch),mock.patch('builtins.print'):
                    if fault in ('ready','budget','disk','transfer','runtime'):
                        with self.assertRaises(RuntimeError):p.run()
                    else:p.run()
                v=json.loads((p.RUN/'execution.json').read_text())
                if fault not in (None,'outside'):
                    self.assertFalse(v['consumer_started']);self.assertIn('archive_skipped',v)
                    self.assertFalse(any(c[:3]==['tar','-C',p.OUTPUT] for c in calls))
                    if fault!='runtime':self.assertFalse(any(c[0]=='mkdir' for c in calls))
                else:
                    self.assertEqual(v['orchestration_complete'],fault is None)
                    self.assertEqual(v['outside_modules_unchanged'],fault is None)
                    self.assertEqual(outside_count[0],2)
                self.assertFalse(any('--stage-runtime' in c for c in calls))

if __name__=='__main__':unittest.main()
