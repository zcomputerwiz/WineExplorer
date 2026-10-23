# SPDX-License-Identifier: GPL-2.0-or-later
import base64
import copy
import hashlib
import json
from pathlib import Path
import shlex
import subprocess
import tempfile
import unittest
from unittest import mock

import g4_virgl_budget
import stop_g4_virgl_launcher
import test_g4_remote_failure as remote_tests


@unittest.skipUnless(remote_tests.PRIVATE.is_file() and remote_tests.LAB.is_dir(), 'private inputs unavailable')
class IdleDiagnosticTests(unittest.TestCase):
    def exercise(self, output=b'[]\n', failure=False, guard=None):
        p = remote_tests.RemoteFailureTests().procedure()
        with tempfile.TemporaryDirectory(prefix='idle-diagnostic-test-',dir=remote_tests.LAB) as temp:
            root = Path(temp); prep=root/'prep'; run=root/'run'
            prep.mkdir();run.mkdir()
            p.PREP,p.RUN,p.BOOT,p.LAB,p.BOOT_NAME=prep,run,run,root,'owned-run'
            p.CONTROL='native-idle-diagnostic'
            (prep/'seed.iso').write_bytes(b'offline seed')
            record=dict(ssh_argv=['never-ssh'],guest_test_argv=[],host_helpers_sha256={},
                host_seed_files={},source_manifest={},seed_iso=dict(path=str(prep/'seed.iso'),sha256=p.sha(prep/'seed.iso')),
                known_binary_sha256={'/bin/wine':'a'*64},current_overlay='/owned/new.qcow2',
                minimum_guest_free_bytes=1024**3,
                probe_sha256='d36b5d1d24f0964c98491263a034e6477f7f6af26a5079cc1244e2b39327aac0')
            (prep/'preflight.json').write_text(json.dumps(record))
            (run/'launcher-identity.json').write_text(json.dumps(dict(qemu_pid=2,qemu_start_ticks=22,xvfb_pid=3,xvfb_start_ticks=33)))
            (root/'owned-run.wsl-session.json').write_text('{}')
            calls,guards,snapshots=[],[],[]
            original_save=p.save
            def save(path,value):
                snapshots.append(copy.deepcopy(value));original_save(path,value)
            def budget(raw,name,required):
                guards.append(required)
                return dict(passed=required!=guard,error='test guard failure')
            def dispatch(argv,**kw):
                command=shlex.split(argv[-1]);calls.append(command)
                if command[0]=='df':return subprocess.CompletedProcess(argv,0,'disk')
                if command[0]=='python3':return subprocess.CompletedProcess(argv,0,str(2*1024**3))
                if command[0]=='sha256sum':return subprocess.CompletedProcess(argv,0,'a'*64+'  /bin/wine\n')
                self.assertEqual(command[:5],['sudo','-n','python3','-B','-c'])
                self.assertEqual(hashlib.sha256(command[5].encode()).hexdigest(),record['probe_sha256'])
                self.assertNotIn('text',kw);self.assertEqual(kw['timeout'],10)
                if failure:raise subprocess.CalledProcessError(1,argv,b'partial',b'\xffactual remote error')
                return subprocess.CompletedProcess(argv,0,output,b'warning\xff')
            with mock.patch.object(p,'save',side_effect=save), \
                 mock.patch.object(p.subprocess,'run',side_effect=dispatch), \
                 mock.patch.object(g4_virgl_budget,'check_budget',side_effect=budget), \
                 mock.patch.object(stop_g4_virgl_launcher,'validate_identity',return_value={'pid':1}), \
                 mock.patch.object(stop_g4_virgl_launcher,'proc_identity',side_effect=lambda pid:dict(
                     start_ticks=22 if pid==2 else 33,cmdline=['qemu','file=/owned/new.qcow2,format=qcow2,if=virtio'])), \
                 mock.patch('builtins.print'):
                try:p.run();error=None
                except Exception as caught:error=caught
            info=json.loads((run/'execution.json').read_text())
            self.assertIn('archive_skipped',info)
            self.assertEqual([c[0] for c in calls],[] if guard==650 else
                ['df','python3','sha256sum'] if guard==435 else ['df','python3','sha256sum','sudo'])
            return info,error,guards,snapshots

    def test_empty_and_busy_success_never_stage_or_consume(self):
        for raw,owners in ((b'[]\n',[]),(b'[123]\n',[123])):
            with self.subTest(raw=raw):
                info,error,guards,snapshots=self.exercise(raw)
                self.assertIsNone(error);self.assertEqual(guards,[650,435])
                self.assertEqual(info['observed_owner_list'],owners)
                record=info['idle_probe_observation']
                self.assertEqual(record['returncode'],0);self.assertNotIn('exception_type',record)
                self.assertEqual(base64.b64decode(record['stdout']['base64']),raw)
                self.assertEqual(base64.b64decode(record['stderr']['base64']),b'warning\xff')
                early=next(s for s in snapshots if 'idle_probe_observation' in s)
                self.assertNotIn('observed_owner_list',early)

    def test_failed_probe_retains_actual_error_no_followup(self):
        info,error,guards,_=self.exercise(failure=True)
        self.assertIsInstance(error,subprocess.CalledProcessError)
        self.assertEqual(guards,[650,435])
        self.assertEqual(base64.b64decode(info['remote_failures'][0]['stderr']['base64']),b'\xffactual remote error')

    def test_both_guard_failures_block_probe(self):
        for guard in (650,435):
            info,error,guards,_=self.exercise(guard=guard)
            self.assertIsInstance(error,RuntimeError)
            self.assertEqual(guards,[650] if guard==650 else [650,435])
            self.assertNotIn('idle_probe_observation',info)

    def test_malformed_success_is_retained_before_parse_failure(self):
        info,error,_,_=self.exercise(b'not JSON\xff')
        self.assertIsNotNone(error)
        self.assertEqual(base64.b64decode(info['idle_probe_observation']['stdout']['base64']),b'not JSON\xff')

    def test_commands_explicitly_have_no_consumer(self):
        p=remote_tests.RemoteFailureTests().procedure();p.CONTROL='native-idle-diagnostic'
        ssh,consumer=p.commands(Path('/private/key'))
        self.assertEqual(consumer,[]);self.assertEqual(ssh[0],'ssh')


if __name__=='__main__':unittest.main()
