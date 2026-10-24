# SPDX-License-Identifier: GPL-2.0-or-later
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
import zipfile
from unittest import mock
import test_g4_native_exit_calibration as calibrated

spec=importlib.util.spec_from_file_location('chromium_native',Path(__file__).with_name('check-g4-chromium-native-exit.py'))
r=importlib.util.module_from_spec(spec);spec.loader.exec_module(r)
LAB=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022');NONCE='a'*32
URL='http://127.0.0.1:4321/'+NONCE+'/index.html#nonce='+NONCE

def rows(code=127):
    bound=dict(v=1,event='bound',mode='chromium',nonce=NONCE,helper_pid=32,child_pid=36,creation_time=1234,
        image=r.cal.win(r.RUNTIME/'chrome.exe'),command_line=' '.join('"'+s+'"' for s in r.child_args(NONCE,URL)),
        creation_flags=0,inherit_handles=False,environment='inherited_NULL',manifest_sha256=r.MARKER,manifest_verified_by_native=False,tick_ms=100)
    exit=dict(v=1,event='exit',nonce=NONCE,child_pid=36,creation_time=1234,wait_result=0,exit_code=code,exit_hex=f'0x{code:08x}',tick_ms=1100)
    return bound,exit
def raw(events):return b''.join((json.dumps(v)+'\n').encode() for v in events)

class ParserTests(unittest.TestCase):
    def test_full_status_arbitrary_signaled259_and_exact_normal_contract(self):
        for code in (0,127,259,0xc000007f,2**32-1):
            value=r.parse(raw(rows(code)),NONCE,r.cal.win(r.RUNTIME/'chrome.exe'),URL)
            self.assertEqual(value['native_child_status'],code);self.assertEqual(value['native_child_hex'],f'0x{code:08x}')
        command=r.command(NONCE,URL)
        self.assertEqual(command[2],'--chromium');self.assertEqual(command[3:5],['--image',r.cal.win(r.RUNTIME/'chrome.exe')])
        self.assertFalse(any('sandbox' in s or 'disable-' in s for s in command+r.child_args(NONCE,URL)))
        self.assertEqual(len(r.child_args(NONCE,URL)),8)

    def test_binding_and_terminal_negatives_do_not_invent_status(self):
        for key,value in [('nonce','b'*32),('image','wrong.exe'),('command_line','wrong'),('creation_flags',1),('manifest_verified_by_native',True),('child_pid',True),('mode','calibration')]:
            events=list(rows());events[0]=dict(events[0],**{key:value})
            with self.subTest(key=key),self.assertRaises(ValueError):r.parse(raw(events),NONCE,r.cal.win(r.RUNTIME/'chrome.exe'),URL)
        for key,value in [('creation_time',1235),('child_pid',37),('exit_code',-1),('exit_code',True),('wait_result',258),('exit_hex','0xc000007f'),('tick_ms',60000),('nonce','b'*32)]:
            events=list(rows());events[1]=dict(events[1],**{key:value})
            with self.subTest(key=key),self.assertRaises(ValueError):r.parse(raw(events),NONCE,r.cal.win(r.RUNTIME/'chrome.exe'),URL)
        for data in (b'',raw(rows())[:-1],b'\xff\n',b'[]\n',raw(rows())+b'\n',b'x'*16385):
            with self.assertRaises(ValueError):r.parse(data,NONCE,r.cal.win(r.RUNTIME/'chrome.exe'),URL)

    def test_native_failure_race_and45timeout_are_not_child_codes(self):
        failure=dict(v=1,event='failure',nonce=NONCE,phase='identity_race_before',win32_error=0,child_pid=36,wait_result=0,tick_ms=100)
        value=r.parse(raw([failure]),NONCE,r.cal.win(r.RUNTIME/'chrome.exe'),URL)
        self.assertEqual(value['outcome'],'native_failure');self.assertNotIn('native_child_status',value)
        bound,_=rows();timeout=dict(v=1,event='timeout',nonce=NONCE,child_pid=36,creation_time=1234,wait_result=258,wait_ms=45000,tick_ms=45100)
        value=r.parse(raw([bound,timeout]),NONCE,r.cal.win(r.RUNTIME/'chrome.exe'),URL)
        self.assertEqual(value['outcome'],'native_timeout');self.assertNotIn('native_child_status',value)

