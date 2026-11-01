# SPDX-License-Identifier: GPL-2.0-or-later
"""Executable installer-only tests: no Wine, guest, browser or vendor execution."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location('edge_install', Path(__file__).with_name('check-g4-edge-install.py'))
edge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(edge)


class InstallTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.output = self.root/'edge-install-test'
        vendor = self.root/'edge-install-test-stage/vendor'
        vendor.mkdir(parents=True)
        self.package = vendor/edge.PACKAGE
        self.package.write_bytes(b'fake vendor package, never executed')
        self.provenance = vendor/'acquisition.json'
        sha = hashlib.sha256(self.package.read_bytes()).hexdigest()
        self.record = dict(status='ACQUIRED_HASH_AND_MICROSOFT_SIGNATURE_VERIFIED_NOT_EXECUTED',
            version=edge.VERSION, payload=edge.PACKAGE, hashPassed=True, expectedSha256=sha,
            actualSha256=sha, payloadBytes=self.package.stat().st_size,
            consent=dict(statement='Yes, accept and test privately', termsUrl='https://www.microsoft.com/en-us/edge/server/download'),
            authenticode=dict(status='Valid', signer=dict(thumbprintSha1=edge.SIGNER)),
            payloadHttp=dict(finalHost='msedge.sf.dl.delivery.mp.microsoft.com'))
        self.provenance.write_text(json.dumps(self.record))
        for name, value in [('LAB',self.root), ('SHA256',sha), ('BYTES',self.package.stat().st_size)]:
            p = patch.object(edge,name,value); p.start(); self.addCleanup(p.stop)
        self.disk = patch.object(edge.shutil,'disk_usage',return_value=SimpleNamespace(free=edge.MIN_FREE))
        self.disk.start(); self.addCleanup(self.disk.stop)
        self.clock = 0
        self.calls = []
        self.codes = {}
        self.failures = {}
        self.payload = True
        self.registration = True
        self.machine = 0x8664
        self.helpers = SimpleNamespace(configure_gl_driver=Mock(), run_logged=self.logged,
                                       matching_servers=Mock(return_value=[]))
        self.comp = Mock()
        self.comp.start.return_value = {'owned':True}
        self.comp.verify_gpu_renderer.return_value = {'passed':True}

    def make_payload(self):
        prefix = self.output/'prefix'
        path = prefix/'drive_c/Program Files/Microsoft/Edge/Application/msedge.exe'
        path.parent.mkdir(parents=True, exist_ok=True)
        data = bytearray(128); data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 64)
        data[64:68] = b'PE\0\0'; struct.pack_into('<H',data,68,self.machine)
        path.write_bytes(data)
        if self.registration:
            (prefix/'system.reg').write_text('[Software\\\\Microsoft\\\\Windows\\\\CurrentVersion\\\\Uninstall\\\\Microsoft Edge]\n'
                '"DisplayName"="Microsoft Edge"\n"DisplayVersion"="'+edge.VERSION+'"\n')

    def logged(self, argv, env, output, label, timeout):
        self.calls.append((argv,dict(env),label,timeout))
        self.clock += 1
        if label in self.failures:
            raise self.failures[label]
        if label == 'installer' and self.payload:
            self.make_payload()
        return SimpleNamespace(returncode=self.codes.get(label,0))

    def execute(self, **kwargs):
        with patch.object(edge,'OwnedWexcomp',return_value=self.comp), \
             patch.object(edge,'verify_native_gl_driver',return_value={'passed':True}), \
             patch.object(edge.graphics_resources,'sample',return_value={}):
            code = edge.run(self.package,self.provenance,self.output,self.helpers,now=lambda:self.clock,**kwargs)
        return code,json.loads((self.output/'results.json').read_text())

    def test_success_command_environment_deadlines_and_registration_source(self):
        with patch.dict(edge.os.environ, {'HOME':'/unowned','WINEPREFIX':'/unowned','DISPLAY':':9','LD_PRELOAD':'bad'}):
            code,result = self.execute()
        self.assertEqual(code,0)
        self.assertTrue(result['passed'])
        self.assertEqual(result['installed']['version_source'],'registration')
        self.assertFalse(result['installed']['independent_pe_file_version_verified'])
        self.assertEqual(result['installed']['executable']['machine'],0x8664)
        argv,env,label,timeout = self.calls[1]
        win = lambda p: 'Z:'+str(p).replace('/','\\')
        self.assertEqual(argv,[str(edge.BUILD/'wine'),'msiexec','/i',win(self.package),'/qn','/norestart','/l*v',win(self.output/'install-msi.log')])
        self.assertEqual([c[3] for c in self.calls],[90,120,10,10])
        self.assertEqual(env['WINEPREFIX'],str(self.output/'prefix'))
        self.assertNotIn('DISPLAY',env); self.assertNotIn('LD_PRELOAD',env)
        for key in ('HOME','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','TMPDIR'):
            self.assertTrue(Path(env[key]).is_relative_to(self.output))
        self.assertEqual(result['remaining_prefix_processes'],{})
        self.comp.verify_gpu_renderer.assert_called_once_with('virgl')
        self.comp.stop.assert_called_once()

    def test_guards_reject_before_any_process_or_finally(self):
        cases = ['hash','consent','signature','version','space','existing','wrong-path']
        for case in cases:
            with self.subTest(case=case):
                record = dict(self.record)
                if case == 'consent': record['consent'] = {}
                if case == 'signature': record['authenticode'] = {}
                if case == 'version': record['version'] = 'wrong'
                self.provenance.write_text(json.dumps(record))
                free = 0 if case == 'space' else edge.MIN_FREE
                if case == 'existing': self.output.mkdir()
                if case == 'hash': self.package.write_bytes(b'tampered')
                with patch.object(edge.shutil,'disk_usage',return_value=SimpleNamespace(free=free)), \
                     patch.object(edge,'OwnedWexcomp') as comp, patch.object(edge.subprocess,'Popen') as popen:
                    with self.assertRaises((ValueError,FileNotFoundError)):
                        edge.run(self.package,self.provenance,self.output if case!='wrong-path' else self.root/'other',self.helpers)
                    comp.assert_not_called(); popen.assert_not_called()
                    self.assertEqual(self.calls,[])
                if self.output.exists(): self.output.rmdir()
                self.package.write_bytes(b'fake vendor package, never executed')

    def test_boot_failure_never_installs(self):
        self.codes['wineboot']=1
        code,r=self.execute()
        self.assertEqual(code,1); self.assertFalse(r['installer_attempted'])
        self.assertEqual([c[2] for c in self.calls],['wineboot','cleanup-kill','cleanup-wait'])

    def test_installer_failure(self):
        self.codes['installer']=1603
        code,r=self.execute()
        self.assertEqual(code,1); self.assertEqual(r['status'],'INSTALLER_FAILED')

    def test_reboot_is_not_pass(self):
        self.codes['installer']=3010
        code,r=self.execute()
        self.assertEqual(code,1); self.assertIn('REBOOT_REQUIRED',r['status'])

    def test_truncated_reboot_is_not_pass(self):
        self.codes['installer']=194
        code,r=self.execute()
        self.assertEqual(code,1); self.assertIn('REBOOT_REQUIRED',r['status'])

    def test_timeout_preserves_results_and_cleans(self):
        self.failures['installer']=subprocess.TimeoutExpired('msiexec',120)
        code,r=self.execute()
        self.assertEqual(code,1); self.assertEqual(r['status'],'TIMEOUT')
        self.assertEqual(r['cleanup_wait_exit'],0)

    def test_global_deadline_refuses_installer(self):
        self.comp.start.side_effect=lambda: setattr(self,'clock',245)
        code,r=self.execute()
        self.assertEqual(code,1); self.assertFalse(r['installer_attempted'])
        self.assertEqual(r['status'],'TIMEOUT')

    def test_cleanup_failure_is_not_pass(self):
        self.failures['cleanup-kill']=RuntimeError('cleanup failed')
        code,r=self.execute()
        self.assertEqual(code,1); self.assertIn('cleanup failed',str(r['cleanup_errors']))

    def test_missing_payload(self):
        self.payload=False
        code,r=self.execute()
        self.assertEqual(code,1); self.assertEqual(r['status'],'PAYLOAD_OR_LOG_EVIDENCE_FAILED')

    def test_missing_registration(self):
        self.registration=False
        code,r=self.execute()
        self.assertEqual(code,1); self.assertIn('registration',r['error'])

    def test_wrong_registration_version(self):
        original = self.make_payload
        def wrong():
            original()
            hive=self.output/'prefix/system.reg'
            hive.write_text(hive.read_text().replace(edge.VERSION,'1.2.3.4'))
        self.make_payload=wrong
        code,r=self.execute()
        self.assertEqual(code,1); self.assertIn('registration',r['error'])

    def test_compositor_failure_retains_results_and_cleans(self):
        self.comp.start.side_effect=RuntimeError('COMPOSITOR')
        code,r=self.execute()
        self.assertEqual(code,1); self.assertIn('COMPOSITOR',r['error'])
        self.assertFalse(r['installer_attempted']); self.comp.stop.assert_called_once()

    def test_remaining_server_fails(self):
        self.helpers.matching_servers.return_value=[123]
        code,r=self.execute()
        self.assertEqual(code,1); self.assertEqual(r['remaining_servers'],[123])

    def test_wrong_arch(self):
        self.machine=0x014c
        code,r=self.execute()
        self.assertEqual(code,1); self.assertIn('not x64',r['error'])

    def test_copy_error_cannot_mask_primary_error_or_results(self):
        self.failures['installer']=RuntimeError('PRIMARY')
        with patch.object(edge,'collect_setup_logs',side_effect=OSError('COPY')):
            code,r=self.execute()
        self.assertEqual(code,1); self.assertIn('PRIMARY',r['error']); self.assertIn('COPY',r['postprocess_error'])

    def test_parsing_failure_saved(self):
        with patch.object(edge,'installed_evidence',side_effect=OSError('PARSE')):
            code,r=self.execute()
        self.assertEqual(code,1); self.assertIn('PARSE',r['error'])

    def test_bounded_log_copy(self):
        prefix=self.root/'prefix'; prefix.mkdir()
        for i in range(130): (prefix/(str(i)+'.log')).write_text('x')
        logs=edge.collect_setup_logs(prefix,self.root,100,lambda:0)
        self.assertEqual(len(logs),129); self.assertIn('scan_stopped',logs[-1])
        self.assertIn('scan_stopped',edge.collect_setup_logs(prefix,self.root,0,lambda:0)[0])

    def test_bounds_checked_pe_headers(self):
        p=self.root/'bad.exe'
        for data in (b'',b'MZ',b'MZ'+b'\0'*126):
            p.write_bytes(data)
            with self.assertRaises(ValueError): edge.pe_metadata(p)

    def test_large_logs_retained_not_copied(self):
        prefix=self.root/'prefix'; prefix.mkdir()
        path=prefix/'large.log'
        with path.open('wb') as stream: stream.truncate(16*1024**2+1)
        logs=edge.collect_setup_logs(prefix,self.root,100,lambda:0)
        self.assertTrue(logs[0]['retained_in_prefix_only']); self.assertTrue(path.exists())


if __name__=='__main__': unittest.main()
