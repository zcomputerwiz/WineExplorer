# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the actual private refresh branch; all guest operations are fakes."""
import base64
import copy
import importlib.util
import io
import json
import os
from pathlib import Path
import shlex
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock

import g4_guest_readiness
import g4_capacity
import g4_virgl_budget
import stop_g4_virgl_launcher
import test_g4_prefix_ownership_integration as owners
import test_g4_native_exit_calibration as native_tests

REPO=Path(__file__).resolve().parents[1]
PRIVATE=REPO/'build/g4-virgl-20261022/presentation-timing-trial.py'
NAME='native-exit-calibration-r2-20261024'
GUEST='/home/debian/wex-g4/native-exit-calibration-r1-20261023'


def procedure():
    with mock.patch.dict(os.environ,dict(WEX_G4_CONTROL='native-exit-calibration',WEX_TIMING_RUN_NAME=NAME,WEX_TIMING_BOOT_NAME=NAME)):
        spec=importlib.util.spec_from_file_location('native_refresh_private',PRIVATE)
        p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
        return p


@unittest.skipUnless(PRIVATE.is_file() and owners.LAB.is_dir(),'owned private inputs unavailable')
class RefreshTests(unittest.TestCase):
    def exercise(self,*,problem=None,raw=None,code=0,guard=None):
        p=procedure();calls=[];guards=[];snapshots=[]
        with tempfile.TemporaryDirectory(prefix='native-refresh-test-',dir=owners.LAB) as temp:
            root=Path(temp);run=root/'run';prep=root/'prep';run.mkdir();prep.mkdir()
            p.LAB=root;p.RUN=p.BOOT=run;p.PREP=prep
            inputs={}
            for i in range(16):
                f=prep/('input'+str(i));f.write_bytes(str(i).encode())
                inputs['scripts/input'+str(i)]=dict(path=str(f),sha256=p.sha(f))
            stage=prep/'source-stage.tar';stage.write_bytes(b'bounded fake stage')
            seed=prep/'seed.iso';seed.write_bytes(b'seed')
            pins={'/binary/'+str(i):'a'*64 for i in range(19)}
            overlay=str(root/'system-readiness-r1-20261024.qcow2')
            ssh,consumer=p.commands(Path('/var/tmp/wex-g4-lab-credentials/vm_key'))
            record=dict(ssh_argv=ssh,ssh_key_path='/var/tmp/wex-g4-lab-credentials/vm_key',guest_test_argv=consumer,
                host_run_name=NAME,guest_stage=p.STAGE,guest_output=p.OUTPUT,guest_absence_argv=p.native_absence_argv(),
                guest_archive_argv=p.native_archive_argv(),host_helpers_sha256={},host_seed_files={},host_test_inventory={},
                source_manifest={n:dict(sha256=v['sha256']) for n,v in inputs.items()},local_inputs=inputs,
                stage_archive_sha256=p.sha(stage),seed_iso=dict(path=str(seed),sha256=p.sha(seed)),current_overlay=overlay,
                known_binary_sha256=pins,minimum_guest_free_bytes=1024**3,probe_code=owners.ownership.PROBE,
                probe_sha256=owners.PROBE_SHA,requested_prefix=owners.PREFIX,probe_nonce=owners.NONCE,
                ownership_probe_argv=['sudo','-n','python3','-B','-c',owners.ownership.PROBE,owners.PREFIX,owners.NONCE])
            if problem=='helper':record['host_helpers_sha256']={'scripts/g4_prefix_ownership.py':'0'*64}
            if problem=='archive':record['stage_archive_sha256']='0'*64
            if problem=='input':next(iter(inputs.values()))['sha256']='0'*64
            if problem=='probe':record['probe_sha256']='0'*64
            if problem=='path':record['guest_output']+='/foreign'
            if problem=='key':record['ssh_argv'][2]='/tmp/stale-key'
            (prep/'preflight.json').write_text(json.dumps(record))
            (run/'launcher-identity.json').write_text(json.dumps(dict(qemu_pid=1,qemu_start_ticks=1,xvfb_pid=2,xvfb_start_ticks=2)))
            (root/(NAME+'.wsl-session.json')).write_text('{}')
            if raw is None:raw=owners.wire(owners.complete(),12000)
            save=p.save
            def capture(path,value):snapshots.append(copy.deepcopy(value));save(path,value)
            def budget(data,name,required):
                guards.append(required)
                return dict(passed=required!=guard,error='fake budget',remaining_seconds=880)
            def dispatch(argv,**kw):
                c=shlex.split(argv[-1]);calls.append(c)
                if c==consumer:
                    self.assertIn(record['ownership_probe_argv'],calls)
                    self.assertIn(record['guest_absence_argv'],calls)
                    return subprocess.CompletedProcess(argv,0)
                if c[0]=='df':out='disk'
                elif c==record['guest_absence_argv']:
                    self.assertIn(record['ownership_probe_argv'],calls)
                    if problem=='exists':raise subprocess.CalledProcessError(1,argv,b'',b'fixed target exists')
                    out=b''
                elif c[0]=='python3':out=str(0 if problem=='space' else 2*1024**3)
                elif c[0]=='sha256sum':
                    expected=pins if c[2:]==list(pins) else {p.STAGE+'/'+n:v['sha256'] for n,v in record['source_manifest'].items()}
                    out=''.join(('b'*64 if problem=='binary' and expected==pins else v)+'  '+k+'\n' for k,v in expected.items())
                elif c==record['ownership_probe_argv']:
                    self.assertEqual(kw['timeout'],10);self.assertNotIn('text',kw)
                    if code=='timeout':raise subprocess.TimeoutExpired(argv,10,raw,b'partial failure')
                    if code:raise subprocess.CalledProcessError(code,argv,raw,b'\xfforiginal error')
                    out=raw
                elif c==['mkdir','--',p.STAGE]:out=b''
                elif c==['tar','-xf','-','-C',p.STAGE]:out=b''
                elif c==record['guest_archive_argv']:
                    with tarfile.open(fileobj=kw['stdout'],mode='w:gz') as archive:
                        data=b'original fake evidence';member=tarfile.TarInfo('results.txt');member.size=len(data)
                        archive.addfile(member,io.BytesIO(data))
                    return subprocess.CompletedProcess(argv,0)
                else:raise AssertionError('Unexpected guest dispatch: '+repr(c))
                return subprocess.CompletedProcess(argv,0,out,b'')
            def identity(pid):return dict(start_ticks=99 if problem=='identity' else pid,
                cmdline=['qemu','file='+overlay+',format=qcow2,if=virtio'])
            with mock.patch.object(p,'save',side_effect=capture),mock.patch.object(p.subprocess,'run',side_effect=dispatch), \
                 mock.patch.object(g4_guest_readiness,'wait_ready',return_value={'ready':problem!='ready','attempts':[]}) as readiness, \
                 mock.patch.object(g4_virgl_budget,'check_budget',side_effect=budget), \
                 mock.patch.object(stop_g4_virgl_launcher,'validate_identity',return_value={'pid':3}), \
                 mock.patch.object(stop_g4_virgl_launcher,'proc_identity',side_effect=identity),mock.patch('builtins.print'):
                try:p.run();error=None
                except Exception as caught:error=caught
            info=json.loads((run/'execution.json').read_text())
            return info,calls,guards,error,snapshots,readiness.call_args

    def test_fixed_compiled_paths_fresh_host_and_normal_original_only_command(self):
        p=procedure();self.assertTrue(p.NATIVE_REFRESH);self.assertEqual(p.NAME,NAME)
        self.assertEqual(p.STAGE,GUEST+'-stage');self.assertEqual(p.OUTPUT,GUEST)
        header=(REPO/'scripts/g4_native_exit_contract.h').read_text()
        for name,value in [('WEX_EXIT_CHILD',native_tests.cal.win(p.STAGE+'/original/exit-child.exe')),('WEX_EXIT_CAL_OUTPUT',native_tests.cal.win(GUEST))]:
            self.assertIn('#define '+name+' L"'+value.replace('\\','\\\\')+'"',header)
        command=p.commands(Path('/var/tmp/wex-g4-lab-credentials/vm_key'))[1]
        self.assertIn('--private-loopback',command);self.assertIn(p.STAGE+'/scripts/check-g4-native-exit-calibration.py',command)
        self.assertEqual(command[-2:],['--output-dir',GUEST])
        self.assertFalse(any(x in shlex.join(command) for x in ('vendor','chromium','winedbg','--gpu-recovery')))

    def test_complete_empty_large_raw_before_stage_and_original_consumer_archive(self):
        info,calls,guards,error,snapshots,ready=self.exercise()
        self.assertIsNone(error);self.assertEqual(guards,[674,650,435,435]);self.assertEqual(ready.kwargs['maximum_seconds'],180)
        self.assertEqual(info['validated_ownership']['owners'],[])
        self.assertLess(next(i for i,c in enumerate(calls) if c[0]=='sudo'),next(i for i,c in enumerate(calls) if c[0]=='mkdir'))
        self.assertEqual(info['consumer_ssh_exit'],0);self.assertEqual(info['archive_exit'],0)
        self.assertEqual(len(info['deployed_source_sha256']),16)
        first=next(s for s in snapshots if 'ownership_probe_observation' in s)
        self.assertNotIn('validated_ownership',first)
        self.assertGreater(first['ownership_probe_observation']['stdout']['retained_byte_length'],4096)

    def test_all_pre_stage_guards_and_inputs_fail_without_stage_or_archive(self):
        for problem in ('ready','helper','archive','input','probe','path','key','space','binary','identity','exists'):
            with self.subTest(problem=problem):
                info,calls,_,error,_,_=self.exercise(problem=problem)
                self.assertIsNotNone(error);self.assertFalse(any(c[0] in ('mkdir','tar') for c in calls))
                self.assertIn('archive_skipped',info);self.assertNotIn('consumer_ssh_exit',info)
        for guard in (674,650,435):
            info,calls,_,error,_,_=self.exercise(guard=guard)
            self.assertIsNotNone(error);self.assertFalse(any(c[0] in ('mkdir','tar') for c in calls));self.assertIn('archive_skipped',info)

    def test_busy_unknown_malformed_wrong_nonce_owner_and_truncation_no_stage(self):
        unknown=owners.complete();unknown.update(status='unknown',owners=None,busy=None)
        nonce=owners.complete();nonce['nonce']='2'*32
        bad=owners.complete(True);bad['owners'][0]['pid']=True
        for raw in (owners.wire(owners.complete(True)),owners.wire(unknown),b'bad\n',owners.wire(nonce),owners.wire(bad),owners.wire(owners.complete(),70000)):
            info,calls,_,error,_,_=self.exercise(raw=raw)
            self.assertIsNotNone(error);self.assertEqual(calls[-1][0],'sudo');self.assertIn('archive_skipped',info)
            self.assertFalse(any(c[0] in ('mkdir','tar') for c in calls))

    def test_nonzero_timeout_preserve_lossless_64k_no_followup(self):
        raw=b'\xff'+b'x'*20000
        for code in (1,'timeout'):
            info,calls,_,error,_,_=self.exercise(raw=raw,code=code)
            self.assertIsNotNone(error);self.assertEqual(calls[-1][0],'sudo')
            failure=info['remote_failures'][0]
            self.assertEqual(base64.b64decode(failure['stdout']['base64']),raw)
            self.assertNotIn('validated_ownership',info);self.assertIn('archive_skipped',info)

    def test_absence_check_real_local_python_rejects_existing_dangling_and_noncanonical(self):
        p=procedure()
        with tempfile.TemporaryDirectory(prefix='native-targets-',dir=owners.LAB) as temp:
            root=Path(temp);p.STAGE=str(root/'stage');p.OUTPUT=str(root/'output')
            def check():return subprocess.run(p.native_absence_argv(),capture_output=True,timeout=5).returncode
            self.assertEqual(check(),0)
            Path(p.STAGE).mkdir();self.assertNotEqual(check(),0);Path(p.STAGE).rmdir()
            Path(p.OUTPUT).symlink_to(root/'missing');self.assertNotEqual(check(),0);Path(p.OUTPUT).unlink()
            p.OUTPUT=str(root/'missing'/'..'/'output');self.assertNotEqual(check(),0)

    def test_current_sealed_storage_missing_changed_backing_and_reserve_fail(self):
        retained=json.loads((owners.LAB/'prefix-ownership-r1-20261024.host-audit/after.json').read_text())
        capacity=json.loads((owners.LAB/'capacity-maintenance-r1-20261023.preflight/preflight.json').read_text())
        target=owners.LAB/'system-readiness-r1-20261024.qcow2';real_id=g4_capacity.file_id
        for problem in ('missing','identity','content','backing','reserve'):
            p=procedure();real_sha=p.sha;rows=copy.deepcopy(retained['chain']);rows[0]['backing-filename']='/wrong/backing'
            def identity(path):
                if path==target:
                    if problem=='missing':raise FileNotFoundError('missing current top')
                    value=dict(retained['new_top_identity'])
                    if problem=='identity':value['inode']+=1
                    return value
                return real_id(path)
            def dispatch(argv,**kw):
                self.assertEqual(argv,['/usr/bin/qemu-img','info','--output=json','--backing-chain',str(target)])
                return subprocess.CompletedProcess(argv,0,json.dumps(rows).encode())
            with mock.patch.object(g4_capacity,'file_id',side_effect=identity), \
                 mock.patch.object(p,'sha',side_effect=lambda path:('0'*64 if problem=='content' else retained['new_top_sha256']) if path==target else real_sha(path)), \
                 mock.patch.object(g4_capacity,'owners',return_value={'idle':True}), \
                 mock.patch.object(p.shutil,'disk_usage',return_value=mock.Mock(free=(1 if problem=='reserve' else 100*1024**3))), \
                 mock.patch.object(p.subprocess,'run',side_effect=dispatch) as calls:
                with self.assertRaises((RuntimeError,FileNotFoundError)):p.prepare_ready_storage(capacity)
            self.assertEqual(calls.call_count,1 if problem=='backing' else 0)

    def test_actual_offline_preparation_original_receipts_16_inputs_no_network_or_create(self):
        p=procedure()
        with tempfile.TemporaryDirectory(prefix='native-preparation-test-',dir=owners.LAB) as temp:
            p.PREP=Path(temp)/'prep';p.RUN=p.BOOT=Path(temp)/NAME
            state=dict(safety_top_identity={'path':str(owners.LAB/'system-readiness-r1-20261024.qcow2')},
                safety_top_previously_booted=True,image_creation_authorized=False)
            with mock.patch.object(p,'prepare_ready_storage',return_value=state), \
                 mock.patch.object(p,'prepare_durable_credential',return_value={'public_matches_seed':True}), \
                 mock.patch.object(p,'source_head',return_value='offline-test'), \
                 mock.patch.object(p,'windows_path',return_value='C:\\owned\\controller.ps1'), \
                 mock.patch.object(p.subprocess,'run',side_effect=AssertionError('Forbidden network/create/native dispatch')) as calls, \
                 mock.patch('builtins.print'):
                p.prepare()
            r=json.loads((p.PREP/'preflight.json').read_text())
            self.assertEqual(r['status'],'PREPARED_NOT_RUN');self.assertEqual(r['host_run_name'],NAME)
            self.assertEqual(r['guest_stage'],GUEST+'-stage');self.assertEqual(r['guest_output'],GUEST)
            self.assertEqual(r['guest_absence_argv'],p.native_absence_argv())
            self.assertEqual(r['guest_archive_argv'],p.native_archive_argv())
            self.assertEqual(r['ssh_argv'][2],'/var/tmp/wex-g4-lab-credentials/vm_key')
            self.assertEqual(r['original_pe_sha256'],native_tests.cal.PES)
            self.assertEqual(len(r['worker_build_logs']),8);self.assertEqual(len(r['known_binary_sha256']),19)
            self.assertEqual(len(r['source_manifest']),16);self.assertEqual(set(r['source_manifest']),set(r['local_inputs']))
            self.assertEqual(len(r['host_helpers_sha256']),8)
            self.assertNotIn('create_argv',r['storage']);self.assertIn('--timeout 80',r['stop_command'])
            self.assertEqual(r['native_event_schema']['manifest_sha256'],None)
            self.assertFalse(r['native_event_schema']['manifest_verified_by_native'])
            with tarfile.open(p.PREP/'source-stage.tar') as archive:
                self.assertEqual(len(archive.getmembers()),16)
                self.assertTrue(all(v.isfile() and not v.name.startswith('vendor/') for v in archive.getmembers()))
            calls.assert_not_called()


if __name__=='__main__':unittest.main()
