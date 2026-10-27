# SPDX-License-Identifier: GPL-2.0-or-later
import json
import contextlib
import io
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock
import g4_userenv_view as view
import g4_userenv_probe as probe
import g4_userenv_r6_probe as probe6
import test_g4_chromium_native_exit as native

LAB=native.LAB
def payload(nonce):return dict(v=1,nonce=nonce,pid=44,module_path=r'C:\WINDOWS\system32\userenv.dll',module_path_normal=True,export_found=True,checks=45,failures=0,passed=True)
def _hr(code):
    v=(0x80070000|code)&0xFFFFFFFF
    return v-0x100000000 if v>=0x80000000 else v
# Canonical R6 success record (v=2): genuine per-step HRESULTs, fresh ownership acquired on a
# single S_OK registration and released by the planned teardown, so no emergency cleanup ran.
def payload6(nonce):return dict(v=2,nonce=nonce,pid=44,userenv_path=r'C:\WINDOWS\system32\userenv.dll',kernelbase_path=r'C:\WINDOWS\system32\kernelbase.dll',paths_normal=True,exports_found=True,checks=21,failures=0,derive_hr=0,initial_lookup_hr=_hr(0x2),register_attempted=True,register_hr=0,owned=True,post_register_lookup_hr=0,duplicate_register_hr=_hr(0xb7),verify_lookup_hr=0,unregister_attempted=True,unregister_hr=0,final_lookup_hr=_hr(0x2),cleanup_attempted=False,cleanup_succeeded=False,passed=True)

