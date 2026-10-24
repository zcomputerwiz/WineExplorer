# SPDX-License-Identifier: GPL-2.0-or-later
import base64
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

import g4_guest_readiness as readiness

NONCE='a'*32
BOOT='fe2c0409-10e4-4cd2-949b-a61b19c5a757'
SSH=['ssh','-i','/private/key','-o','BatchMode=yes','-o','ConnectTimeout=5',
     '-o','StrictHostKeyChecking=yes','-o','UserKnownHostsFile=/private/known_hosts',
     '-p','2235','debian@127.0.0.1']
LAB=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')


def encoded(data):return base64.b64encode(data).decode()


def response():
    def query(data):return dict(returncode=0,stdout=encoded(data),stderr='')
    return dict(nonce=NONCE,uid=1000,username='debian',boot_id=BOOT,queries=dict(
        cloud_init=query(json.dumps(dict(status='done',errors=[],recoverable_errors={})).encode()),
        systemd=query(b'running\n'),power_handler=query(b'LoadState=not-found\n')))


def attempt_record(value=None,code=0,stderr=b'',error=None):
    raw=json.dumps(response() if value is None else value).encode() if not isinstance(value,bytes) else value
    row=dict(argv=['fake'],returncode=code,reaped=True,within_deadline=True,
             stdout=dict(base64=encoded(raw),observed_bytes=len(raw),truncated=False),
             stderr=dict(base64=encoded(stderr),observed_bytes=len(stderr),truncated=False))
    if error:row['error']=error
    return row


class Clock:
    def __init__(self):self.value=0;self.sleeps=[]
    def now(self):return self.value
    def sleep(self,n):self.sleeps.append(n);self.value+=n


