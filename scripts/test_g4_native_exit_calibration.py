# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic native events + executable runner callbacks; NO Wine/VM execution."""
import base64
import copy
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch
import g4_edge_owned_threads as owned
import test_g4_rejected_identity as identity_tests

spec=importlib.util.spec_from_file_location('exit_cal',Path(__file__).with_name('check-g4-native-exit-calibration.py'))
cal=importlib.util.module_from_spec(spec);spec.loader.exec_module(cal)
REAL_READ_OWNED_STAT=cal.read_owned_stat
HOST_LAB=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')


def events(nonce,image):
    a=dict(v=1,event='bound',mode='calibration',nonce=nonce,helper_pid=32,child_pid=36,creation_time=123456,
        image=image,command_line=' '.join('"'+x+'"' for x in (image,'--exit-status','3221225599','--nonce',nonce)),
        creation_flags=0,inherit_handles=False,environment='inherited_NULL',manifest_sha256=None,
        manifest_verified_by_native=False,tick_ms=100)
    b=dict(v=1,event='exit',nonce=nonce,child_pid=36,creation_time=123456,wait_result=0,
           exit_code=3221225599,exit_hex='0xc000007f',tick_ms=1100)
    return a,b


def raw(rows):return b''.join((json.dumps(v)+'\n').encode() for v in rows)


class ParserTests(unittest.TestCase):
    def setUp(self):
        self.nonce='a'*32;self.image=cal.win(cal.LAB/(cal.NAME+'-stage')/'original/exit-child.exe')
        self.rows=events(self.nonce,self.image)

    def test_full_unsigned_native_code_separate_from_linux_and_exact_bound_exit(self):
        r=cal.parse(raw(self.rows),self.nonce,self.image)
        self.assertEqual(r['native_child_status'],3221225599);self.assertEqual(r['native_child_hex'],'0xc000007f')
        self.assertEqual(r['bound']['helper_pid'],32);self.assertEqual(r['bound']['child_pid'],36)

    def test_missing_partial_extra_timeout_error_duplicate_trailing_nonfinite(self):
        good=raw(self.rows)
        bad=[b'',good[:-1],raw(self.rows[:1]),good+b'\n',good+b'{}\n',raw(self.rows[::-1]),
             good.replace(b'"v": 1',b'"v": 1, "v": 1',1),good.replace(b'123456',b'NaN',1),
             raw((self.rows[0],dict(event='timeout',wait_result=258))),raw((dict(event='failure'),self.rows[1])),
             b'x'*(cal.EVENT_LIMIT+1),good.replace(b'}\n',b'} junk\n',1)]
        for value in bad:
            with self.subTest(value=value[:80]),self.assertRaises(ValueError):cal.parse(value,self.nonce,self.image)

    def test_every_binding_field_schema_identity_flags_and_command(self):
        a,b=self.rows
        for key in a:
            bad=dict(a);bad.pop(key)
            with self.subTest(missing=key),self.assertRaises(ValueError):cal.parse(raw((bad,b)),self.nonce,self.image)
        for key,value in [('v',True),('event','exit'),('mode','chromium'),('nonce','b'*32),('helper_pid',False),
            ('helper_pid',36),('child_pid',0),('child_pid',2**32),('creation_time',False),('creation_time',0),
            ('image','exit-child.exe'),('image',r'Z:\foreign\exit-child.exe'),('command_line',a['command_line']+' --no-sandbox'),
            ('command_line',a['command_line'].replace('"','')),('creation_flags',False),('creation_flags',4),
            ('inherit_handles',True),('environment','modified'),('manifest_sha256','invented'),
            ('manifest_verified_by_native',True),('tick_ms',False),('tick_ms',-1),('tick_ms',2**64),('extra',1)]:
            bad=dict(a);bad[key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):cal.parse(raw((bad,b)),self.nonce,self.image)

    def test_terminal_status_time_identity_and_signaled259_is_wrong_calibration_not_liveness(self):
        a,b=self.rows
        for key in b:
            bad=dict(b);bad.pop(key)
            with self.assertRaises(ValueError):cal.parse(raw((a,bad)),self.nonce,self.image)
        for key,value in [('v',True),('event','timeout'),('nonce','b'*32),('child_pid',32),('child_pid',True),
            ('creation_time',False),('creation_time',654321),('wait_result',False),('wait_result',258),
            ('exit_code',127),('exit_code',False),('exit_code',-1073741697),('exit_code',2**32),
            ('exit_hex','0x0000007f'),('tick_ms',99),('tick_ms',7101),('tick_ms',1e999),('extra',1)]:
            bad=dict(b);bad[key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):cal.parse(raw((a,bad)),self.nonce,self.image)
        bad=dict(b,exit_code=259,exit_hex='0x00000103')
        with self.assertRaisesRegex(ValueError,'status mismatch; signaled259 is not liveness'):
            cal.parse(raw((a,bad)),self.nonce,self.image)


class RunnerTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='native-exit-runner-test-',dir=HOST_LAB);self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.output=self.root/cal.NAME;self.stage=self.root/(cal.NAME+'-stage')
        self.prefix=self.root/'retained/prefix';self.edge=self.prefix/'drive_c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
        self.edge.parent.mkdir(parents=True);self.edge.write_bytes(b'pinned fake retained Edge')
        (self.prefix/'dosdevices').mkdir();(self.prefix/'dosdevices/c:').symlink_to('../drive_c');(self.prefix/'dosdevices/z:').symlink_to('/')
        (self.stage/'original').mkdir(parents=True)
        data=bytearray(128);data[:2]=b'MZ';struct.pack_into('<I',data,60,64);data[64:68]=b'PE\0\0';struct.pack_into('<H',data,68,0x8664)
        self.pes={}
        for name in cal.PES:
            p=self.stage/'original'/name;p.write_bytes(data);self.pes[name]=cal.install.digest(p)
        core={str(cal.BUILD/n):'a'*64 for n in cal.CORE};core[str(self.edge)]='a'*64;core[str(self.root/'labwc')]='a'*64
        self.loader=self.root/'owned-build/loader/wine';self.loader.parent.mkdir(parents=True);self.loader.write_bytes(b'original loader fixture')
        self.pins=dict(known_binary_sha256=core,original_pe_sha256=self.pes,additional_loader_pin=dict(path=str(self.loader),sha256='a'*64))
        cal.save(self.stage/'native-exit-pins.json',self.pins)
        self.clock=0;self.calls=[];self.fault=None;self.polls=0;self.killed=False;self.wait_failure=False;self.raw_edit=lambda v:v
        self.comp=Mock();self.comp.start.return_value={'owned':True};self.comp.verify_gpu_renderer.return_value={'passed':True}
        self.lo=Mock(return_value={'namespace':42,'private':True})
        self.helpers=SimpleNamespace(configure_gl_driver=Mock(),matching_servers=Mock(return_value=[]),run_logged=self.logged)
        self.child=Mock(pid=1234);self.child.poll.side_effect=self.poll;self.child.kill.side_effect=self.kill;self.child.wait.side_effect=self.wait
        self.popen=Mock(side_effect=self.launch)
        installed={'executable':{'path':str(self.edge),'sha256':'a'*64}}
        original_digest=cal.install.digest
        def digest(path):return original_digest(path) if path.is_relative_to(self.stage) else 'a'*64
        patches=[patch.object(cal,'LAB',self.root),patch.object(cal,'PREFIX',self.prefix),patch.object(cal,'EDGE',self.edge),
            patch.object(cal,'EDGE_SHA','a'*64),patch.object(cal,'PES',self.pes),patch.object(cal,'APPROVED_LOADER',self.loader),patch.object(cal,'LOADER_SHA','a'*64),patch.object(cal.install,'digest',side_effect=digest),
            patch.object(cal.install,'installed_evidence',return_value=installed),patch.object(cal.shutil,'disk_usage',return_value=SimpleNamespace(free=2*1024**3)),
            patch.object(cal.graphics_resources,'sample',return_value={}),patch.object(cal.isolation,'verify_private_loopback',self.lo),
            patch.object(cal,'OwnedWexcomp',return_value=self.comp),patch.object(cal,'verify_native_gl_driver',return_value={'passed':True}),
            patch.object(cal,'stat_record',return_value={'start_ticks':99}),patch.object(cal,'read_owned_stat',return_value=b'synthetic stat, parser separately patched'),patch.object(Path,'read_bytes',autospec=True,side_effect=self.read_bytes),
            patch.object(cal,'linux_identity',return_value={'linux_pid':1234,'start_ticks':99,'prefix':str(self.prefix),'argv_style':'array'})]
        self.real_read_bytes=Path.read_bytes
        for p in patches:p.start();self.addCleanup(p.stop)

    def read_bytes(self,path):
        if str(path)=='/proc/1234/stat':return b'synthetic stat, parser separately patched'
        return self.real_read_bytes(path)

    def logged(self,argv,env,output,label,timeout):
        self.calls.append(label)
        self.assertTrue(self.child.wait.called or not self.popen.called,'Reap observer BEFORE prefix teardown')
        if self.fault==label:raise RuntimeError('SECONDARY '+label)
        return SimpleNamespace(returncode=0,stdout='')

    def launch(self,argv,**kwargs):
        self.argv=argv;nonce=argv[-1];image=argv[argv.index('--image')+1]
        rows=events(nonce,image);(self.output/'native-exit.jsonl').write_bytes(self.raw_edit(raw(rows)))
        return self.child

    def poll(self):
        self.polls+=1
        if self.killed:return -9
        if self.fault=='early':return 0
        if self.fault in ('timeout','quota','drift'):return None
        return None if self.polls==1 else 1 if self.fault=='helper_exit' else 0

    def wait(self,**kwargs):
        self.calls.append('helper-wait')
        if self.wait_failure:raise TimeoutError('SECONDARY helper reap failure')
        return -9 if self.killed else 1 if self.fault=='helper_exit' else 0

    def kill(self):self.calls.append('helper-kill');self.killed=True

    def execute(self):
        code=cal.run(self.output,self.helpers,now=lambda:self.clock,sleep=lambda s:setattr(self,'clock',self.clock+max(s,1)),popen=self.popen)
        return code,json.loads((self.output/'results.json').read_text())

    def test_mock_success_and_separate_native_linux_before_cleanup(self):
        code,r=self.execute();self.assertEqual(code,0);self.assertTrue(r['passed'])
        self.assertEqual(r['native']['native_child_status'],3221225599)
        self.assertEqual(r['linux_helper_exit_observed']['code'],0);self.assertTrue(r['linux_helper_exit_observed']['before_cleanup'])
        self.assertLessEqual(r['linux_helper_exit_observed']['monotonic'],r['cleanup_started']['monotonic'])
        self.assertLess(self.calls.index('helper-wait'),self.calls.index('cleanup-kill'))
        self.assertEqual(self.argv[2],'--calibration');self.assertNotIn('chrome.exe',' '.join(self.argv))
        self.assertEqual(r['environment']['WINEPREFIX'],str(self.prefix))

    def test_guards_space_idle_binary_pins_fail_before_any_process_or_finally(self):
        for fault in ('space','idle','pins','pe'):
            with self.subTest(fault=fault):
                if fault=='space':scope=patch.object(cal.shutil,'disk_usage',return_value=SimpleNamespace(free=0))
                elif fault=='idle':scope=patch.object(cal.graphics_resources,'sample',return_value={'foreign':{}})
                elif fault=='pins':scope=patch.object(cal.install,'digest',return_value='b'*64)
                else:scope=patch.object(cal.install,'pe_metadata',side_effect=ValueError('bad original PE'))
                with scope,self.assertRaises(ValueError):self.execute()
                self.assertFalse(self.output.exists());self.popen.assert_not_called();self.comp.start.assert_not_called();self.assertEqual(self.calls,[])

    def test_nonzero_helper_and_early_zero_exit_never_native_success(self):
        self.fault='early';code,r=self.execute();self.assertEqual(code,1)
        self.assertIn('before independent Linux identity',r['error']);self.assertNotIn('native',r)

    def test_helper_nonzero_before_cleanup_retained_not_native_child_status(self):
        self.fault='helper_exit';code,r=self.execute();self.assertEqual(code,1)
        self.assertEqual(r['linux_helper_exit_observed']['code'],1);self.assertIn('helper failed',r['error'])

    def test_timeout_kills_exact_helper_reaps_before_teardown_raw_preserved(self):
        self.fault='timeout';code,r=self.execute();self.assertEqual(code,1);self.assertIn('timeout',r['error'])
        self.assertTrue(r['observer_killed']);self.assertTrue(r['helper_reaped']);self.assertEqual(r['cleanup_started']['reason'],'owned_helper_abort')
        self.assertLess(self.calls.index('helper-kill'),self.calls.index('cleanup-kill'));self.assertTrue((self.output/'native-exit.jsonl').exists())
        self.assertLessEqual(r['helper_finished_monotonic'],cal.OPERATION)

    def test_log_quota_error_and_primary_retained_when_cleanup_fails(self):
        self.fault='cleanup-kill';self.raw_edit=lambda value:value+b'bad trailing'
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('complete bounded native',r['error'])
        self.assertIn('SECONDARY cleanup-kill',str(r['cleanup_errors']))

    def test_quota_while_helper_alive_stops_and_reaps(self):
        self.fault='quota'
        original=self.popen.side_effect
        def start(*a,**kw):
            child=original(*a,**kw)
            with (self.output/'helper.stderr.log').open('wb') as stream:stream.truncate(cal.LOG_LIMIT+1)
            return child
        self.popen.side_effect=start;code,r=self.execute();self.assertEqual(code,1)
        self.assertIn('quota',r['error']);self.assertTrue(r['helper_reaped'])

    def test_linux_identity_drift_is_primary_and_reap_failure_skips_prefix_teardown(self):
        self.fault='drift';self.wait_failure=True
        with patch.object(cal,'linux_identity',side_effect=ValueError('PRIMARY Linux owner drift')):
            code,r=self.execute()
        self.assertEqual(code,1);self.assertIn('PRIMARY Linux owner drift',r['error'])
        self.assertIn('helper reap failure',str(r['cleanup_errors']));self.assertNotIn('cleanup-kill',self.calls)

    def test_private_namespace_drift_and_compositor_failure_fail(self):
        self.lo.side_effect=[{'namespace':42},{'namespace':43}]
        code,r=self.execute();self.assertEqual(code,1);self.assertTrue(r['calibration_observation_complete']);self.assertFalse(r['passed'])

    def test_missing_native_file_still_reaps_and_preserves_failed_results(self):
        self.raw_edit=lambda value:b'';code,r=self.execute();self.assertEqual(code,1);self.assertTrue(r['helper_reaped'])
        self.assertIn('Exactly two',r['error']);self.assertFalse(r['passed'])

    def test_popen_failure_no_child_falsely_claimed_started_or_reaped(self):
        self.popen.side_effect=OSError('PRIMARY Popen failed')
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('PRIMARY Popen failed',r['error'])
        self.assertFalse(r['helper_started']);self.assertFalse(r['helper_reaped'])
        self.child.kill.assert_not_called();self.child.wait.assert_not_called()
        self.assertNotIn('linux_helper_popen',r)

    def test_procstat_failure_cannot_adopt_recovered_pid_or_claim_reaped(self):
        self.fault='drift'
        with patch.object(cal,'stat_record',side_effect=[ValueError('PRIMARY procstat failed'),{'start_ticks':99}]):
            code,r=self.execute()
        self.assertEqual(code,1);self.assertIn('PRIMARY procstat failed',r['error'])
        self.assertTrue(r['helper_started']);self.assertFalse(r['helper_reaped'])
        self.child.kill.assert_not_called();self.assertNotIn('cleanup-kill',self.calls)
        self.assertNotIn('linux_helper_popen',r);self.assertIn('no signal',str(r['cleanup_errors']))

    def test_missing_binding_times_out_before42_and_reaps_exact_helper(self):
        self.fault='timeout';self.raw_edit=lambda value:b''
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('binding30s',r['error'])
        self.assertTrue(r['helper_reaped']);self.assertTrue(r['observer_killed'])
        self.assertLessEqual(r['helper_finished_monotonic'],cal.LOADER+cal.REAP)

    def test_compositor_failure_before_helper_no_unrelated_dispatch(self):
        self.comp.start.side_effect=RuntimeError('PRIMARY compositor')
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('PRIMARY compositor',r['error'])
        self.popen.assert_not_called();self.assertFalse(r['helper_started']);self.assertFalse(r['helper_reaped'])

    def identity_transition(self,*,write_failure=False,reap_failure=False,journal_failure=False):
        proc=self.root/'proc';task=proc/str(self.child.pid);task.mkdir(parents=True)
        identity_tests.write_stat(task/'stat',self.child.pid,99)
        (task/'environ').write_bytes(('WINEPREFIX='+str(self.prefix)+'\0SECRET=hidden\0').encode())
        self.fault='drift';self.wait_failure=reap_failure;self.raw_edit=lambda value:b''
        initial_launch=self.popen.side_effect
        def launch(*args,**kwargs):
            child=initial_launch(*args,**kwargs);(self.output/'native-exit.jsonl').unlink();return child
        self.popen.side_effect=launch;count=0;persisted=[];original_save=cal.save;original_kill=self.kill;original_lstat=Path.lstat
        def identity(proc_ignored,pid,start,prefix,argv,**kwargs):
            nonlocal count
            count+=1
            (task/'cmdline').write_bytes(b'\0'.join(v.encode() for v in argv)+b'\0' if count==1 else b'actual rejected\xff\0')
            self.assertEqual(kwargs['deadline'],cal.OPERATION-cal.REAP)
            return owned.linux_identity(proc,pid,start,prefix,argv,**kwargs)
        def saving(path,value):
            if 'linux_helper_rejected_identity' in value and 'cleanup_started' not in value:
                persisted.append(json.loads(json.dumps(value)))
                if write_failure:raise OSError('SECONDARY diagnostic write failure')
            original_save(path,value)
        def killing():
            self.assertTrue(persisted,'Capture/persist attempted BEFORE helper kill')
            self.assertNotIn('cleanup_started',persisted[0]);self.assertFalse(persisted[0]['helper_reaped'])
            if not journal_failure:self.assertEqual(persisted[0]['native_journal_at_identity_rejection'],{'present':False})
            original_kill()
        def lstat(path,*args,**kwargs):
            if journal_failure and count>=2 and path==self.output/'native-exit.jsonl':raise OSError('SECONDARY journal metadata failure')
            return original_lstat(path,*args,**kwargs)
        self.child.kill.side_effect=killing
        with patch.object(cal,'linux_identity',side_effect=identity),patch.object(cal,'save',side_effect=saving),patch.object(Path,'lstat',autospec=True,side_effect=lstat):code,r=self.execute()
        self.assertEqual(code,1);self.assertIn('LinuxIdentityError',r['error'])
        self.assertNotIn('native',r);self.assertNotIn('calibration_observation_complete',r)
        self.assertEqual(r['linux_helper_first_live'],r['linux_helper_last_live'])
        self.assertEqual(r['linux_helper_rejected_identity']['owned_popen_pid'],self.child.pid)
        self.assertEqual(r['linux_helper_rejected_identity']['fields']['cmdline']['bytes_read'],len(b'actual rejected\xff\0'))
        self.assertEqual(base64.b64decode(r['linux_helper_rejected_identity']['fields']['cmdline']['base64']),b'actual rejected\xff\0')
        return r

    def test_actual_reader_valid_to_invalid_evidence_before_exact_kill_and_reap(self):
        r=self.identity_transition();self.assertTrue(r['helper_reaped']);self.assertTrue(r['observer_killed'])
        self.assertLess(self.calls.index('helper-wait'),self.calls.index('cleanup-kill'))

    def test_rejected_identity_write_failure_cannot_replace_primary_or_skip_reap(self):
        r=self.identity_transition(write_failure=True)
        self.assertTrue(r['helper_reaped']);self.assertIn('SECONDARY diagnostic write failure',str(r['observation_errors']))

    def test_rejected_identity_reap_failure_still_skips_prefix_teardown(self):
        r=self.identity_transition(reap_failure=True)
        self.assertFalse(r['helper_reaped']);self.assertNotIn('cleanup-kill',self.calls)
        self.assertIn('helper reap failure',str(r['cleanup_errors']))

    def test_rejected_identity_journal_metadata_failure_still_persists_primary(self):
        r=self.identity_transition(journal_failure=True)
        self.assertTrue(r['helper_reaped']);self.assertIn('SECONDARY journal metadata failure',str(r['observation_errors']))

    def test_owned_stat_actual_read_cap_deadline_and_secondary_close_error(self):
        with patch.object(cal.os,'open',return_value=999),patch.object(cal.os,'read',return_value=b'x'*4096) as reading,patch.object(cal.os,'close') as closing:
            with self.assertRaisesRegex(ValueError,'stat quota'):REAL_READ_OWNED_STAT(1234,deadline=1,now=lambda:0)
            reading.assert_called_once_with(999,4096);closing.assert_called_once_with(999)
        with patch.object(cal.os,'open',return_value=999),patch.object(cal.os,'read',side_effect=OSError('PRIMARY stat read')),patch.object(cal.os,'close',side_effect=OSError('SECONDARY close')):
            with self.assertRaisesRegex(OSError,'PRIMARY stat read'):REAL_READ_OWNED_STAT(1234,deadline=1,now=lambda:0)
        with patch.object(cal.os,'open') as opening:
            with self.assertRaises(TimeoutError):REAL_READ_OWNED_STAT(1234,deadline=0,now=lambda:0)
            opening.assert_not_called()

    def test_internal_loader_hash_or_noncanonical_pin_fails_before_any_launch(self):
        original=cal.install.digest.side_effect
        with patch.object(cal.install,'digest',side_effect=lambda path:'0'*64 if path==self.loader else original(path)):
            with self.assertRaisesRegex(ValueError,'loader hash'):self.execute()
        self.popen.assert_not_called();self.comp.start.assert_not_called();self.assertFalse(self.output.exists())
        self.loader.unlink();self.loader.symlink_to(self.edge)
        with self.assertRaisesRegex(ValueError,'loader hash'):self.execute()
        self.popen.assert_not_called();self.assertFalse(self.output.exists())


if __name__=='__main__':unittest.main()
