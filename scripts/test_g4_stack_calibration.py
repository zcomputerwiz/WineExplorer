# SPDX-License-Identifier: GPL-2.0-or-later
"""Executable OFFLINE calibration contracts. Mocks are NOT Wine/debugger proof."""
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

from g4_stack_trace import parse
from g4_stack_commands import commands

spec=importlib.util.spec_from_file_location('calibration',Path(__file__).with_name('check-g4-stack-calibration.py'))
c=importlib.util.module_from_spec(spec);spec.loader.exec_module(c)
R={'pid':408,'main_tid':412,'worker_tid':576,'creation_time':123456,'nonce':'a'*32}
IMAGE='Z:\\home\\debian\\wex-g4\\stack-calibration-test-stage\\original\\stack-waiter.exe'


def trace(image=IMAGE):
    # Source-format representative, not captured live WineDbg evidence. IDs in
    # output are HEX; input command generator is decimal. Initial exception is
    # expected and intentionally not bound to either selected thread.
    return ('0198:0240:trace:process: other arbitrary trace number 9999\n'
        'WineDbg attached to pid 0198\nUnhandled exception: breakpoint in 64-bit code\n'
        'Backtrace:\n=>0 0x000000007000 DbgBreakPoint in ntdll (0x000000008000)\n'+
        ''.join(f'\nBacktracing for thread {tid:04x}'+(' (WEXStackMain)' if i==0 else '')+
                f' in process 0198 ({image}):\nBacktrace:\n'
                '=>0 0x000000007000 NtWaitForSingleObject in ntdll (0x000000008000)\n'
                '  1 0x000000007100 WaitForSingleObject in kernelbase (0x000000008100)\n'
                '  2 0x000000007200 main+0x10 in stack-waiter (+0x1200) (0x000000008200)\n'
                for i,tid in enumerate((412,576))))


class ParserTests(unittest.TestCase):
    def test_pinned_source_formats_initial_breakpoint_and_silent_detach(self):
        r=parse(trace(),R,IMAGE)
        self.assertEqual([s['win32_tid'] for s in r['selected_stacks']],[412,576])
        self.assertFalse(r['detach_api_return_proven'])
        self.assertEqual(parse(trace(IMAGE.upper()),R,IMAGE)['attached_win32_pid'],408)
        parse(trace().replace('0x000000008200','0x000000000000'),R,IMAGE)  # optimized AMD64 RBP may be zero

    def test_reject_wrong_basename_full_path_or_placeholder(self):
        for image in ('stack-waiter.exe',IMAGE.replace('test-stage','foreign-stage'),'Process_00000198'):
            with self.subTest(image=image),self.assertRaises(ValueError): parse(trace(image),R,IMAGE)

    def test_other_target_pid_is_headers_not_arbitrary_numbers(self):
        parse(trace()+'0278:trace: arbitrary pid 0012\n',R,IMAGE)
        for text in (trace().replace('attached to pid 0198','attached to pid 0199'),
                     trace().replace('in process 0198','in process 0199'),
                     trace()+'WineDbg attached to pid 9999\n',trace()+'Backtracing for thread malformed\n'):
            with self.assertRaises(ValueError): parse(text,R,IMAGE)

    def test_current_tid_plain_backtrace_is_not_selected_binding(self):
        text=trace().replace(f'Backtracing for thread 0240 in process 0198 ({IMAGE}):','')
        with self.assertRaises(ValueError): parse(text,R,IMAGE)

    def test_missing_wrong_order_duplicate_unknown_tid(self):
        for text in (trace().replace('thread 0240','thread 019c'),trace().replace('thread 019c','thread 0240'),
                     trace().replace('thread 0240','thread 9999'),trace()+'Unknown thread id (0240) in process (0198)\n'):
            with self.assertRaises(ValueError): parse(text,R,IMAGE)

    def test_actual_frames_required_not_headers(self):
        for text in (trace().replace(' in stack-waiter',' in foreign'),trace().replace(' in ntdll',' in other').replace(' in kernelbase',' in other'),
                     trace().replace('  2 0x','  7 0x'),trace().replace('0x000000007200','0x000000000000'),
                     trace().replace('Backtrace:\n',''),trace()+'syntax error\n',trace()+'No symbols found\n'):
            with self.assertRaises(ValueError): parse(text,R,IMAGE)


class ReadinessTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.path=Path(self.tmp.name)/'out';self.path.write_bytes(b'')
        self.child=Mock();self.child.poll.return_value=None;self.clock=0

    def read(self): return c.readiness(self.child,self.path,R['nonce'],30,now=lambda:self.clock,sleep=self.advance)
    def advance(self,seconds): self.clock+=seconds

    def test_explicit_initial_loader_cap_separate_postready_budget(self):
        def load(_):
            self.clock=20;self.path.write_text(json.dumps(R)+'\n')
        r,_=c.readiness(self.child,self.path,R['nonce'],30,now=lambda:self.clock,sleep=load)
        self.assertEqual(r,R);self.assertEqual(c.post_budget(20,now=lambda:22),22)
        with self.assertRaises(TimeoutError): c.post_budget(20,now=lambda:25)
        self.assertEqual(c.LOADER,30);self.assertEqual(c.DEBUGGER,10)

    def test_readiness_malformed_duplicate_trailing_oversized_nonce_and_dead(self):
        for raw in (b'bad\n',(json.dumps(R)+'\ntrailing').encode(),b'x'*4097,
                    json.dumps(dict(R,nonce='b'*32)).encode()+b'\n',
                    (json.dumps(R)[:-1]+',"pid":408}\n').encode(),(json.dumps(R)+'\n\n').encode()):
            self.path.write_bytes(raw)
            with self.subTest(raw=raw[:30]),self.assertRaises((ValueError,json.JSONDecodeError)): self.read()
        self.path.write_text(json.dumps(R)+'\n');self.child.poll.return_value=1
        with self.assertRaises(ValueError): self.read()

    def test_readiness_timeout_no_retry(self):
        with self.assertRaises(TimeoutError): self.read()
        self.assertGreaterEqual(self.clock,30);self.assertLess(self.clock,30.1)

    def test_monitor_aborts_loader_immediately(self):
        with self.assertRaisesRegex(ValueError,'child stderr'):
            c.readiness(self.child,self.path,R['nonce'],30,monitor=Mock(side_effect=ValueError('child stderr quota')))


class IntegrationTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.output=self.root/'stack-calibration-test'
        self.fixture=self.root/'original/stack-waiter.exe';self.fixture.parent.mkdir();self.fixture.write_bytes(b'original mock PE')
        self.pins={'fixture_sha256':c.install.digest(self.fixture),'known_binary_sha256':dict(c.TOOLS)}
        self.child=Mock(pid=100);self.child.poll.return_value=None;self.child.wait.return_value=0
        self.calls=[];self.clock=0;self.fail={}
        self.native=dict(R);self.raw=(json.dumps(R)+'\n').encode()
        self.identity={'pid':100,'start':55}
        self.linux_error=None

    def operation(self,argv,env,output,label,seconds,records,**kwargs):
        self.calls.append((argv,dict(env),label,seconds));self.clock+=.1
        if label in self.fail: raise self.fail[label]
        if label.startswith('verify'): return json.dumps(self.native),'native trace'
        if label=='debugger': return trace(c.win(self.fixture)),''
        if label=='release': self.child.poll.return_value=0
        return '',''

    def execute(self):
        self.output.mkdir(exist_ok=True);r={}
        def ready(*a,**kw):
            (self.output/'child.stdout.log').write_bytes(self.raw)
            return dict(R),self.raw
        with patch.object(c.uuid,'uuid4',return_value=SimpleNamespace(hex=R['nonce'])), \
             patch.object(c.subprocess,'Popen',return_value=self.child),patch.object(c,'process_start',return_value=55), \
             patch.object(c,'readiness',side_effect=ready),patch.object(c,'bounded',side_effect=self.operation), \
             patch.object(c,'linux_identity',return_value=self.identity,side_effect=self.linux_error),patch.object(c,'kill_reap',return_value=0), \
             patch.object(c.install,'digest',side_effect=lambda p:c.TOOLS.get(str(p),self.pins['fixture_sha256'])):
            try: c.calibrate(self.fixture,self.pins,dict(WINEDLLOVERRIDES='d3d11=b',WINE='owned',WINESERVER='ownedserver',WINEPREFIX=str(c.PREFIX)),self.output,r,230,now=lambda:self.clock)
            except Exception as e: r['error']=repr(e)
        return r

    def test_success_exact_commands_argv_policy_and_functional_detach(self):
        r=self.execute();self.assertNotIn('error',r);self.assertTrue(r['functional_detach_proven'])
        self.assertEqual((self.output/'selected-commands.txt').read_text(),commands(R))
        self.assertEqual([v[2] for v in self.calls],['verify-before','debugger','verify-after','release'])
        argv,env,_,seconds=self.calls[1]
        self.assertEqual(argv,[str(c.BUILD/'wine'),str(c.BUILD/'programs/winedbg/x86_64-windows/winedbg.exe'),'--file',c.win(self.output/'selected-commands.txt')])
        self.assertEqual(seconds,10);self.assertEqual(env['WINEDLLOVERRIDES'],'d3d11=b;dbghelp=b')
        self.assertEqual(env['_NT_SYMBOL_PATH'],'');self.assertEqual(env['_NT_ALT_SYMBOL_PATH'],'')
        self.assertEqual(self.calls[-1][0][-2:],['--release',R['nonce']])
        self.assertFalse(r['debugger_trace']['detach_api_return_proven'])

    def test_identity_mismatch_no_attach(self):
        for field,value in [('creation_time',999),('pid',409),('main_tid',413),('worker_tid',577),('nonce','b'*32)]:
            self.native=dict(R,**{field:value});self.calls=[]
            r=self.execute();self.assertIn('identity mismatch',r['error']);self.assertEqual(len(self.calls),1)

    def test_linux_before_failure_and_after_reuse_do_not_release(self):
        for error in (ValueError('foreign prefix/argv/start'),[self.identity,dict(self.identity,start=99)]):
            self.linux_error=error;self.calls=[];self.child.poll.return_value=None
            r=self.execute();self.assertIn('error',r);self.assertNotIn('release',[v[2] for v in self.calls])

    def test_native_after_changed_identity_no_release(self):
        original=self.operation
        def changed(*args,**kw):
            if args[3]=='verify-after': self.native=dict(R,creation_time=999)
            return original(*args,**kw)
        self.operation=changed
        r=self.execute();self.assertIn('identity mismatch',r['error'])
        self.assertNotIn('release',[v[2] for v in self.calls])

    def test_failed_debugger_or_release_no_success_even_if_process_absent(self):
        for label in ('debugger','verify-after','release'):
            self.fail={label:RuntimeError('primary '+label)};self.child.poll.return_value=None;self.calls=[]
            r=self.execute();self.assertIn('primary',r['error']);self.assertNotIn('functional_detach_proven',r)

    def test_child_nonzero_or_dead_before_release_not_detach_proof(self):
        self.child.wait.return_value=3
        self.assertIn('did not exit zero',self.execute()['error'])
        self.child.wait.return_value=0;self.child.poll.return_value=1
        self.assertIn('no longer alive',self.execute()['error'])

    def test_postready_insufficient_refuses_attach(self):
        original=self.operation
        def slow(*args,**kwargs):
            value=original(*args,**kwargs);self.clock+=8;return value
        self.operation=slow
        r=self.execute();self.assertIn('Insufficient separate',r['error'])
        self.assertEqual([v[2] for v in self.calls],['verify-before'])