class ReadinessTests(unittest.TestCase):
    def gate(self,records,maximum=30):
        clock=Clock();calls=[];pending=list(records)
        def attempt(argv,seconds):
            calls.append((argv,seconds));clock.value+=min(.1,seconds)
            return pending.pop(0) if pending else attempt_record(b'',255,b'Connection timed out during banner exchange')
        result=readiness.wait_ready(SSH,nonce=NONCE,maximum_seconds=maximum,
            now=clock.now,sleep=clock.sleep,attempt=attempt)
        return result,calls,clock

    def test_two_same_boot_replies_required(self):
        result,calls,clock=self.gate([attempt_record(),attempt_record()])
        self.assertTrue(result['ready']);self.assertEqual(result['boot_id'],BOOT)
        self.assertEqual(len(calls),2);self.assertEqual(clock.sleeps,[2])
        self.assertEqual(calls[0][0][:-1],SSH)
        import shlex
        command=shlex.split(calls[0][0][-1])
        self.assertEqual(command,['python3','-B','-c',readiness.PROBE,NONCE])
        self.assertTrue(all(2<s<=10 for _,s in calls))

    def test_single_reply_or_banner_is_not_ready_and_bounded(self):
        for records in ([attempt_record()], [attempt_record(b'SSH-2.0-OpenSSH',255)],
                        [attempt_record(b'',255,b'Connection timed out during banner exchange')]):
            result,calls,clock=self.gate(records,maximum=7)
            self.assertFalse(result['ready']);self.assertLessEqual(result['elapsed_seconds'],7)
            self.assertTrue(clock.sleeps);self.assertLess(len(calls),5)

    def test_whole_deadline_refuses_late_second_reply(self):
        clock=Clock();calls=[]
        def attempt(argv,seconds):
            calls.append(seconds)
            clock.value += .1 if len(calls)==1 else 9
            return attempt_record()
        result=readiness.wait_ready(SSH,nonce=NONCE,maximum_seconds=8,
            now=clock.now,sleep=clock.sleep,attempt=attempt)
        self.assertFalse(result['ready']);self.assertIn('deadline',result['error'])
        self.assertEqual(len(calls),2);self.assertAlmostEqual(calls[1],5.9)

    def test_auth_and_host_key_failures_terminal(self):
        for text in (b'Permission denied (publickey)',b'Host key verification failed.',
                     b'WARNING REMOTE HOST IDENTIFICATION HAS CHANGED'):
            result,calls,_=self.gate([attempt_record(b'',255,text),attempt_record(),attempt_record()])
            self.assertFalse(result['ready']);self.assertEqual(len(calls),1)
            self.assertIn('authentication/host-key',result['error'])
            self.assertEqual(base64.b64decode(result['attempts'][0]['stderr']['base64']),text)

    def test_identity_nonce_uuid_schema_failures_never_adopted(self):
        for key,values in dict(nonce=['b'*32,True],uid=[0,1001,True,1000.0],username=['root',None],
                               boot_id=['',BOOT.upper(),'0'*32,'00000000-0000-0000-0000-000000000000']).items():
            for bad in values:
                item=response();item[key]=bad
                result,calls,_=self.gate([attempt_record(item),attempt_record(),attempt_record()])
                self.assertFalse(result['ready']);self.assertEqual(len(calls),1)

    def test_boot_change_terminal_even_while_starting(self):
        initial=response();initial['queries']['systemd']['stdout']=encoded(b'starting\n')
        changed=response();changed['boot_id']='11111111-1111-4111-8111-111111111111'
        result,calls,_=self.gate([attempt_record(initial),attempt_record(changed),attempt_record(changed)])
        self.assertFalse(result['ready']);self.assertEqual(len(calls),2)
        self.assertIn('boot changed',result['error'])

    def test_starting_resets_consecutive_but_can_later_become_ready(self):
        item=response();item['queries']['systemd']['stdout']=encoded(b'starting\n')
        item['queries']['systemd']['returncode']=1
        result,calls,_=self.gate([attempt_record(),attempt_record(item),attempt_record(),attempt_record()])
        self.assertTrue(result['ready']);self.assertEqual(len(calls),4)
        item=response();item['queries']['cloud_init']['stdout']=encoded(json.dumps(dict(status='running',errors=[],recoverable_errors={})).encode())
        result,calls,_=self.gate([attempt_record(item),attempt_record(),attempt_record()])
        self.assertTrue(result['ready']);self.assertEqual(len(calls),3)

    def test_cloud_errors_degraded_and_query_types(self):
        items=[]
        for state in ('degraded','maintenance','stopping','unknown'):
            item=response();item['queries']['systemd']['stdout']=encoded(state.encode());items.append(item)
        for cloud in (dict(status='done',errors=['failure'],recoverable_errors={}),
                      dict(status='done',errors=[],recoverable_errors={'WARNING':['bad']}),
                      dict(status='done',errors=None,recoverable_errors={}),dict(status='error',errors=[],recoverable_errors={})):
            item=response();item['queries']['cloud_init']['stdout']=encoded(json.dumps(cloud).encode());items.append(item)
        for field,value in (('returncode',True),('stdout','!'),('stderr',3)):
            item=response();item['queries']['cloud_init'][field]=value;items.append(item)
        for item in items:
            result,calls,_=self.gate([attempt_record(item)])
            self.assertFalse(result['ready']);self.assertEqual(len(calls),1)

    def test_malformed_duplicate_partial_oversize_and_nonfinite(self):
        raw=json.dumps(response()).encode()
        for data in (b'SSH-2.0 banner',raw[:-1],b'[]',raw.replace(b'"uid": 1000',b'"uid": 1000,"uid": 1000'),
                     raw.replace(b'1000',b'NaN'),b'x'*16385,raw+b' trailing'):
            result,calls,_=self.gate([attempt_record(data)])
            self.assertFalse(result['ready']);self.assertEqual(len(calls),1)
        item=response();item['extra']=1
        with self.assertRaises(ValueError):readiness.decode_response(json.dumps(item).encode(),NONCE)

    def test_cleanup_failure_preserves_primary_evidence(self):
        row=attempt_record(b'partial',-9,b'original stderr',error='original timeout')
        row.update(cleanup_error='reap failed',reaped=False)
        result,calls,_=self.gate([row,attempt_record()])
        self.assertFalse(result['ready']);self.assertEqual(len(calls),1)
        self.assertEqual(result['attempts'][0]['error'],'original timeout')
        self.assertEqual(result['attempts'][0]['cleanup_error'],'reap failed')

    def test_strict_prefix_and_numeric_limits(self):
        for index,value in ((0,'fake'),(2,''),(4,'BatchMode=no'),(8,'StrictHostKeyChecking=no'),
                            (10,'UserKnownHostsFile='),(12,'22'),(13,'root@127.0.0.1')):
            prefix=SSH.copy();prefix[index]=value
            with self.assertRaises(ValueError):readiness.wait_ready(prefix)
        for field,value in (('maximum_seconds',181),('maximum_seconds',True),('maximum_seconds',float('inf')),
                            ('maximum_seconds',float('nan')),('interval_seconds',0),('nonce','bad')):
            with self.assertRaises(ValueError):readiness.wait_ready(SSH,**{field:value})

    @unittest.skipUnless(os.name=='posix' and LAB.is_dir(),'requires Linux/D: private storage')
    def test_real_original_host_timeout_kill_reap_no_pid_left(self):
        with tempfile.TemporaryDirectory(prefix='readiness-timeout-',dir=LAB) as temp:
            row=readiness.run_attempt([sys.executable,'-B','-c',
                'import os,time;print(os.getpid(),flush=True);time.sleep(30)'],3)
            self.assertIn('deadline',row['error']);self.assertTrue(row['reaped'])
            self.assertEqual(row['returncode'],-9);self.assertTrue(row['within_deadline'])
            pid=int(base64.b64decode(row['stdout']['base64']).strip())
            self.assertEqual(pid,row['pid']);self.assertFalse(Path('/proc',str(pid)).exists())
            (Path(temp)/'timeout.json').write_text(json.dumps(row))

    @unittest.skipUnless(os.name=='posix' and LAB.is_dir(),'requires Linux/D: private storage')
    def test_real_original_host_output_quota_and_success(self):
        with tempfile.TemporaryDirectory(prefix='readiness-output-',dir=LAB):
            row=readiness.run_attempt([sys.executable,'-B','-c','print("x"*20000,flush=True)'],3)
            self.assertIn('quota',row['error']);self.assertTrue(row['reaped']);self.assertTrue(row['stdout']['truncated'])
            row=readiness.run_attempt([sys.executable,'-B','-c','print("ok")'],3)
            self.assertNotIn('error',row);self.assertEqual(row['returncode'],0)
            self.assertEqual(base64.b64decode(row['stdout']['base64']),b'ok\n')

    def test_popen_and_reap_failures_not_false_success(self):
        with mock.patch.object(readiness.subprocess,'Popen',side_effect=OSError('spawn denied')):
            row=readiness.run_attempt(['not-executed'],3)
        self.assertIn('spawn denied',row['error']);self.assertNotIn('reaped',row)
        child=mock.Mock();child.pid=123;child.stdout=mock.Mock();child.stderr=mock.Mock()
        child.poll.return_value=None;child.returncode=None;child.wait.side_effect=TimeoutError('failed reap')
        with mock.patch.object(readiness.subprocess,'Popen',return_value=child), \
             mock.patch.object(readiness.os,'set_blocking',side_effect=RuntimeError('primary pipe error')), \
             mock.patch.object(readiness.os,'killpg') as kill:
            row=readiness.run_attempt(['not-executed'],3)
        self.assertIn('primary pipe error',row['error']);self.assertIn('failed reap',row['cleanup_error'])
        self.assertFalse(row['reaped']);kill.assert_called_once()

    @unittest.skipUnless(os.name=='posix' and LAB.is_dir(),'requires Linux/D: private storage')
    def test_actual_embedded_probe_with_original_local_query_children(self):
        value,children=self.execute_probe()
        state=readiness.decode_response(value,NONCE)
        self.assertTrue(state['ready']);self.assertEqual(len(children),3)
        self.assertTrue(all(p.returncode==0 for p in children))

    @unittest.skipUnless(os.name=='posix' and LAB.is_dir(),'requires Linux/D: private storage')
    def test_actual_embedded_probe_query_timeout_and_quota_reap(self):
        for mode in ('timeout','quota'):
            with self.subTest(mode=mode):
                _,children=self.execute_probe(mode)
                self.assertEqual(len(children),1)
                self.assertIsNotNone(children[0].returncode)
                self.assertFalse(Path('/proc',str(children[0].pid)).exists())

    def execute_probe(self,mode='healthy'):
        real_popen=subprocess.Popen;children=[];out=io.StringIO()
        with tempfile.TemporaryDirectory(prefix='readiness-probe-',dir=LAB) as temp:
            def original_query(argv,**kwargs):
                if argv[0]=='cloud-init':
                    data=json.dumps(dict(status='done',errors=[],recoverable_errors={})).encode()
                elif argv[:2]==['systemctl','is-system-running']:data=b'running\n'
                else:
                    self.assertEqual(argv[:3],['systemctl','show','systemd-logind.service'])
                    data=b'LoadState=not-found\n'
                code='import sys;sys.stdout.buffer.write('+repr(data)+');sys.stdout.flush()'
                if mode=='timeout':code='import time;time.sleep(30)'
                if mode=='quota':code='import sys;sys.stdout.write("x"*5000);sys.stdout.flush()'
                p=real_popen([sys.executable,'-B','-c',code],cwd=temp,**kwargs);children.append(p);return p
            real_read=Path.read_text
            def read(path,*a,**kw):
                if str(path)=='/proc/sys/kernel/random/boot_id':return BOOT+'\n'
                return real_read(path,*a,**kw)
            with mock.patch.object(subprocess,'Popen',side_effect=original_query), \
                 mock.patch.object(sys,'argv',['original-probe',NONCE]), \
                 mock.patch.object(os,'getuid',return_value=1000), \
                 mock.patch('pwd.getpwuid',return_value=SimpleNamespace(pw_name='debian')), \
                 mock.patch.object(Path,'read_text',read),contextlib.redirect_stdout(out):
                if mode=='healthy':exec(readiness.PROBE,{})
                else:
                    with self.assertRaises((TimeoutError,ValueError)):exec(readiness.PROBE,{})
            return out.getvalue().encode(),children


if __name__=='__main__':unittest.main()
