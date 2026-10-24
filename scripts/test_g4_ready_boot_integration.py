# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual ignored integration functions, no import-time launcher/guest effects."""
import ast
import base64
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import time
import unittest
from unittest import mock

import g4_guest_readiness
import g4_virgl_budget
import stop_g4_virgl_launcher

REPO=Path(__file__).resolve().parents[1]
PRIVATE=REPO/'build/g4-virgl-20261022'
LAB=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')


class Clock:
    def __init__(self):self.value=0
    def now(self):return self.value


class Child:
    def __init__(self,pid,clock,mode='natural',code=0):
        self.pid=pid;self.clock=clock;self.mode=mode;self.code=code
        self.returncode=None;self.terminated=False;self.killed=False;self.waits=[]
    def poll(self):return self.returncode
    def wait(self,timeout):
        self.waits.append(timeout)
        if self.mode!='natural' and not self.terminated or self.mode=='kill' and not self.killed:
            self.clock.value+=timeout;raise subprocess.TimeoutExpired(['owned-child'],timeout)
        self.clock.value+=min(timeout,.01)
        self.returncode=-9 if self.killed else self.code
        return self.returncode
    def terminate(self):self.terminated=True
    def kill(self):self.killed=True


@unittest.skipUnless((PRIVATE/'launch.py').is_file() and LAB.is_dir(),'owned private inputs unavailable')
class ReadyBootIntegration(unittest.TestCase):
    def launcher_functions(self,query):
        tree=ast.parse((PRIVATE/'launch.py').read_text())
        module=ast.Module(body=[n for n in tree.body if isinstance(n,ast.FunctionDef)],type_ignores=[])
        env=dict(Path=Path,time=time,subprocess=subprocess,process_identity=query)
        exec(compile(module,str(PRIVATE/'launch.py'),'exec'),env)
        env['process_identity']=query  # replace extracted real /proc query
        return env

    def shutdown(self,*,protocol=True,mode='natural',code=0,wrong=False,observer_error=False):
        clock=Clock();qemu=Child(1,clock,mode,code);display=Child(2,clock)
        def identity(pid):return dict(start_ticks=(99 if wrong and pid==1 else pid),cmdline=['file=NEW,format=qcow2,if=virtio'])
        fn=self.launcher_functions(identity)['shutdown_owned']
        def observer(path,maximum_seconds):
            self.assertEqual(path,'/owned/qmp.sock');self.assertEqual(maximum_seconds,50)
            clock.value+=50
            if observer_error:raise OSError('original QMP error')
            return dict(protocol_complete=protocol,guest_shutdown_observed=protocol,
                        capabilities_ack=True,powerdown_ack=True,error=None if protocol else 'ACK only')
        result=fn(qemu,display,Path('/owned/qmp.sock'),{'start_ticks':1},{'start_ticks':2},
                  'file=NEW,format=qcow2,if=virtio',observer=observer,now=clock.now)
        return result,qemu,display

    def test_actual_shutdown_requires_event_exit0_no_fallback(self):
        result,_,_=self.shutdown();self.assertTrue(result['graceful_powerdown'])
        for options in ({'protocol':False},{'code':1},{'mode':'forced'}, {'observer_error':True}):
            with self.subTest(options=options):
                result,_,_=self.shutdown(**options);self.assertFalse(result['graceful_powerdown'])

    def test_actual_shutdown_bounded_terminate_kill_and_identity_refusal(self):
        result,qemu,display=self.shutdown(mode='kill')
        self.assertFalse(result['graceful_powerdown']);self.assertTrue(result['fallback_used'])
        self.assertTrue(result['kill_used']);self.assertEqual(qemu.waits,[5,10,5]);self.assertEqual(display.waits,[5])
        self.assertLessEqual(result['shutdown_elapsed_seconds'],75)
        result,qemu,_=self.shutdown(wrong=True)
        self.assertIn('identity changed',result['qemu_cleanup_error'])
        self.assertFalse(qemu.terminated);self.assertFalse(qemu.killed)

    def test_actual_overlay_selection_missing_old_only_and_symlink(self):
        fn=self.launcher_functions(None)['writable_overlay']
        with tempfile.TemporaryDirectory(prefix='ready-overlay-fixture-',dir=LAB) as temp:
            root=Path(temp)
            # Original plain fixture bytes, not a formatted disk/image operation.
            (root/'system-capacity-r1-20261023.qcow2').write_bytes(b'plain test fixture')
            with self.assertRaises(RuntimeError):fn(root)
            new=root/'system-readiness-r1-20261024.qcow2'
            new.symlink_to(root/'system-capacity-r1-20261023.qcow2')
            with self.assertRaises(RuntimeError):fn(root)
            new.unlink();new.write_bytes(b'plain new fixture')
            self.assertEqual(fn(root),new)

    def exercise(self,*,ready=True,unexpected=False,remaining=880,guard=None,mutation=None,busy=False):
        spec=importlib.util.spec_from_file_location('ready_private',PRIVATE/'presentation-timing-trial.py')
        p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p)
        with tempfile.TemporaryDirectory(prefix='ready-integration-',dir=LAB) as temp:
            root=Path(temp);prep=root/'prep';run=root/'run';prep.mkdir();run.mkdir()
            p.LAB=root;p.RUN=p.BOOT=run;p.PREP=prep;p.BOOT_NAME='owned-run';p.CONTROL='native-idle-ready'
            (prep/'seed.iso').write_bytes(b'original seed fixture')
            pins={'/binary/'+str(i):'a'*64 for i in range(19)}
            overlay=str(root/'system-readiness-r1-20261024.qcow2')
            record=dict(ssh_argv=['never-ssh'],guest_test_argv=[],host_helpers_sha256={},
                host_seed_files={},source_manifest={},seed_iso=dict(path=str(prep/'seed.iso'),sha256=p.sha(prep/'seed.iso')),
                known_binary_sha256=pins,current_overlay=overlay,minimum_guest_free_bytes=1024**3,
                probe_sha256='d36b5d1d24f0964c98491263a034e6477f7f6af26a5079cc1244e2b39327aac0')
            if mutation=='helper':record['host_helpers_sha256']={'scripts/g4_guest_readiness.py':'0'*64}
            if mutation=='source':record['source_manifest']={'bad':dict(path=str(prep/'seed.iso'),sha256='0'*64)}
            if mutation=='pins':record['known_binary_sha256']={}
            if mutation=='old-overlay':record['current_overlay']=str(root/'system-capacity-r1-20261023.qcow2')
            (prep/'preflight.json').write_text(json.dumps(record))
            (run/'launcher-identity.json').write_text(json.dumps(dict(qemu_pid=1,qemu_start_ticks=1,xvfb_pid=2,xvfb_start_ticks=2)))
            (root/'owned-run.wsl-session.json').write_text('{}')
            calls=[];guards=[];readiness_calls=[]
            def budget(raw,name,required):
                guards.append(required)
                return dict(passed=required!=guard and remaining>=required,remaining_seconds=remaining,error='test budget')
            def readiness(ssh,maximum_seconds):
                readiness_calls.append(maximum_seconds);calls.append('readiness')
                if unexpected:raise RuntimeError('original unexpected gate error')
                return dict(ready=ready,attempts=[dict(error='raw retained attempt')])
            def dispatch(argv,**kwargs):
                c=shlex.split(argv[-1]);calls.append(c[0])
                if c[0]=='df':out='disk'
                elif c[0]=='python3':out=str(2*1024**3)
                elif c[0]=='sha256sum':out=''.join(sha+'  '+path+'\n' for path,sha in pins.items())
                elif c[:5]==['sudo','-n','python3','-B','-c']:out=b'[123]' if busy else b'[]'
                else:raise AssertionError('FORBIDDEN stage/consumer/archive dispatch: '+repr(c))
                return subprocess.CompletedProcess(argv,0,out,b'')
            with mock.patch.object(g4_virgl_budget,'check_budget',side_effect=budget), \
                 mock.patch.object(g4_guest_readiness,'wait_ready',side_effect=readiness), \
                 mock.patch.object(stop_g4_virgl_launcher,'validate_identity',return_value={'pid':3}), \
                 mock.patch.object(stop_g4_virgl_launcher,'proc_identity',side_effect=lambda pid:dict(
                     start_ticks=pid,cmdline=['qemu','file='+overlay+',format=qcow2,if=virtio'])), \
                 mock.patch.object(p.subprocess,'run',side_effect=dispatch),mock.patch('builtins.print'):
                try:p.run();error=None
                except Exception as caught:error=caught
            info=json.loads((run/'execution.json').read_text())
            self.assertIn('archive_skipped',info)
            return info,calls,guards,readiness_calls,error

    def test_actual_procedure_ready_first_empty_busy_no_staging(self):
        for busy in (False,True):
            info,calls,guards,caps,error=self.exercise(busy=busy)
            self.assertIsNone(error);self.assertEqual(calls,['readiness','df','python3','sha256sum','sudo'])
            self.assertEqual(guards,[674,650,435]);self.assertEqual(caps,[180])
            self.assertEqual(info['observed_owner_list'],[123] if busy else [])
            self.assertIn('success or failure',info['idle_probe_observation']['stdout']['byte_origin'])

    def test_actual_readiness_failure_exception_and_headroom_no_ssh(self):
        for options in ({'ready':False},{'unexpected':True},{'remaining':673}):
            info,calls,guards,_,error=self.exercise(**options)
            self.assertIsNotNone(error);self.assertEqual(guards,[674])
            self.assertEqual(calls,[] if options.get('remaining') else ['readiness'])
            if options.get('unexpected'):self.assertIn('original unexpected gate error',info['readiness_error'])
            elif not options.get('remaining'):self.assertEqual(len(info['readiness']['attempts']),1)

    def test_actual_gate_cap_raw_guards_and_host_input_negatives(self):
        _,_,_,caps,error=self.exercise(remaining=700)
        self.assertIsNone(error);self.assertEqual(caps,[50])
        for guard in (650,435):
            _,calls,_,_,error=self.exercise(guard=guard);self.assertIsNotNone(error)
            self.assertEqual(calls,['readiness'] if guard==650 else ['readiness','df','python3','sha256sum'])
        for mutation in ('helper','source','pins','old-overlay'):
            _,calls,_,_,error=self.exercise(mutation=mutation)
            self.assertIsNotNone(error);self.assertEqual(calls,[])


if __name__=='__main__':unittest.main()
