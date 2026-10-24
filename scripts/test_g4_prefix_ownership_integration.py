# SPDX-License-Identifier: GPL-2.0-or-later
import base64
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
from unittest import mock

import g4_prefix_ownership as ownership
import g4_guest_readiness
import g4_virgl_budget
import g4_capacity
import stop_g4_virgl_launcher
import test_g4_prefix_ownership as fixtures

LAB=fixtures.LAB
REPO=Path(__file__).resolve().parents[1]
PRIVATE=REPO/'build/g4-virgl-20261022/presentation-timing-trial.py'
PREFIX='/home/debian/wex-g4/edge-install-r1-v2-20261022/prefix'
NONCE='1'*32
PROBE_SHA='ce5fcd5097173dd1b3651a0f6424f1c533f3d6c37d1cf8b868da8b252bcd737c'


def wire(value,padding=0):
    return json.dumps(value,separators=(',',':'),ensure_ascii=False).encode()+b' '*padding+b'\n'


def complete(busy=False):
    with tempfile.TemporaryDirectory(prefix='ownership-response-',dir=LAB) as temp:
        if busy:
            task=Path(temp)/'42';task.mkdir();(task/'stat').write_bytes(fixtures.stat_bytes(42))
            (task/'environ').write_bytes(b'WINEPREFIX='+PREFIX.encode()+b'\0')
        result=ownership.scan(PREFIX,NONCE,proc_root=temp)
        if result['status']!='complete':raise AssertionError('Fixture scanner incomplete')
        return result


@unittest.skipUnless(LAB.is_dir(),'D: private storage unavailable')
class ResponseTests(unittest.TestCase):
    def test_actual_complete_empty_busy_and_accepted_probe_unchanged(self):
        self.assertEqual(hashlib.sha256(ownership.PROBE.encode()).hexdigest(),PROBE_SHA)
        for busy in (False,True):
            value=complete(busy)
            self.assertEqual(ownership.validate_response(wire(value),PREFIX,NONCE,0),value)
            self.assertEqual(value['busy'],busy)
        value=complete(True);value['counts'].update(numeric_entries=2,scanned=2,kernel_threads=1)
        value['observations'].append(dict(pid=2,start_ticks=0,flags=ownership.PF_KTHREAD,
            flags_after=ownership.PF_KTHREAD,classification='kernel_thread_excluded'))
        self.assertEqual(ownership.validate_response(wire(value),PREFIX,NONCE,0),value)

    def test_transport_json_utf8_duplicate_nonfinite_partial_and_size(self):
        good=wire(complete())
        cases=[(good.decode(),0,False),(good,1,False),(good,False,False),(good,0,True),(good,0,1),
            (good[:-1],0,False),(good[:-3],0,False),(b'\xff\n',0,False),(b'[]\n',0,False),
            (good.replace(b'"version":1',b'"version":1,"version":1'),0,False),
            (good.replace(b'"version":1',b'"version":NaN'),0,False),(good+b' '*65536+b'\n',0,False)]
        for raw,code,truncated in cases:
            with self.subTest(code=code,truncated=truncated,size=len(raw)):
                with self.assertRaises(ValueError):ownership.validate_response(raw,PREFIX,NONCE,code,truncated=truncated)

    def test_header_unknown_counters_busy_time_limits_and_owner_negatives(self):
        baseline=complete(True)
        mutations=[('schema','wrong'),('version',True),('version',2),('requested_prefix','/foreign'),
            ('nonce','2'*32),('status','unknown'),('owners',None),('busy',1),('busy',False),
            ('environment_bytes_read',True),('environment_bytes_read',0),('environment_bytes_read',-1),
            ('elapsed_seconds',float('inf')),('finished_monotonic',0)]
        for key,value in mutations:
            row=copy.deepcopy(baseline);row[key]=value
            with self.subTest(key=key,value=value):
                with self.assertRaises(ValueError):ownership.validate_response(wire(row),PREFIX,NONCE,0)
        for group,key,value in (('counts','scanned',False),('counts','numeric_entries',2),('counts','empty_environ',1),
                ('counts','kernel_threads',1),('limits','max_processes',True),('limits','maximum_seconds',9),
                ('limits','max_output_bytes',8191)):
            row=copy.deepcopy(baseline);row[group][key]=value
            with self.assertRaises(ValueError):ownership.validate_response(wire(row),PREFIX,NONCE,0)
        for key,value in (('pid',True),('pid',-1),('start_ticks',True),('start_ticks',-1),('start_ticks',124)):
            row=copy.deepcopy(baseline);row['owners'][0][key]=value
            with self.assertRaises(ValueError):ownership.validate_response(wire(row),PREFIX,NONCE,0)
        for key,value in (('flags',True),('flags_after',ownership.PF_KTHREAD),('classification','kernel_thread_excluded')):
            row=copy.deepcopy(baseline);row['observations'][0][key]=value
            with self.assertRaises(ValueError):ownership.validate_response(wire(row),PREFIX,NONCE,0)
        row=copy.deepcopy(baseline);row['owners'].append(dict(row['owners'][0]))
        row['counts'].update(numeric_entries=2,scanned=2,user_tasks=2);row['environment_bytes_read']*=2
        with self.assertRaisesRegex(ValueError,'Duplicate ownership PID'):ownership.validate_response(wire(row),PREFIX,NONCE,0)
        row=copy.deepcopy(baseline);row['observations'].append(dict(row['observations'][0]));row['counts'].update(numeric_entries=2,scanned=2,user_tasks=2)
        with self.assertRaisesRegex(ValueError,'Duplicate ownership observation'):ownership.validate_response(wire(row),PREFIX,NONCE,0)


