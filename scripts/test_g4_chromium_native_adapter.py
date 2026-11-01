# SPDX-License-Identifier: GPL-2.0-or-later
import importlib.util
import io
import json
from pathlib import Path
import shlex
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock

import test_g4_chromium_native_exit as native_tests
import test_g4_prefix_ownership_integration as ownership

REPO=Path(__file__).resolve().parents[1];PRIVATE=REPO/'build/g4-virgl-20261022/chromium-native-trial.py'
def adapter():
    spec=importlib.util.spec_from_file_location('native_adapter',PRIVATE);p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p);return p

@unittest.skipUnless(PRIVATE.is_file() and ownership.LAB.is_dir(),'private source/storage unavailable')
class AdapterTests(unittest.TestCase):
    def test_diagnostic1_with_complete_archive_is_orchestration_not_product_failure(self):
        with tempfile.TemporaryDirectory(prefix='native-adapter-complete-',dir=ownership.LAB) as temp:
            p=adapter();root=Path(temp);p.LAB=root;p.RUN=root/'run';p.RUN.mkdir();p.PREP=root/'prep';p.PREP.mkdir()
            payload=b'x';digest=__import__('hashlib').sha256(payload).hexdigest()
            with tarfile.open(p.PREP/'source-stage.tar','w') as archive:
                member=tarfile.TarInfo('own.py');member.size=1;archive.addfile(member,io.BytesIO(payload))
            record=dict(disk=str(root/'system-native-identity-r4-20261024.qcow2'),archive_sha256=p.sha(p.PREP/'source-stage.tar'),guest_stage=p.STAGE,
                compiled_output=p.OUTPUT,compiled_runtime=p.RUNTIME,guest_consumer_argv=p.guest_consumer(),transfer_files={'own.py':{'sha256':digest}})
            p.save(p.PREP/'preflight.json',record);p.save(p.RUN/'launcher-identity.json',dict(qemu_pid=1,qemu_start_ticks=1,xvfb_pid=2,xvfb_start_ticks=2))
            (root/(p.NAME+'.wsl-session.json')).write_text('{}');calls=[]
            def dispatch(argv,**kwargs):
                c=shlex.split(argv[-1]);calls.append(c)
                if c==p.guest_consumer():return subprocess.CompletedProcess(argv,1)
                if c[:3]==['tar','-C',p.OUTPUT]:
                    with tarfile.open(fileobj=kwargs['stdout'],mode='w:gz') as archive:
                        data=json.dumps(dict(diagnostic_exit=1,passed=False,observation_complete=True)).encode()
                        item=tarfile.TarInfo('results.json');item.size=len(data);archive.addfile(item,io.BytesIO(data))
                    return subprocess.CompletedProcess(argv,0)
                if c[0]=='sudo':
                    value=ownership.complete();value['nonce']=c[-1];out=ownership.wire(value)
                elif c[0]=='sha256sum':out=(digest+'  '+p.STAGE+'/own.py\n').encode()
                elif '--runtime-probe' in c:out=b'{"state":"complete"}'
                elif c[0]=='python3' and 'disk_usage' in c[-1]:out=b'2000000000\n'
                elif c[0] in ('python3','mkdir','tar'):out=b''
                else:raise AssertionError(c)
                return subprocess.CompletedProcess(argv,0,out,b'')
            with mock.patch.object(p,'validate_identity',return_value={}),mock.patch.object(p,'proc_identity',side_effect=lambda pid:dict(start_ticks=pid,cmdline=['file='+record['disk']+',format=qcow2,if=virtio'])), \
                 mock.patch.object(p,'wait_ready',return_value={'ready':True}),mock.patch.object(p,'check_budget',return_value={'passed':True,'remaining_seconds':880}), \
                 mock.patch.object(p.subprocess,'run',side_effect=dispatch),mock.patch('builtins.print'):p.run()
            result=json.loads((p.RUN/'execution.json').read_text())
            self.assertTrue(result['orchestration_complete']);self.assertTrue(result['diagnostic_nonacceptance_expected'])
            self.assertEqual(result['consumer_ssh_exit'],1);self.assertEqual(result['archive_exit'],0)
            self.assertFalse(any('--stage-runtime' in c for c in calls))

    def test_transitive_source_inventory_includes_actual_imports_and_safe_transfer(self):
        p=adapter();files=p.sources()
        for name in ('check-g4-chromium-native-exit.py','check-g4-native-exit-calibration.py','g4_diagnostic_inventory.py','g4_edge_owned_threads.py','g4_edge_http_fixture.py','g4_chromium_cache.py','graphics_resources.py','graphics_wexcomp.py','run-isolated-graphics.py','check-g4-edge-install.py','check-g3-correctness.py'):
            self.assertIn('scripts/'+name,files)
        self.assertFalse(any('vendor' in n for n in files))
        with tempfile.TemporaryDirectory(prefix='native-source-test-',dir=ownership.LAB) as temp:
            p.PREP=Path(temp);tar=p.PREP/'source-stage.tar'
            for name in ('scripts/own.py','../escape','/absolute'):
                with tarfile.open(tar,'w') as archive:
                    member=tarfile.TarInfo(name);member.size=1;archive.addfile(member,io.BytesIO(b'x'))
                record=dict(archive_sha256=p.sha(tar),transfer_files={name:{'sha256':__import__('hashlib').sha256(b'x').hexdigest()}})
                if name=='scripts/own.py':p.validate_transfer(record)
                else:
                    with self.assertRaises(ValueError):p.validate_transfer(record)

    def test_actual_adapter_guard_failures_never_stage_consume_or_archive(self):
        for fault in ('ready','budget','busy','exists','wrong-disk','wrong-command','archive','timeout'):
            with self.subTest(fault=fault),tempfile.TemporaryDirectory(prefix='native-adapter-',dir=ownership.LAB) as temp:
                p=adapter();root=Path(temp);p.LAB=root;p.RUN=root/'run';p.RUN.mkdir();p.PREP=root/'prep';p.PREP.mkdir()
                stage=p.PREP/'source-stage.tar'
                with tarfile.open(stage,'w') as archive:
                    member=tarfile.TarInfo('own.py');member.size=1;archive.addfile(member,io.BytesIO(b'x'))
                record=dict(disk=str(root/'system-native-identity-r4-20261024.qcow2'),archive_sha256=p.sha(stage),guest_stage=p.STAGE,
                    compiled_output=p.OUTPUT,compiled_runtime=p.RUNTIME,guest_consumer_argv=p.guest_consumer(),transfer_files={'own.py':{'sha256':__import__('hashlib').sha256(b'x').hexdigest()}})
                if fault=='wrong-disk':record['disk']='/wrong'
                if fault=='wrong-command':record['guest_consumer_argv']=['wrong']
                if fault=='archive':record['archive_sha256']='0'*64
                p.save(p.PREP/'preflight.json',record)
                p.save(p.RUN/'launcher-identity.json',dict(qemu_pid=1,qemu_start_ticks=1,xvfb_pid=2,xvfb_start_ticks=2))
                (root/(p.NAME+'.wsl-session.json')).write_text('{}');calls=[]
                def dispatch(argv,**kwargs):
                    command=shlex.split(argv[-1]);calls.append(command)
                    if fault=='timeout':raise subprocess.TimeoutExpired(argv,10,b'partial',b'original stderr')
                    if command[0]=='sudo':
                        value=ownership.complete(fault=='busy');value['nonce']=command[-1]
                        return subprocess.CompletedProcess(argv,0,ownership.wire(value),b'')
                    if command[0]=='python3' and 'disk_usage' in command[-1]:return subprocess.CompletedProcess(argv,0,b'2000000000\n',b'')
                    if command[0]=='python3' and 'lexists' in command[-1]:return subprocess.CompletedProcess(argv,1 if fault=='exists' else 0,b'',b'original missing guard')
                    raise AssertionError('Forbidden source/ZIP/consumer/archive dispatch')
                with mock.patch.object(p,'validate_identity',return_value={}),mock.patch.object(p,'proc_identity',side_effect=lambda pid:dict(start_ticks=pid,cmdline=['file='+record['disk']+',format=qcow2,if=virtio'])), \
                     mock.patch.object(p,'wait_ready',return_value={'ready':fault!='ready'}),mock.patch.object(p,'check_budget',return_value={'passed':fault!='budget','error':'original budget','remaining_seconds':880}), \
                     mock.patch.object(p.subprocess,'run',side_effect=dispatch),mock.patch('builtins.print'):
                    with self.assertRaises((RuntimeError,subprocess.TimeoutExpired)):p.run()
                info=json.loads((p.RUN/'execution.json').read_text());self.assertFalse(info['consumer_started']);self.assertIn('archive_skipped',info)
                self.assertFalse(any(c[0] in ('mkdir','tar') for c in calls));self.assertFalse(info['orchestration_complete'])
                if fault=='timeout':self.assertEqual(info['remote_timeouts'][0]['stderr_base64'],__import__('base64').b64encode(b'original stderr').decode())

if __name__=='__main__':unittest.main()