@unittest.skipUnless(LAB.is_dir(),'D: private storage unavailable')
class RuntimeTests(unittest.TestCase):
    def test_actual_log_quota_and_missing_journal(self):
        with tempfile.TemporaryDirectory(prefix='native-quota-',dir=LAB) as temp:
            output=Path(temp)
            self.assertEqual(r.cal.journal(output),b'')
            with self.assertRaises(ValueError):r.parse(r.cal.journal(output),NONCE,r.cal.win(r.RUNTIME/'chrome.exe'),URL)
            with (output/'chromium.log').open('wb') as stream:stream.truncate(20*1024**2+1)
            with self.assertRaisesRegex(ValueError,'Chromium log quota'):r.log_quota(output)

    def test_actual_safe_extract_absent_only_and_failed_guard_no_output(self):
        with tempfile.TemporaryDirectory(prefix='native-extract-',dir=LAB) as temp:
            root=Path(temp);runtime=root/'vendor/extracted/chrome-win';manifest=root/'manifest.json';archive=root/'original.zip'
            pe=bytearray(128);pe[:2]=b'MZ';struct.pack_into('<I',pe,60,64);pe[64:68]=b'PE\0\0';struct.pack_into('<H',pe,68,0x8664)
            names=set(r.cache.REQUIRED)|{'D3DCompiler_47.dll','libEGL.dll','libGLESv2.dll'}
            with zipfile.ZipFile(archive,'w') as out:
                for name in names:out.writestr('chrome-win/'+name,bytes(pe) if name.endswith(('.dll','.exe')) else b'original data')
            with zipfile.ZipFile(archive) as data:files={n:{'bytes':len(data.read(n)),'sha256':hashlib.sha256(data.read(n)).hexdigest()} for n in data.namelist()}
            manifest.write_text(json.dumps(dict(files=files,zip_sha256=r.cache.digest(archive))))
            with mock.patch.object(r,'RUNTIME',runtime),mock.patch.object(r,'MANIFEST',manifest),mock.patch.object(r,'LAB',root):
                self.assertEqual(r.runtime_state(zip_path=archive,extract=True)['state'],'complete')
                before=(runtime/'chrome.exe').read_bytes()
                self.assertEqual(r.runtime_state(zip_path=root/'not-read.zip',extract=True)['state'],'complete')
                self.assertEqual((runtime/'chrome.exe').read_bytes(),before)
            helpers=mock.Mock()
            with self.assertRaises(ValueError):r.run(root/'unapproved-output',helpers)
            helpers.configure_gl_driver.assert_not_called();self.assertFalse((root/'unapproved-output').exists())

    def test_actual_shared_collector_native_status_and_early_exit_phase(self):
        for fault in (None,'early','helper_exit','timeout'):
            case=calibrated.RunnerTests('test_mock_success_and_separate_native_linux_before_cleanup');case.setUp()
            try:
                case.output.mkdir();case.fault=fault
                case.raw_edit=lambda _:raw(rows(0xc000007f)) if fault!='timeout' else b''
                contract=dict(operation_seconds=82,nonce=NONCE,image=r.cal.win(r.RUNTIME/'chrome.exe'),
                    command=lambda out,n:r.command(n,URL),binding=lambda v,n,i:r.binding(v,n,i,URL),parse=lambda data,n,i:r.parse(data,n,i,URL))
                result={}
                def cleanup(reason):result.setdefault('cleanup_started',{'reason':reason})
                if fault is None:
                    calibrated.cal.collect(case.output,{},result,1000,cleanup,lambda:None,now=lambda:case.clock,
                        sleep=lambda s:setattr(case,'clock',case.clock+1),popen=case.popen,contract=contract)
                    self.assertTrue(result['observation_complete']);self.assertEqual(result['native']['native_child_status'],0xc000007f)
                else:
                    with self.assertRaises((ValueError,TimeoutError)):
                        calibrated.cal.collect(case.output,{},result,1000,cleanup,lambda:None,now=lambda:case.clock,
                            sleep=lambda s:setattr(case,'clock',case.clock+1),popen=case.popen,contract=contract)
                    self.assertNotIn('observation_complete',result)
                    if fault=='early':self.assertEqual(result['helper_phase'],'natural_exit_before_identity')
                self.assertTrue(result['helper_reaped']);self.assertLessEqual(result['helper_finished_monotonic'],82)
            finally:case.doCleanups()

    def test_absent_reuse_drift_and_partial_refusal(self):
        with tempfile.TemporaryDirectory(prefix='chromium-runtime-',dir=LAB) as temp:
            root=Path(temp);runtime=root/'vendor/extracted/chrome-win';manifest=root/'manifest.json'
            files={f'chrome-win/{name}':{'bytes':128,'sha256':'0'*64} for name in ('chrome.exe','chrome.dll','chrome_elf.dll','D3DCompiler_47.dll','libEGL.dll','libGLESv2.dll')}
            manifest.write_text(json.dumps(dict(files=files,zip_sha256='0'*64)))
            with mock.patch.object(r,'RUNTIME',runtime),mock.patch.object(r,'MANIFEST',manifest):
                self.assertEqual(r.runtime_state()['state'],'absent')
                runtime.parent.mkdir(parents=True)
                with self.assertRaisesRegex(ValueError,'partial runtime'):r.runtime_state()
                runtime.mkdir()
                data=bytearray(128);data[:2]=b'MZ';struct.pack_into('<I',data,60,64);data[64:68]=b'PE\0\0';struct.pack_into('<H',data,68,0x8664)
                for name in files:(runtime.parent/name).write_bytes(data)
                state=r.runtime_state();self.assertEqual(state['state'],'complete')
                self.assertTrue(all(not v['previous_version_tested'] and v['warnings'] for v in state['files'].values()))
                (runtime/'chrome.dll').unlink()
                with self.assertRaisesRegex(ValueError,'partial/extra'):r.runtime_state(extract=True,zip_path=root/'never-used.zip')

    def test_actual_run_callbacks_diagnostic1_even_complete_and_reap_before_prefix(self):
        for fault in (None,'collector','reap','server','quota'):
            with self.subTest(fault=fault),tempfile.TemporaryDirectory(prefix='chromium-native-run-',dir=LAB) as temp:
                root=Path(temp);out=root/'output';prefix=root/'prefix';prefix.mkdir();calls=[]
                env={k:str(out/k.lower()) for k in ('HOME','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','TMPDIR')}
                server=mock.Mock(record={});server.start.return_value=URL
                if fault=='server':server.start.side_effect=RuntimeError('PRIMARY server')
                comp=mock.Mock();comp.start.return_value={};comp.verify_gpu_renderer.return_value={'passed':True}
                def logged(argv,environment,output,label,timeout):
                    calls.append(label)
                    if label.startswith('cleanup-'):self.assertTrue('reaped' in calls or fault=='server')
                    return SimpleNamespace(returncode=0)
                helpers=SimpleNamespace(configure_gl_driver=mock.Mock(),run_logged=logged,matching_servers=mock.Mock(return_value=[]))
                def collect(output,environment,result,deadline,cleanup,monitor,**kw):
                    self.assertEqual(kw['contract']['operation_seconds'],82)
                    self.assertIn('--chromium',kw['contract']['command'](output,NONCE))
                    result.update(helper_started=True,helper_reaped=fault!='reap',nonce=NONCE)
                    if fault=='quota':raise ValueError('PRIMARY quota')
                    if fault=='collector':raise ValueError('PRIMARY parser')
                    if fault=='reap':raise ValueError('PRIMARY identity, SECONDARY reap failed')
                    calls.append('reaped');result.update(observation_complete=True,native={'outcome':'native_exit','native_child_status':259},linux_helper_wait_exit=0)
                # These callbacks exercise actual orchestration, not Windows APIs.
                with mock.patch.object(r,'PREFIX',prefix),mock.patch.object(r,'guards',return_value={'private_loopback':{'same':1}}), \
                     mock.patch.object(r.cal.install,'environment',return_value=env),mock.patch.object(r.cal,'OwnedWexcomp',return_value=comp), \
                     mock.patch.object(r.cal,'verify_native_gl_driver',return_value={'passed':True}),mock.patch.object(r,'FixtureServer',return_value=server), \
                     mock.patch.object(r.cal,'collect',side_effect=collect),mock.patch.object(r.cal.graphics_resources,'sample',return_value={}), \
                     mock.patch.object(r.cal.isolation,'verify_private_loopback',return_value={'same':1}):
                    # A collector failure that has reaped its observer permits cleanup.
                    if fault in ('collector','quota'):
                        base=collect
                        def failed(*args,**kw):
                            try:return base(*args,**kw)
                            finally:calls.append('reaped')
                        r.cal.collect.side_effect=failed
                    code=r.run(out,helpers)
                result=json.loads((out/'results.json').read_text());self.assertEqual(code,1);self.assertFalse(result['passed'])
                if fault is None:self.assertTrue(result['observation_complete'])
                else:self.assertIn('PRIMARY',result['error'])
                if fault=='reap':self.assertNotIn('cleanup-kill',calls)
                self.assertNotIn('raw_environ',json.dumps(result))

if __name__=='__main__':unittest.main()