class ViewTests(unittest.TestCase):
    def test_actual_validation_and_exact_readonly_commands_before_fixed_exec(self):
        with tempfile.TemporaryDirectory(prefix='view-test-',dir=LAB) as folder:
            root=Path(folder);build=root/'build';stage=root/'stage';build.mkdir();events=[]
            with mock.patch.object(view,'BUILD',build),mock.patch.object(view,'STAGE',stage),mock.patch.object(view,'OUTPUT',root/'output'):
                for s,t,machine in view.selected():
                    s.parent.mkdir(parents=True);t.parent.mkdir(parents=True)
                    data=bytearray(128);data[:2]=b'MZ';struct.pack_into('<I',data,60,64);data[64:68]=b'PE\0\0';struct.pack_into('<H',data,68,machine)
                    s.write_bytes(data);t.write_bytes(b'original')
                def validate(fault=None):
                    link=lambda p:('same' if fault==p.split('/')[-1] else ('self' if '/self/' in p else 'init'))
                    return view.validate(readlink=link,uid=lambda:0,loopback=lambda:{'private':True})
                for fault in ('mnt','net'):
                    with self.assertRaises(ValueError):view.execute(validate_context=lambda:validate(fault),run=lambda *a,**k:events.append('mount'),execv=lambda *a:events.append('exec'),emit=lambda *a,**k:None)
                self.assertEqual(events,[])
                source=view.selected()[0][0];saved=source.read_bytes();source.write_bytes(b'wrong')
                with self.assertRaises(ValueError):view.execute(validate_context=validate,run=lambda *a,**k:events.append('mount'),emit=lambda *a,**k:None)
                source.write_bytes(saved);self.assertEqual(events,[])
                target=view.selected()[0][1];target.unlink();target.symlink_to(source)
                with self.assertRaises(ValueError):view.execute(validate_context=validate,run=lambda *a,**k:events.append('mount'),emit=lambda *a,**k:None)
                target.unlink();target.write_bytes(b'original');self.assertEqual(events,[])
                with self.assertRaises(ValueError):view.validate(uid=lambda:1000)
                with self.assertRaises(RuntimeError):
                    view.execute(validate_context=lambda:view.validate(readlink=lambda p:'self' if '/self/' in p else 'init',uid=lambda:0,loopback=lambda:(_ for _ in ()).throw(RuntimeError('bad loopback proof'))),run=lambda *a,**k:events.append('mount'),emit=lambda *a,**k:None)
                self.assertEqual(events,[])
                def dispatch(command,**kwargs):events.append(command);self.assertTrue(kwargs['check']);self.assertEqual(kwargs['timeout'],2)
                def emit(text,**kwargs):self.assertLess(len(text.encode()),32768);events.append('json');self.assertTrue(kwargs['flush'])
                def execute(path,args):events.append('exec');self.assertEqual(path,'/usr/sbin/runuser');self.assertEqual(args[-3:],['--userenv-r3','--output-dir',str(root/'output')])
                view.execute(validate_context=validate,run=dispatch,execv=execute,emit=emit)
                commands=events[:-2];self.assertEqual(len(commands),6)
                self.assertEqual(commands[:2],[['mount','--bind',str(build),str(build)],['mount','-o','remount,bind,ro',str(build)]])
                for i,(s,t,_) in enumerate(view.selected()):
                    self.assertEqual(commands[2+2*i],['mount','--bind',str(s),str(t)])
                    self.assertEqual(commands[3+2*i],['mount','-o','remount,bind,ro',str(t)])
                    self.assertEqual(t.read_bytes(),b'original')
                self.assertEqual(events[-2:],['json','exec'])
                emitted=[]
                with self.assertRaises(RuntimeError):
                    view.execute(validate_context=validate,run=lambda *a,**k:(_ for _ in ()).throw(RuntimeError('mount failed')),execv=lambda *a:self.fail('No exec after mount failure'),emit=lambda text,**kwargs:emitted.append(text))
                self.assertIn('USERENV_MODULE_VIEW_ERROR ',emitted[0])

    def test_r6_readonly_view_covers_userenv_and_kernelbase_both_arches(self):
        with tempfile.TemporaryDirectory(prefix='view-r6-',dir=LAB) as folder:
            root=Path(folder);build=root/'build';stage=root/'stage';build.mkdir();events=[]
            with mock.patch.object(view,'BUILD',build),mock.patch.object(view,'STAGE',stage),mock.patch.object(view,'OUTPUT',root/'output'),mock.patch.object(view,'MODE','userenv-r6'):
                sel=view.selected()
                self.assertEqual(len(sel),4)
                self.assertEqual(sorted(Path(t).name for _,t,_ in sel),['kernelbase.dll','kernelbase.dll','userenv.dll','userenv.dll'])
                # Target staging dirs carry the bare arch; builtin source dirs are <arch>-windows.
                self.assertEqual(sorted(Path(s).parent.name for s,_,_ in sel),['i386','i386','x86_64','x86_64'])
                self.assertEqual(sorted(Path(t).parent.name for _,t,_ in sel),['i386-windows','i386-windows','x86_64-windows','x86_64-windows'])
                self.assertEqual(sorted(m for _,_,m in sel),[0x14c,0x14c,0x8664,0x8664])
                for s,t,machine in sel:
                    s.parent.mkdir(parents=True,exist_ok=True);t.parent.mkdir(parents=True,exist_ok=True)
                    data=bytearray(128);data[:2]=b'MZ';struct.pack_into('<I',data,60,64);data[64:68]=b'PE\0\0';struct.pack_into('<H',data,68,machine)
                    s.write_bytes(data);t.write_bytes(b'original')
                def validate():
                    link=lambda p:('self' if '/self/' in p else 'init')
                    return view.validate(readlink=link,uid=lambda:0,loopback=lambda:{'private':True})
                def dispatch(command,**kwargs):events.append(command);self.assertTrue(kwargs['check']);self.assertEqual(kwargs['timeout'],2)
                def emit(text,**kwargs):self.assertLess(len(text.encode()),32768);events.append('json');self.assertTrue(kwargs['flush'])
                def execute(path,args):events.append('exec');self.assertEqual(path,'/usr/sbin/runuser');self.assertEqual(args[-3:],['--userenv-r6','--output-dir',str(root/'output')])
                view.execute(validate_context=validate,run=dispatch,execv=execute,emit=emit)
                commands=events[:-2];self.assertEqual(len(commands),10)
                self.assertEqual(commands[:2],[['mount','--bind',str(build),str(build)],['mount','-o','remount,bind,ro',str(build)]])
                for i,(s,t,_) in enumerate(sel):
                    self.assertEqual(commands[2+2*i],['mount','--bind',str(s),str(t)])
                    self.assertEqual(commands[3+2*i],['mount','-o','remount,bind,ro',str(t)])
                    self.assertEqual(t.read_bytes(),b'original')
                self.assertEqual(events[-2:],['json','exec'])