@unittest.skipUnless(PRIVATE.is_file() and LAB.is_dir(),'owned private inputs unavailable')
class OwnershipIntegrationTests(unittest.TestCase):
    def procedure(self):
        spec=importlib.util.spec_from_file_location('prefix_private',PRIVATE)
        p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p);return p

    def exercise(self,raw=None,*,code=0,guard=None,ready=True,mutation=None):
        p=self.procedure();snapshots=[];calls=[];guards=[]
        if raw is None:raw=wire(complete())
        with tempfile.TemporaryDirectory(prefix='prefix-integration-',dir=LAB) as temp:
            root=Path(temp);run=root/'run';prep=root/'prep';run.mkdir();prep.mkdir()
            p.LAB=root;p.RUN=p.BOOT=run;p.PREP=prep;p.BOOT_NAME='owned-run';p.CONTROL='prefix-ownership'
            (prep/'seed.iso').write_bytes(b'original seed fixture')
            pins={'/binary/'+str(i):'a'*64 for i in range(19)}
            overlay=str(root/'system-readiness-r1-20261024.qcow2')
            record=dict(ssh_argv=['never-ssh'],guest_test_argv=[],host_helpers_sha256={},host_seed_files={},source_manifest={},
                seed_iso=dict(path=str(prep/'seed.iso'),sha256=p.sha(prep/'seed.iso')),current_overlay=overlay,
                known_binary_sha256=pins,minimum_guest_free_bytes=1024**3,probe_code=ownership.PROBE,
                probe_sha256=PROBE_SHA,requested_prefix=PREFIX,probe_nonce=NONCE,
                ownership_probe_argv=['sudo','-n','python3','-B','-c',ownership.PROBE,PREFIX,NONCE])
            if mutation=='helper':record['host_helpers_sha256']={'scripts/g4_prefix_ownership.py':'0'*64}
            if mutation=='probe':record['probe_sha256']='0'*64
            (prep/'preflight.json').write_text(json.dumps(record))
            (run/'launcher-identity.json').write_text(json.dumps(dict(qemu_pid=1,qemu_start_ticks=1,xvfb_pid=2,xvfb_start_ticks=2)))
            (root/'owned-run.wsl-session.json').write_text('{}')
            save=p.save
            def capture(path,value):snapshots.append(copy.deepcopy(value));save(path,value)
            def budget(data,name,required):guards.append(required);return dict(passed=required!=guard,error='fixture guard',remaining_seconds=880)
            def dispatch(argv,**kw):
                c=shlex.split(argv[-1]);calls.append(c[0])
                if c[0]=='df':out='disk'
                elif c[0]=='python3':out=str(2*1024**3)
                elif c[0]=='sha256sum':out=''.join(('b'*64 if mutation=='binary' else v)+'  '+k+'\n' for k,v in pins.items())
                elif c[0]=='sudo':
                    self.assertEqual(c,record['ownership_probe_argv']);self.assertEqual(kw['timeout'],10);self.assertNotIn('text',kw)
                    if code=='timeout':raise subprocess.TimeoutExpired(argv,10,raw,b'\xfforiginal timeout')
                    if code:raise subprocess.CalledProcessError(code,argv,raw,b'\xfforiginal failure')
                    out=raw
                else:raise AssertionError('Forbidden staging/consumer/archive dispatch')
                return subprocess.CompletedProcess(argv,0,out,b'')
            with mock.patch.object(p,'save',side_effect=capture),mock.patch.object(p.subprocess,'run',side_effect=dispatch), \
                 mock.patch.object(g4_guest_readiness,'wait_ready',return_value={'ready':ready,'attempts':[]}), \
                 mock.patch.object(g4_virgl_budget,'check_budget',side_effect=budget), \
                 mock.patch.object(stop_g4_virgl_launcher,'validate_identity',return_value={'pid':3}), \
                 mock.patch.object(stop_g4_virgl_launcher,'proc_identity',side_effect=lambda pid:dict(start_ticks=pid,cmdline=['qemu','file='+overlay+',format=qcow2,if=virtio'])), \
                 mock.patch('builtins.print'):
                try:p.run();error=None
                except Exception as caught:error=caught
            info=json.loads((run/'execution.json').read_text());self.assertIn('archive_skipped',info)
            self.assertNotIn('consumer_ssh_exit',info);self.assertNotIn('archive_exit',info)
            return info,calls,guards,error,snapshots

    def test_commands_and_legacy_capture_defaults(self):
        p=self.procedure();p.CONTROL='prefix-ownership'
        _,consumer=p.commands(Path('/private/key'));self.assertEqual(consumer,[])
        old=p.captured_output(['old'],0,b'x'*5000,b'')
        self.assertEqual(old['stdout']['cap_bytes'],4096);self.assertTrue(old['stdout']['truncated'])

    def test_complete_empty_busy_and_large_structured_success_stop(self):
        for busy in (False,True):
            raw=wire(complete(busy),12000)
            info,calls,guards,error,snapshots=self.exercise(raw)
            self.assertIsNone(error);self.assertEqual(calls,['df','python3','sha256sum','sudo']);self.assertEqual(guards,[674,650,435])
            self.assertEqual(info['ownership_busy'],busy);self.assertEqual(len(info['observed_owner_list']),1 if busy else 0)
            captured=info['ownership_probe_observation']['stdout']
            self.assertEqual(captured['cap_bytes'],65536);self.assertFalse(captured['truncated'])
            self.assertEqual(base64.b64decode(captured['base64']),raw)
            early=next(v for v in snapshots if 'ownership_probe_observation' in v)
            self.assertNotIn('observed_owner_list',early)

    def test_nonzero_large_structured_failure_lossless_and_oversize_bounded(self):
        failed=complete();failed.update(status='unknown',owners=None,busy=None,error={'code':'fixture'})
        for raw in (wire(failed,20000),wire(failed,70000)):
            info,calls,_,error,_=self.exercise(raw,code=1)
            self.assertIsInstance(error,subprocess.CalledProcessError);self.assertEqual(calls[-1],'sudo')
            captured=info['remote_failures'][0]['stdout']
            self.assertEqual(base64.b64decode(captured['base64']),raw[:65536])
            self.assertEqual(captured['original_byte_length'],len(raw));self.assertEqual(captured['truncated'],len(raw)>65536)
            self.assertNotIn('observed_owner_list',info)

    def test_unknown_malformed_wrong_nonce_bad_owner_and_oversize_never_authoritative(self):
        unknown=complete();unknown.update(status='unknown',owners=None,busy=None)
        nonce=complete();nonce['nonce']='2'*32
        bad=complete(True);bad['owners'][0]['pid']=True
        for raw in (wire(unknown),b'not JSON\n',wire(nonce),wire(bad),wire(complete(),70000),wire(complete()).decode()):
            info,calls,_,error,_=self.exercise(raw)
            self.assertIsInstance(error,ValueError);self.assertEqual(calls,['df','python3','sha256sum','sudo'])
            self.assertNotIn('observed_owner_list',info);self.assertNotIn('validated_ownership',info)
            self.assertIn('ownership_probe_observation',info)

    def test_timeout_partial_large_response_retained_without_fake_exit_or_acceptance(self):
        raw=wire(complete(),20000)
        info,calls,_,error,_=self.exercise(raw,code='timeout')
        self.assertIsInstance(error,subprocess.TimeoutExpired);self.assertEqual(calls,['df','python3','sha256sum','sudo'])
        row=info['remote_failures'][0];self.assertTrue(row['timed_out']);self.assertIsNone(row['returncode'])
        self.assertEqual(base64.b64decode(row['stdout']['base64']),raw)
        self.assertNotIn('observed_owner_list',info)

    def test_guards_readiness_hash_and_probe_failure_no_followup(self):
        for guard,expected in ((674,[]),(650,[]),(435,['df','python3','sha256sum'])):
            _,calls,_,error,_=self.exercise(guard=guard);self.assertIsNotNone(error);self.assertEqual(calls,expected)
        for options,expected in (({'ready':False},[]),({'mutation':'helper'},[]),({'mutation':'binary'},['df','python3','sha256sum']),({'mutation':'probe'},['df','python3','sha256sum'])):
            _,calls,_,error,_=self.exercise(**options);self.assertIsNotNone(error);self.assertEqual(calls,expected)

    def test_current_sealed_prep_storage_negatives_use_current_fixture(self):
        retained=json.loads((LAB/'native-idle-ready-r2-20261024.host-audit/after.json').read_text())
        capacity=json.loads((LAB/'capacity-maintenance-r1-20261023.preflight/preflight.json').read_text())
        target=LAB/'system-readiness-r1-20261024.qcow2';real_id=g4_capacity.file_id
        for problem in ('missing','identity','content','backing'):
            p=self.procedure();p.CONTROL='prefix-ownership';real_sha=p.sha
            rows=copy.deepcopy(retained['chain']);rows[0]['backing-filename']='/wrong/backing'
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
                 mock.patch.object(p.shutil,'disk_usage',return_value=mock.Mock(free=100*1024**3)), \
                 mock.patch.object(p.subprocess,'run',side_effect=dispatch) as calls:
                with self.assertRaises((RuntimeError,FileNotFoundError)) as caught:p.prepare_ready_storage(capacity)
            self.assertIn({'missing':'missing current top','identity':'identity changed','content':'content changed','backing':'backing metadata changed'}[problem],str(caught.exception))
            self.assertEqual(calls.call_count,1 if problem=='backing' else 0)

    def test_actual_fresh_preparation_pins_exact_probe_nonce_and_has_no_dispatch(self):
        p=self.procedure();p.CONTROL='prefix-ownership';p.NAME=p.BOOT_NAME='prefix-ownership-r1-20261024'
        with tempfile.TemporaryDirectory(prefix='prefix-prep-fixture-',dir=LAB) as temp:
            p.PREP=Path(temp)/'prep';p.RUN=p.BOOT=Path(temp)/p.NAME
            state=dict(safety_top_identity={'path':str(LAB/'system-readiness-r1-20261024.qcow2')},
                safety_top_previously_booted=True,image_creation_authorized=False)
            with mock.patch.object(p,'prepare_ready_storage',return_value=state), \
                 mock.patch.object(p,'prepare_durable_credential',return_value={'public_matches_seed':True}), \
                 mock.patch.object(p,'source_head',return_value='offline-test'), \
                 mock.patch.object(p,'windows_path',return_value='C:\\owned\\controller.ps1'), \
                 mock.patch.object(p.subprocess,'run',side_effect=AssertionError('Forbidden create/network dispatch')) as calls, \
                 mock.patch('builtins.print'):
                p.prepare_idle()
            r=json.loads((p.PREP/'preflight.json').read_text())
            self.assertEqual(r['mode'],'prefix-ownership');self.assertEqual(r['guest_test_argv'],[])
            self.assertEqual(r['ssh_argv'][2],r['ssh_key_path']);self.assertEqual(r['ssh_key_path'],'/var/tmp/wex-g4-lab-credentials/vm_key')
            self.assertEqual(r['probe_sha256'],PROBE_SHA);self.assertEqual(r['probe_code'],ownership.PROBE)
            self.assertRegex(r['probe_nonce'],'^[0-9a-f]{32}$')
            self.assertEqual(r['ownership_probe_argv'],['sudo','-n','python3','-B','-c',ownership.PROBE,PREFIX,r['probe_nonce']])
            self.assertEqual(r['guest_readonly_argv'][-1],r['ownership_probe_argv'])
            self.assertEqual(r['limits']['ownership_stdout_bytes'],65536)
            self.assertNotIn('create_argv',r['storage']);self.assertEqual(len(r['host_helpers_sha256']),8)
            self.assertFalse((p.PREP/'source-stage.tar').exists());calls.assert_not_called()


if __name__=='__main__':unittest.main()
