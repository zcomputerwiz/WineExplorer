# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual original fixture plus fake runtime/processes; no browser/guest execution."""
import copy
import importlib.util
import json
from pathlib import Path
import struct
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

import test_g4_edge_headful as fixtures

spec=importlib.util.spec_from_file_location('chromium',Path(__file__).with_name('check-g4-chromium-headful.py'))
chrome=importlib.util.module_from_spec(spec);spec.loader.exec_module(chrome)
edge=chrome.edge


class ChromiumTests(unittest.TestCase):
    setUpClass=classmethod(fixtures.HeadfulTests.setUpClass.__func__)
    png=fixtures.HeadfulTests.png
    logged=fixtures.HeadfulTests.logged

    def setUp(self):
        # Reuse fake compositor/capture callbacks, not inherited test duplication.
        with patch.dict(fixtures.HeadfulTests.setUp.__globals__,edge=edge):fixtures.HeadfulTests.setUp(self)
        self.output=self.root/'chromium-headful-test'
        self.fixture=self.root/'chromium-headful-test-stage/scripts/g4-edge-probe/index.html'
        self.fixture.parent.mkdir(parents=True)
        for name in chrome.FIXTURE_SHA:
            self.fixture.with_name(name).write_bytes(Path(__file__).with_name('g4-edge-probe').joinpath(name).read_bytes())
        self.runtime=self.root/'chromium-headful-test-stage/vendor/extracted/chrome-win';self.runtime.mkdir(parents=True)
        pe=self.exe.read_bytes()
        for name in ('chrome.exe','chrome.dll','chrome_elf.dll','D3DCompiler_47.dll','libEGL.dll','libGLESv2.dll'):
            (self.runtime/name).write_bytes(pe)
        self.manifest=self.runtime.parent.parent/'runtime-manifest.json'
        chrome.cache.save(self.manifest,dict(version=chrome.cache.VERSION,revision=chrome.cache.REVISION,zip_sha256=chrome.cache.ZIP_SHA,
            files={'chrome-win/'+p.name:dict(bytes=p.stat().st_size,sha256=chrome.cache.digest(p)) for p in self.runtime.iterdir()}))
        pin=patch.object(chrome,'MANIFEST_SHA',chrome.cache.digest(self.manifest));pin.start();self.addCleanup(pin.stop)
        self.policy=chrome.Chromium(self.runtime)
        self.window.update(image='Z:'+str(self.runtime/'chrome.exe').replace('/','\\'),creation_time=123456,top_level=True)
        self.window['title']=self.title+' - Chromium'
        self.server=Mock();self.server.record={'requests':[],'errors':[]}
        self.server.start.return_value='http://127.0.0.1:1234/'+self.nonce+'/index.html#nonce='+self.nonce
        self.proof=SimpleNamespace(verify_private_loopback=Mock(return_value={'private':True,'identity':42}))
        original=edge.load
        loader=patch.object(edge,'load',side_effect=lambda name:self.proof if name=='run-isolated-graphics' else
            SimpleNamespace(FixtureServer=Mock(return_value=self.server)) if name=='g4_edge_http_fixture' else original(name))
        loader.start();self.addCleanup(loader.stop)
        self.owner=patch.object(self.policy,'linux_owner',return_value={'linux_pid':900,'start_ticks':123,'argv_style':'wine_flat_padded'})
        self.owner.start();self.addCleanup(self.owner.stop)
        self.comp.verify_gpu_renderer.return_value={'passed':True}

    def state(self):
        return edge.window_record([self.window],self.nonce,vendor='chromium',
             owner_aliases=['Z:'+str(self.runtime/'chrome.exe').replace('/','\\')])

    def execute(self):
        with patch.object(edge,'OwnedWexcomp',return_value=self.comp), \
             patch.object(edge,'verify_native_gl_driver',return_value={'passed':True}), \
             patch.object(edge.secrets,'token_hex',return_value=self.nonce):
            code=edge.run(self.output,self.fixture,self.helpers,chromium=self.policy,
                private_loopback_control=True,http_fixture_control=True,now=lambda:self.clock,
                sleep=lambda s:setattr(self,'clock',self.clock+s),popen=self.popen)
        return code,json.loads((self.output/'results.json').read_text())

    def test_normal_http_success_exact_vendor_command_and_no_security_flags(self):
        code,r=self.execute();self.assertEqual(code,0);self.assertTrue(r['passed']);self.assertFalse(r['startup_diagnostic'])
        self.assertEqual(r['argv'],self.policy.command(self.fixture,self.output,self.nonce,self.server.start.return_value))
        self.assertEqual(r['argv'][1],str(self.runtime/'chrome.exe'));self.assertIn('--app=http://127.0.0.1:',r['argv'][2])
        self.assertFalse(any(word in ' '.join(r['argv']) for word in ('no-sandbox','disable-','remote-debug','vmodule')))
        self.assertTrue(r['shared_installed_edge_prefix']);self.assertEqual(len(r['front_samples']),3)
        self.assertEqual(r['linux_owner_before'],r['linux_owner_after']);self.assertEqual(self.policy.linux_owner.call_count,2)
        self.assertTrue(all(s['pixels']['passed'] for s in r['front_samples']))

    def test_vendor_suffix_and_full_image_ownership_do_not_mix(self):
        self.assertIsNotNone(self.state())
        for title in (self.title+' - Microsoft Edge',self.title+' - Chromium - Chromium'):
            self.window['title']=title
            with self.assertRaises(ValueError):self.state()
        self.window['title']=self.title+' - Chromium'
        for image in ('chrome.exe',str(self.runtime/'chrome.exe'),r'C:\chrome.exe',r'Z:\foreign\chrome.exe',str(self.exe)):
            self.window['image']=image
            with self.assertRaises(ValueError):self.state()
        with self.assertRaises(ValueError):edge.page_record(self.title+' - Chromium',self.nonce)

    def test_creation_top_level_bool_geometry_and_nonce_fail(self):
        for key,value in (('creation_time',True),('creation_time',0),('top_level',False),('pid',False),
                          ('client',dict(x=False,y=60,width=640,height=480))):
            original=copy.deepcopy(self.window);self.window[key]=value
            with self.assertRaises(ValueError):self.state()
            self.window=original
        with self.assertRaises(ValueError):edge.page_record(self.title,'b'*32,vendor='chromium')

    def test_runtime_missing_extra_wrong_pe_hash_mapping_manifest_preprocess(self):
        original={p:p.read_bytes() for p in self.runtime.iterdir()}
        manifest_bytes=self.manifest.read_bytes();fixture_bytes=self.fixture.read_bytes()
        for fault in ('missing','extra','pe','manifest','z','fixture'):
            with self.subTest(fault=fault):
                if fault=='missing':(self.runtime/'chrome.dll').rename(self.runtime/'missing')
                elif fault=='extra':(self.runtime/'extra').write_text('foreign')
                elif fault=='pe':(self.runtime/'chrome.exe').write_bytes(b'bad PE')
                elif fault=='manifest':self.manifest.write_text('{}')
                elif fault=='z':(self.prefix/'dosdevices/z:').unlink();(self.prefix/'dosdevices/z:').symlink_to('/tmp')
                else:self.fixture.write_text('changed original')
                with self.assertRaises((ValueError,FileNotFoundError)):self.execute()
                self.assertFalse(self.output.exists());self.popen.assert_not_called();self.comp.start.assert_not_called()
                for path in self.runtime.iterdir():
                    if path not in original:path.unlink()
                for path,data in original.items():path.write_bytes(data)
                self.manifest.write_bytes(manifest_bytes);self.fixture.write_bytes(fixture_bytes)
                if fault=='z':(self.prefix/'dosdevices/z:').unlink();(self.prefix/'dosdevices/z:').symlink_to('/')

    def test_wrong_pe_even_with_matching_synthetic_inventory_and_version_source(self):
        data=bytearray((self.runtime/'chrome.exe').read_bytes());struct.pack_into('<H',data,68,0x014c)
        (self.runtime/'chrome.exe').write_bytes(data)
        record=json.loads(self.manifest.read_text())
        record['files']['chrome-win/chrome.exe']['sha256']=chrome.cache.digest(self.runtime/'chrome.exe')
        chrome.cache.save(self.manifest,record)
        with patch.object(chrome,'MANIFEST_SHA',chrome.cache.digest(self.manifest)),self.assertRaisesRegex(ValueError,'AMD64'):
            self.execute()
        self.popen.assert_not_called()

    def test_wrong_runtime_and_bad_url_profile_args_are_not_accepted(self):
        with self.assertRaises(ValueError):chrome.Chromium(self.root/'foreign').require_payload(self.output,self.fixture)
        for url in ('file:///other','http://example.com/x','http://127.0.0.1:1234/foreign/index.html',
                    self.server.start.return_value+' --no-sandbox','http://127.0.0.1:99999/'+self.nonce+'/index.html#nonce='+self.nonce):
            with self.assertRaises(ValueError):self.policy.command(self.fixture,self.output,self.nonce,url)
        argv=self.policy.command(self.fixture,self.output,self.nonce,self.server.start.return_value)
        self.assertEqual(argv[3],'--user-data-dir=Z:'+str(self.output/'profile').replace('/','\\'))

    def test_no_diagnostic_attach_or_nonhttp_dispatch(self):
        for changes in ({'startup_diagnostic':True},{'owned_thread_diagnostic':True},{'owned_stack_diagnostic':True},
                        {'http_fixture_control':False},{'private_loopback_control':False}):
            flags=dict(private_loopback_control=True,http_fixture_control=True);flags.update(changes)
            with self.assertRaises(ValueError):edge.run(self.output,self.fixture,self.helpers,chromium=self.policy,**flags)
            self.popen.assert_not_called();self.assertFalse(self.output.exists())

    def test_timeout_retains_primary_front_even_when_capture_fails(self):
        self.window['title']='WEXEDGE loading';self.codes['failure-front-capture']=1
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('No nonce-bound',r['error'])
        self.assertIn('Terminal front capture failed',r['failure_front']['error']);self.server.close.assert_called_once()
        self.assertEqual(r['cleanup_wait'],0);self.assertFalse(r['passed'])

    def test_capture_failure_front_and_cleanup_failure_cannot_pass(self):
        self.codes['front-0-capture']=1;self.failures['cleanup-wait']=RuntimeError('secondary cleanup')
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('Front capture failed',r['error'])
        self.assertTrue(r['failure_front']['captured']);self.assertIn('secondary cleanup',str(r['cleanup_errors']))

    def test_log_quota_enforced_in_normal_mode_and_final_not_acceptance(self):
        original=self.popen.side_effect
        def launch(*a,**kw):
            browser=original(*a,**kw)
            with (self.output/'browser.stderr.log').open('wb') as stream:stream.truncate(edge.LOG_LIMIT+1)
            return browser
        self.popen.side_effect=launch
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('quota',r['error'])
        self.assertTrue(r['failure_front']['captured']);self.assertFalse(r['log_quota_after_cleanup']['passed'])

    def test_linux_or_namespace_change_cannot_pass(self):
        self.policy.linux_owner.side_effect=[{'linux_pid':900},{'linux_pid':901}]
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('Linux owner changed',r['error'])
        self.assertTrue(r['failure_front']['captured'])

    def test_private_namespace_after_must_equal_before(self):
        self.proof.verify_private_loopback.side_effect=[{'identity':1},{'identity':2}]
        code,r=self.execute();self.assertEqual(code,1);self.assertTrue(r['completed']);self.assertFalse(r['passed'])

    def test_linux_owner_requires_recorded_identity_and_exact_full_argv(self):
        real=chrome.Chromium(self.runtime)
        with self.assertRaises(ValueError):real.linux_owner(900,None,['wine','chrome.exe'])
        with patch.object(chrome.ownership,'linux_identity',side_effect=ValueError('profile/prefix/argv mismatch')) as verify:
            with self.assertRaisesRegex(ValueError,'profile/prefix/argv'):real.linux_owner(900,{'start_ticks':123},['wine','chrome.exe'])
            self.assertEqual(verify.call_args.args[1:4],(900,123,self.prefix))


if __name__=='__main__':unittest.main()