class ProbeTests(unittest.TestCase):
    def test_slow_start_keeps_reap_and_intermediate_cleanup_reserves(self):
        with tempfile.TemporaryDirectory(prefix='probe-reserve-',dir=LAB) as folder:
            root=Path(folder);clock=[0];child=SimpleNamespace(killed=False);result={}
            child.poll=lambda:-9 if child.killed else None
            child.kill=lambda:setattr(child,'killed',True)
            def wait(**kwargs):clock[0]+=5;return -9
            child.wait=wait
            def popen(*args,**kwargs):clock[0]=8;return child
            def logged(*args):
                duration=5 if args[0][-1]=='-k' else 8
                self.assertGreaterEqual(args[-1],duration);clock[0]+=duration
                return SimpleNamespace(returncode=0)
            helpers=SimpleNamespace(run_logged=logged,matching_servers=lambda *args:[])
            with self.assertRaises(TimeoutError):probe.phase(root,{},result,root/'probe',root/'build',root/'prefix',helpers,lambda:None,lambda p:{},1000,popen=popen,now=lambda:clock[0],sleep=lambda s:clock.__setitem__(0,clock[0]+1))
            self.assertTrue(result['probe_reaped']);self.assertNotIn('cleanup_error',result['api_probe'])
            self.assertLessEqual(clock[0],45)

    def _admission_phase_fakes(self,clock):
        popen_calls=[];child=SimpleNamespace(killed=False)
        child.poll=lambda:0
        def wait(**kwargs):clock[0]+=1;return 0
        child.wait=wait
        def popen(argv,**kwargs):
            popen_calls.append(1);kwargs['stdout'].write(json.dumps(payload(argv[-1])).encode());kwargs['stdout'].flush()
            clock[0]+=2;return child
        def logged(argv,*args):
            self.assertGreaterEqual(args[-1],5 if argv[-1]=='-k' else 8);clock[0]+=4 if argv[-1]=='-k' else 5
            return SimpleNamespace(returncode=0)
        helpers=SimpleNamespace(run_logged=logged,matching_servers=lambda *a:[])
        return popen,popen_calls,helpers

    def test_fractional_clock_starts_admit_and_complete_full_phase(self):
        for start in (31.1,15.9,16.1,31.9,32.1,63.9,64.1,127.9,128.1):
            with self.subTest(start=start),tempfile.TemporaryDirectory(prefix='probe-admit-',dir=LAB) as folder:
                output=Path(folder);clock=[start];result={};popen,popen_calls,helpers=self._admission_phase_fakes(clock)
                probe.phase(output,{},result,output/'probe.exe',output/'build',output/'prefix',helpers,lambda:None,lambda p:{},start+100,popen=popen,now=lambda:clock[0],sleep=lambda s:clock.__setitem__(0,clock[0]+1))
                self.assertEqual(popen_calls,[1]);self.assertTrue(result['probe_started']);self.assertTrue(result['probe_reaped'])
                meta=result['api_probe'];self.assertTrue(meta['passed']);self.assertEqual(meta['linux_exit'],0)
                self.assertEqual(meta['started_monotonic'],start);self.assertNotIn('cleanup_error',meta)
                self.assertLessEqual(clock[0],start+45)

    def test_exact_45s_allowance_admits_and_completes(self):
        start=31.1
        with tempfile.TemporaryDirectory(prefix='probe-exact-',dir=LAB) as folder:
            output=Path(folder);clock=[start];result={};popen,popen_calls,helpers=self._admission_phase_fakes(clock)
            probe.phase(output,{},result,output/'probe.exe',output/'build',output/'prefix',helpers,lambda:None,lambda p:{},start+45.0,popen=popen,now=lambda:clock[0],sleep=lambda s:clock.__setitem__(0,clock[0]+1))
            self.assertTrue(result['api_probe']['passed']);self.assertLessEqual(clock[0],start+45)

    def test_short_44p99s_allowance_rejects_before_child(self):
        for start in (0.0,31.1,64.1):
            with self.subTest(start=start),tempfile.TemporaryDirectory(prefix='probe-short-',dir=LAB) as folder:
                output=Path(folder);clock=[start];result={};popen_calls=[]
                def popen(*args,**kwargs):popen_calls.append(1);return SimpleNamespace(poll=lambda:0)
                helpers=SimpleNamespace(run_logged=lambda *a:self.fail('No cleanup after admission rejection'),matching_servers=lambda *a:[])
                with self.assertRaises(TimeoutError) as error:
                    probe.phase(output,{},result,output/'probe.exe',output/'build',output/'prefix',helpers,lambda:None,lambda p:{},start+44.99,popen=popen,now=lambda:clock[0],sleep=lambda s:None)
                self.assertEqual(str(error.exception),'Need full45s API probe/reap/intermediate cleanup allowance')
                self.assertEqual(popen_calls,[]);self.assertIs(result.get('probe_started'),False);self.assertIs(result.get('probe_reaped'),False)

    def test_deadline_exhaustion_during_run_kills_reaps_and_cleans(self):
        start=31.1
        with tempfile.TemporaryDirectory(prefix='probe-exhaust-',dir=LAB) as folder:
            output=Path(folder);clock=[start];result={};calls=[]
            child=SimpleNamespace(killed=False)
            child.poll=lambda:None if not child.killed else 0
            child.kill=lambda:setattr(child,'killed',True)
            def wait(**kwargs):self.assertGreaterEqual(kwargs['timeout'],5);clock[0]+=3;return -9
            child.wait=wait
            def popen(argv,**kwargs):
                kwargs['stdout'].write(json.dumps(payload(argv[-1])).encode());kwargs['stdout'].flush()
                clock[0]=start+30;return child
            def logged(argv,*args):
                calls.append(argv[-1]);self.assertGreaterEqual(args[-1],5 if argv[-1]=='-k' else 8);clock[0]+=4 if argv[-1]=='-k' else 5
                return SimpleNamespace(returncode=0)
            helpers=SimpleNamespace(run_logged=logged,matching_servers=lambda *a:[])
            with self.assertRaises(TimeoutError) as error:
                probe.phase(output,{},result,output/'probe.exe',output/'build',output/'prefix',helpers,lambda:None,lambda p:{},start+100,popen=popen,now=lambda:clock[0],sleep=lambda s:clock.__setitem__(0,clock[0]+1))
            self.assertEqual(str(error.exception),'API probe25s timeout')
            meta=result['api_probe'];self.assertIn('API probe25s timeout',meta['error'])
            self.assertTrue(child.killed);self.assertTrue(result['probe_reaped']);self.assertEqual(calls,['-k','-w'])
            self.assertNotIn('cleanup_error',meta);self.assertLessEqual(clock[0],start+45)

    def test_actual_r3_cli_and_mutual_exclusion(self):
        r=native.r
        try:
            with mock.patch.object(r.sys,'argv',['native','--userenv-r3']),mock.patch.object(r,'load'),mock.patch.object(r,'run',return_value=1) as run:
                self.assertEqual(r.main(),1);self.assertEqual(r.MODE,'userenv-r3')
                self.assertEqual(run.call_args.args[0],r.LAB/'chromium-userenv-r3-20261024')
            with mock.patch.object(r.sys,'argv',['native','--userenv-r3','--module-warn-r2']),contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:r.main()
                self.assertEqual(error.exception.code,2)
        finally:r.configure('r1')

    def test_host_real_result_and_strict_json_negatives(self):
        path=LAB/'userenv-browser-r3-20261024.build/host-probe.stdout.json'
        self.assertTrue(path.is_file());raw=path.read_bytes();nonce='0123456789abcdef0123456789abcdef'
        self.assertTrue(probe.decode(raw,nonce,0)['passed'])
        for key,value in [('pid',True),('module_path',r'Z:\foreign\userenv.dll'),('checks',44),('checks',True),('failures',1),('passed',False),('nonce','b'*32),('export_found',False)]:
            v=payload(nonce);v[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):probe.decode(json.dumps(v).encode(),nonce,0)
        for raw in (b'{}',b'{"v":1,"v":1}',b'NaN',b'x'*4097):
            with self.assertRaises(ValueError):probe.decode(raw,nonce,0)
        with self.assertRaises(ValueError):probe.decode(json.dumps(payload(nonce)).encode(),nonce,1)

    def test_actual_probe_phase_failure_cleanup_and_separate_marker(self):
        for fault in (None,'json','exit','timeout','quota','reap','cleanup','json-cleanup'):
            with self.subTest(fault=fault),tempfile.TemporaryDirectory(prefix='probe-phase-',dir=LAB) as folder:
                output=Path(folder);clock=[0];calls=[];result={}
                child=SimpleNamespace(killed=False)
                child.poll=lambda:None if fault in ('timeout','reap') and not child.killed else (1 if fault=='exit' else 0)
                child.kill=lambda:setattr(child,'killed',True)
                def wait(**kwargs):
                    if fault=='reap':raise TimeoutError('reap PRIMARY retained')
                    return -9 if child.killed else child.poll()
                child.wait=wait
                def popen(argv,**kwargs):
                    v=payload(argv[-1]);v['passed']=fault not in ('json','json-cleanup')
                    kwargs['stdout'].write(b'x'*4097 if fault=='quota' else json.dumps(v).encode());kwargs['stdout'].flush()
                    return child
                def logged(argv,*args):
                    calls.append(argv[-1]);self.assertNotIn('cleanup_started',result)
                    if fault in ('cleanup','json-cleanup'):raise RuntimeError('cleanup rejected')
                    return SimpleNamespace(returncode=0)
                helpers=SimpleNamespace(run_logged=logged,matching_servers=lambda *a:[])
                kwargs=dict(popen=popen,now=lambda:clock[0],sleep=lambda s:clock.__setitem__(0,clock[0]+1))
                args=(output,{},result,output/'probe.exe',output/'build',output/'prefix',helpers,lambda:None,lambda p:{},1000)
                if fault:
                    with self.assertRaises((ValueError,TimeoutError,RuntimeError)):probe.phase(*args,**kwargs)
                else:probe.phase(*args,**kwargs);self.assertTrue(result['api_probe']['passed'])
                self.assertNotIn('cleanup_started',result)
                if fault=='reap':self.assertEqual(calls,[]);self.assertFalse(result['probe_reaped']);self.assertIn('timeout',result['api_probe']['error'])
                else:self.assertTrue(result['probe_reaped'])
                if fault=='json-cleanup':self.assertIn('malformed',result['api_probe']['error']);self.assertIn('cleanup rejected',result['api_probe']['cleanup_error'])

    def test_actual_r4_probe_failure_prevents_browser_and_success_is_ordered(self):
        self.test_actual_runner_probe_failure_prevents_browser_and_success_is_ordered('userenv-r4')

    def test_actual_runner_probe_failure_prevents_browser_and_success_is_ordered(self,mode='userenv-r3'):
        r=native.r
        try:
            r.configure(mode)
            for failed in (True,False):
                with tempfile.TemporaryDirectory(prefix='probe-before-browser-',dir=LAB) as folder:
                    out=Path(folder)/'output';calls=[]
                    env={k:str(out/k.lower()) for k in ('HOME','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','TMPDIR')}
                    comp=mock.Mock();comp.start.return_value={};comp.verify_gpu_renderer.return_value={}
                    server=mock.Mock(record={});server.start.return_value=native.URL
                    helpers=SimpleNamespace(configure_gl_driver=mock.Mock(),matching_servers=lambda *a:[],run_logged=lambda *a:SimpleNamespace(returncode=0))
                    def phase(*args,**kwargs):
                        value=args[2];self.assertNotIn('cleanup_started',value);calls.append('probe')
                        if failed:raise ValueError('PRIMARY API mismatch')
                        value['api_probe']={'passed':True};value['probe_reaped']=True;calls.append('probe-cleanup')
                    def collect(*args,**kwargs):
                        self.assertEqual(calls,['probe','probe-cleanup']);self.assertNotIn('cleanup_started',args[2]);calls.append('browser')
                        args[2].update(helper_started=True,helper_reaped=True,native={'outcome':'native_exit'})
                    with mock.patch.object(r,'guards',return_value={'private_loopback':{}}),mock.patch.object(r.cal.install,'environment',return_value=env),mock.patch.object(r.cal,'OwnedWexcomp',return_value=comp),mock.patch.object(r.cal,'verify_native_gl_driver',return_value={}),mock.patch.object(probe,'phase',side_effect=phase),mock.patch.object(r.cal,'collect',side_effect=collect),mock.patch.object(r,'FixtureServer',return_value=server),mock.patch.object(r.cal.graphics_resources,'sample',return_value={}),mock.patch.object(r.cal.isolation,'verify_private_loopback',return_value={}):
                        self.assertEqual(r.run(out,helpers),1)
                    value=json.loads((out/'results.json').read_text())
                    if failed:self.assertEqual(calls,['probe']);self.assertIn('PRIMARY',value['error']);server.start.assert_not_called()
                    else:self.assertEqual(calls,['probe','probe-cleanup','browser']);self.assertTrue(value['observation_complete'])
        finally:r.configure('r1')

    def test_actual_r6_cli_and_paths(self):
        r=native.r
        try:
            with mock.patch.object(r.sys,'argv',['native','--userenv-r6']),mock.patch.object(r,'load'),mock.patch.object(r,'run',return_value=1) as run:
                self.assertEqual(r.main(),1);self.assertEqual(r.MODE,'userenv-r6')
                self.assertEqual(r.HOST,'chromium-registration-r6-20261026')
                self.assertEqual(r.STAGE,r.LAB/'chromium-registration-r6-20261026-stage')
                self.assertEqual(r.OUTPUT,r.LAB/'chromium-registration-r6-20261026')
                self.assertEqual(r.PROFILE,r.OUTPUT/'profile')
                self.assertEqual(r.PROFILE,r.LAB/'chromium-registration-r6-20261026/profile')
                self.assertEqual(run.call_args.args[0],r.LAB/'chromium-registration-r6-20261026')
            with mock.patch.object(r.sys,'argv',['native','--userenv-r6','--userenv-r4']),contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:r.main()
                self.assertEqual(error.exception.code,2)
        finally:r.configure('r1')

    def test_actual_token_integration_cli_and_paths(self):
        r=native.r
        try:
            with mock.patch.object(r.sys,'argv',['native','--token-integration']),mock.patch.object(r,'load'),mock.patch.object(r,'run',return_value=1) as run:
                self.assertEqual(r.main(),1);self.assertEqual(r.MODE,'token-integration')
                self.assertEqual(r.HOST,'chromium-token-integration-20261027')
                self.assertEqual(r.STAGE,r.LAB/'chromium-token-integration-20261027-stage')
                self.assertEqual(r.OUTPUT,r.LAB/'chromium-token-integration-20261027')
                self.assertEqual(r.PROFILE,r.OUTPUT/'profile')
                self.assertEqual(r.BUILD,r.LAB/'wine-token-integration-20261027/build/wow64')
                self.assertEqual(run.call_args.args[0],r.LAB/'chromium-token-integration-20261027')
            with mock.patch.object(r.sys,'argv',['native','--token-integration','--userenv-r6']),contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:r.main()
                self.assertEqual(error.exception.code,2)
            view.configure('token-integration')
            self.assertEqual(view.MODE,'token-integration')
            self.assertEqual(view.STAGE,view.LAB/'chromium-token-integration-20261027-stage')
            self.assertEqual(view.OUTPUT,view.LAB/'chromium-token-integration-20261027')
            self.assertEqual(view.BUILD,view.LAB/'wine-token-integration-20261027/build/wow64')
            self.assertEqual(len(view.selected()),4)
        finally:
            r.configure('r1')
            view.configure('userenv-r3')

    def test_r6_probe_decode_strict_negatives(self):
        nonce='0123456789abcdef0123456789abcdef'
        self.assertTrue(probe6.decode(json.dumps(payload6(nonce)).encode(),nonce,0)['passed'])
        for key,value in [('pid',True),('userenv_path',r'Z:\foreign\userenv.dll'),('kernelbase_path',r'Z:\foreign\kernelbase.dll'),('paths_normal',False),('exports_found',False),('checks',20),('checks',45),('failures',1),('passed',False),('nonce','b'*32)]:
            v=payload6(nonce);v[key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):probe6.decode(json.dumps(v).encode(),nonce,0)
        for raw in (b'{}',b'{"v":1,"v":1}',b'NaN',b'x'*4097):
            with self.assertRaises(ValueError):probe6.decode(raw,nonce,0)
        with self.assertRaises(ValueError):probe6.decode(json.dumps(payload6(nonce)).encode(),nonce,1)

    def test_actual_r6_probe_failure_prevents_browser_and_success_is_ordered(self):
        r=native.r
        try:
            r.configure('userenv-r6')
            for failed in (True,False):
                with tempfile.TemporaryDirectory(prefix='r6-probe-before-browser-',dir=LAB) as folder:
                    out=Path(folder)/'output';calls=[]
                    env={k:str(out/k.lower()) for k in ('HOME','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','TMPDIR')}
                    comp=mock.Mock();comp.start.return_value={};comp.verify_gpu_renderer.return_value={}
                    server=mock.Mock(record={});server.start.return_value=native.URL
                    helpers=SimpleNamespace(configure_gl_driver=mock.Mock(),matching_servers=lambda *a:[],run_logged=lambda *a:SimpleNamespace(returncode=0))
                    def phase(*args,**kwargs):
                        value=args[2];self.assertNotIn('cleanup_started',value);calls.append('probe')
                        if failed:raise ValueError('PRIMARY R6 metadata mismatch')
                        value['api_probe']={'passed':True};value['probe_reaped']=True;calls.append('probe-cleanup')
                    def collect(*args,**kwargs):
                        self.assertEqual(calls,['probe','probe-cleanup']);self.assertNotIn('cleanup_started',args[2]);calls.append('browser')
                        args[2].update(helper_started=True,helper_reaped=True,native={'outcome':'native_exit'})
                    with mock.patch.object(r,'guards',return_value={'private_loopback':{}}),mock.patch.object(r.cal.install,'environment',return_value=env),mock.patch.object(r.cal,'OwnedWexcomp',return_value=comp),mock.patch.object(r.cal,'verify_native_gl_driver',return_value={}),mock.patch.object(probe6,'phase',side_effect=phase),mock.patch.object(r.cal,'collect',side_effect=collect),mock.patch.object(r,'FixtureServer',return_value=server),mock.patch.object(r.cal.graphics_resources,'sample',return_value={}),mock.patch.object(r.cal.isolation,'verify_private_loopback',return_value={}):
                        self.assertEqual(r.run(out,helpers),1)
                    value=json.loads((out/'results.json').read_text())
                    if failed:self.assertEqual(calls,['probe']);self.assertIn('PRIMARY',value['error']);server.start.assert_not_called()
                    else:self.assertEqual(calls,['probe','probe-cleanup','browser']);self.assertTrue(value['observation_complete'])
        finally:r.configure('r1')

if __name__=='__main__':unittest.main()