class OperationTests(unittest.TestCase):
    def test_child_exit_bounded_monitored_and_nonzero_retained(self):
        child=Mock();child.poll.return_value=None;clock=[0];monitor=Mock()
        with self.assertRaises(TimeoutError):
            c.child_exit(child,monitor,now=lambda:clock[0],sleep=lambda n:clock.__setitem__(0,clock[0]+n))
        self.assertGreater(monitor.call_count,1)
        child.poll.return_value=3;child.wait.return_value=3
        self.assertEqual(c.child_exit(child,monitor),3)
    def test_child_log_quota_and_extra_output_checked_during_operation(self):
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp);stdout=output/'child.stdout.log';stderr=output/'child.stderr.log'
            raw=(json.dumps(R)+'\n').encode();stdout.write_bytes(raw);stderr.write_bytes(b'')
            c.child_logs(output,raw)
            for extra,err in ((b'trailing',b''),(b'',b'x'*(c.LOG_LIMIT+1))):
                stdout.write_bytes(raw+extra);stderr.write_bytes(err)
                rows=[]
                with patch.object(c.subprocess,'Popen') as popen:
                    with self.assertRaises(ValueError):
                        c.bounded(['not-executed'],{},output,'guard',2,rows,monitor=lambda:c.child_logs(output,raw))
                    popen.assert_not_called()
            stdout.write_bytes(b'x'*4097);stderr.write_bytes(b'')
            with self.assertRaises(ValueError): c.child_logs(output)

    def test_monitor_during_running_helper_kills_reaps_within_bound(self):
        with tempfile.TemporaryDirectory() as tmp:
            calls=[];rows=[]
            def monitor():
                calls.append(1)
                if len(calls)>2: raise ValueError('child log storm')
            with self.assertRaisesRegex(ValueError,'child log storm'):
                c.bounded([sys.executable,'-c','import time;time.sleep(5)'],dict(os.environ),Path(tmp),'monitor',2,rows,monitor=monitor)
            self.assertEqual(rows[0]['cleanup_exit'],-9);self.assertTrue(rows[0]['within_deadline'])

    def test_real_owned_linux_subprocess_transport_success_and_timeout(self):
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp);rows=[]
            stdout,_=c.bounded([sys.executable,'-c','print("original mock helper")'],dict(os.environ),output,'ok',2,rows)
            self.assertEqual(stdout.strip(),'original mock helper');self.assertEqual(rows[0]['exit'],0)
            with self.assertRaises(TimeoutError): c.bounded([sys.executable,'-c','import time;time.sleep(5)'],dict(os.environ),output,'timeout',1.2,rows)
            self.assertEqual(rows[-1]['cleanup_exit'],-9);self.assertTrue(rows[-1]['within_deadline'])

    def test_log_quota_nonzero_and_cleanup_failure_preserve_primary(self):
        with tempfile.TemporaryDirectory() as tmp:
            rows=[]
            with self.assertRaises(ValueError): c.bounded([sys.executable,'-c','print("x"*1100000)'],dict(os.environ),Path(tmp),'quota',2,rows)
            self.assertIn('quota',rows[-1]['error'])
            with self.assertRaises(RuntimeError): c.bounded([sys.executable,'-c','raise SystemExit(7)'],dict(os.environ),Path(tmp),'exit',2,rows)
            self.assertEqual(rows[-1]['exit'],7)
            with patch.object(c,'kill_reap',side_effect=RuntimeError('secondary')):
                with self.assertRaises(RuntimeError): c.bounded([sys.executable,'-c','raise SystemExit(7)'],dict(os.environ),Path(tmp),'secondary',2,rows)
            self.assertIn('nonzero',rows[-1]['error']);self.assertIn('secondary',rows[-1]['cleanup_error'])

    def test_kill_refuses_reused_pid_or_foreign_group(self):
        child=Mock(pid=9);child.poll.return_value=None
        with patch.object(c,'process_start',return_value=2),patch.object(c.os,'killpg') as kill:
            with self.assertRaises(ValueError): c.kill_reap(child,1,999)
            kill.assert_not_called()
        with patch.object(c,'process_start',return_value=1),patch.object(c.os,'getpgid',return_value=10),patch.object(c.os,'killpg') as kill:
            with self.assertRaises(ValueError): c.kill_reap(child,1,999)
            kill.assert_not_called()


class RunTests(unittest.TestCase):
    def test_success_requires_functional_release_exit_and_same_namespace(self):
        for missing in ('none','functional_detach_proven','release_exit','child_exit','changed_namespace'):
            with self.subTest(missing=missing),tempfile.TemporaryDirectory() as tmp:
                output=Path(tmp)/'stack-calibration-test';helper=Mock();helper.matching_servers.return_value=[]
                helper.run_logged.return_value=SimpleNamespace(returncode=0)
                comp=Mock();comp.start.return_value={};comp.verify_gpu_renderer.return_value={};comp.stop.return_value=None
                def calibrated(f,p,e,o,r,d,**kw):
                    r.update(functional_detach_proven=True,release_exit=0,child_exit=0)
                    r.pop(missing,None)
                with patch.object(c,'guards',return_value=(Path(tmp)/'f',{'known_binary_sha256':{}},{'installed':{},'private_loopback':{'ns':1}})), \
                     patch.object(c,'OwnedWexcomp',return_value=comp),patch.object(c,'verify_native_gl_driver',return_value={}), \
                     patch.object(c,'calibrate',side_effect=calibrated),patch.object(c,'payload',return_value={}), \
                     patch.object(c.isolation,'verify_private_loopback',return_value={'ns':2 if missing=='changed_namespace' else 1}), \
                     patch.object(c.graphics_resources,'sample',return_value={}):
                    self.assertEqual(c.run(output,helper),0 if missing=='none' else 1)

    def test_primary_error_retained_with_each_cleanup_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            output=Path(tmp)/'stack-calibration-test';fixture=Path(tmp)/'f'
            helper=Mock();helper.run_logged.side_effect=RuntimeError('prefix cleanup')
            helper.matching_servers.return_value=[]
            comp=Mock();comp.stop.side_effect=RuntimeError('stop cleanup')
            comp.start.return_value={};comp.verify_gpu_renderer.return_value={}
            with patch.object(c,'guards',return_value=(fixture,{'known_binary_sha256':{}},{'installed':{},'private_loopback':{}})), \
                 patch.object(c,'OwnedWexcomp',return_value=comp),patch.object(c,'verify_native_gl_driver',return_value={}), \
                 patch.object(c,'calibrate',side_effect=RuntimeError('PRIMARY debugger')), \
                 patch.object(c,'payload',side_effect=ValueError('payload cleanup')), \
                 patch.object(c.isolation,'verify_private_loopback',side_effect=ValueError('lo cleanup')), \
                 patch.object(c.graphics_resources,'sample',return_value={}):
                self.assertEqual(c.run(output,helper),1)
            r=json.loads((output/'results.json').read_text());self.assertIn('PRIMARY',r['error'])
            self.assertEqual(len(r['cleanup_errors']),5);self.assertFalse(r['passed'])


class GuardTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        self.output=self.root/'stack-calibration-test';self.stage=self.root/(self.output.name+'-stage')
        self.fixture=self.stage/'original/stack-waiter.exe';self.fixture.parent.mkdir(parents=True);self.fixture.write_bytes(b'PE mock')
        self.prefix=self.root/'retained/prefix';self.prefix.mkdir(parents=True)
        self.edge=self.prefix/'msedge.exe';self.edge.write_bytes(b'pinned mock')
        self.build=self.root/'build';self.tools={str(self.build/'winedbg.exe'):'a'*64,str(self.build/'dbghelp.dll'):'b'*64}
        self.binaries={str(self.build/n):'c'*64 for n in c.CORE}
        self.binaries.update({str(self.root/'labwc'):'d'*64,str(self.edge):c.EDGE_HASH,**self.tools})
        self.pins={'known_binary_sha256':self.binaries,'fixture_sha256':'e'*64}
        self.save()
        patches=[patch.object(c,'LAB',self.root),patch.object(c,'BUILD',self.build),patch.object(c,'PREFIX',self.prefix),
                 patch.object(c,'EDGE',self.edge),patch.object(c,'TOOLS',self.tools),
                 patch.object(c.shutil,'disk_usage',return_value=SimpleNamespace(free=1024**3)),
                 patch.object(c.graphics_resources,'sample',return_value={}),patch.object(c.isolation,'verify_private_loopback',return_value={'ns':1}),
                 patch.object(c.install,'digest',side_effect=lambda p:self.binaries[str(p)]),
                 patch.object(c.install,'pe_metadata',return_value={'sha256':'e'*64,'machine':0x8664}),
                 patch.object(c,'payload',return_value={})]
        for p in patches: p.start();self.addCleanup(p.stop)
        self.helper=Mock();self.helper.matching_servers.return_value=[]

    def save(self): (self.stage/'calibration-pins.json').write_text(json.dumps(self.pins))

    def reject(self):
        with patch.object(c.subprocess,'Popen') as popen,patch.object(c,'OwnedWexcomp') as comp:
            with self.assertRaises((ValueError,FileNotFoundError,KeyError)): c.run(self.output,self.helper)
            popen.assert_not_called();comp.assert_not_called()
            self.assertFalse((self.output/'results.json').exists())
            self.helper.run_logged.assert_not_called()

    def test_valid_complete_inventory(self):
        fixture,pins,approved=c.guards(self.output,self.helper)
        self.assertEqual(fixture,self.fixture);self.assertEqual(len(pins['known_binary_sha256']),19)

    def test_space_idle_namespace_payload_before_any_child(self):
        for obj,field,value in ((c.shutil,'disk_usage',SimpleNamespace(free=1024**3-1)),
                                (c.graphics_resources,'sample',{'100':{}}),
                                (self.helper,'matching_servers',[123])):
            with patch.object(obj,field,return_value=value): self.reject()
        for obj,field in ((c.isolation,'verify_private_loopback'),(c,'payload')):
            with patch.object(obj,field,side_effect=ValueError('bad')): self.reject()

    def test_path_existing_and_symlink_guards(self):
        self.output.mkdir();self.reject();self.output.rmdir()
        target=self.root/'other';target.mkdir();self.output.symlink_to(target,target_is_directory=True)
        self.reject();self.output.unlink()
        self.fixture.unlink();self.fixture.symlink_to(self.edge)
        self.reject()

    def test_missing_tool_wrong_hash_wrong_arch_before_any_child(self):
        with patch.object(c.install,'digest',return_value='f'*64): self.reject()
        with patch.object(c.install,'pe_metadata',side_effect=ValueError('wrong x86')): self.reject()
        with patch.object(c.install,'pe_metadata',return_value={'sha256':'f'*64}): self.reject()
        old=dict(self.binaries);self.pins['known_binary_sha256'].pop(next(iter(self.tools)));self.save();self.reject()
        self.pins['known_binary_sha256']=old;self.pins['known_binary_sha256'][next(iter(self.tools))]='f'*64
        self.save();self.reject()


if __name__=='__main__': unittest.main()
