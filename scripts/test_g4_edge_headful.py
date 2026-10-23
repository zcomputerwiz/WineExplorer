# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline only: actual fixture JS, decoded PNGs and mocked Wine/process flows."""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch
from PIL import Image, ImageDraw

spec=importlib.util.spec_from_file_location('headful',Path(__file__).with_name('check-g4-edge-headful.py'))
edge=importlib.util.module_from_spec(spec);spec.loader.exec_module(edge)


class HeadfulTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        script=Path(__file__).with_name('test_g4_edge_scene.js').resolve()
        node=shutil.which('node')
        if not node:
            node='/mnt/c/Program Files/nodejs/node.exe'
            if not Path(node).is_file(): raise RuntimeError('Node required; no fixture test skip')
            script='D:/DeepThought/Projects/WineExplorer/scripts/test_g4_edge_scene.js'
        cls.title=subprocess.check_output([node,str(script),'--record'],text=True).strip()
        cls.page=json.loads(cls.title[8:])
        cls.nonce=cls.page['nonce']

    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name)
        self.output=self.root/'edge-headful-test'
        self.prefix=self.root/'installed/prefix'
        self.exe=self.prefix/'drive_c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
        self.exe.parent.mkdir(parents=True)
        data=bytearray(128);data[:2]=b'MZ';struct.pack_into('<I',data,0x3c,64)
        data[64:68]=b'PE\0\0';struct.pack_into('<H',data,68,0x8664);self.exe.write_bytes(data)
        (self.prefix/'dosdevices').mkdir()
        (self.prefix/'dosdevices/c:').symlink_to('../drive_c')
        (self.prefix/'dosdevices/z:').symlink_to('/')
        (self.prefix/'system.reg').write_text('[Software\\\\Microsoft\\\\EdgeUpdate\\\\Clients\\\\edge]\n'
            '"name"="Microsoft Edge"\n"pv"="156.0.4314.42"\n\n'
            '[System\\\\CurrentControlSet\\\\Control\\\\Session Manager]\n'
            '"PendingFileRenameOperations"=hex(7):00,00\n')
        (self.prefix/'user.reg').write_text('WINE REGISTRY Version 2\n')
        self.fixture=self.root/'edge-headful-test-stage/scripts/g4-edge-probe/index.html'
        self.fixture.parent.mkdir(parents=True);self.fixture.write_text('original test fixture placeholder')
        self.patches=[patch.object(edge,'LAB',self.root),patch.object(edge,'PREFIX',self.prefix),
                      patch.object(edge,'EXE',self.exe),patch.object(edge,'EXE_SHA',edge.install.digest(self.exe)),
                      patch.object(edge.shutil,'disk_usage',return_value=SimpleNamespace(free=1024**3)),
                      patch.object(edge.graphics_resources,'sample',return_value={})]
        for p in self.patches: p.start();self.addCleanup(p.stop)
        self.window={'title':self.title,'pid':88,'hwnd':123,'visible':True,'geometry_valid':True,
            'class':'Chrome_WidgetWin_1','image':'Z:'+str(self.exe).replace('/','\\'),'fixed':65536,
            'window':dict(x=20,y=30,width=660,height=520),'client':dict(x=30,y=60,width=640,height=480)}
        self.clock=0; self.calls=[];self.codes={};self.failures={};self.boot_action=lambda:None
        self.startup_diagnostic=False
        self.browser=Mock();self.browser.poll.return_value=None;self.browser.wait.return_value=0
        self.browser.pid=900
        def start_browser(*args,**kwargs):
            (self.output/'chromium.log').write_text('Mock browser diagnostic log\n')
            return self.browser
        self.popen=Mock(side_effect=start_browser)
        self.comp=Mock();self.comp.start.return_value={'owned':True}
        self.comp.verify_gpu_renderer.return_value={'passed':True}
        self.helpers=SimpleNamespace(configure_gl_driver=Mock(),matching_servers=Mock(return_value=[]),run_logged=self.logged)

    def state(self): return edge.window_record([self.window],self.nonce)

    def png(self,path,state=None,blank=False):
        state=state or self.state();scale=state['scale'];c=state['native']['client']
        image=Image.new('RGB',(1600,1200),(0,0,0));draw=ImageDraw.Draw(image)
        if not blank:
            draw.rectangle([round(c['x']*scale),round(c['y']*scale),round((c['x']+c['width'])*scale)-1,
                            round((c['y']+c['height'])*scale)-1],fill=edge.COLORS['background'])
            for name,r in edge.RECTS.items():
                draw.rectangle([round((c['x']+r['x'])*scale),round((c['y']+r['y'])*scale),
                    round((c['x']+r['x']+r['width'])*scale)-1,round((c['y']+r['y']+r['height'])*scale)-1],fill=edge.COLORS[name])
        image.save(path)

    def logged(self,argv,env,output,label,timeout):
        self.calls.append((argv,dict(env),label,timeout));self.clock+=.01
        if label in self.failures: raise self.failures[label]
        if label=='wineboot': self.boot_action()
        text=''
        if label=='observer-build':Path(argv[4]).write_bytes(b'mocked original observer executable')
        if label=='fixture-file-probe':
            data=self.fixture.read_bytes()
            text=json.dumps(dict(ok=True,error=0,size=len(data),read=min(64,len(data)),prefix_hex=data[:64].hex()))
        if label.startswith('native-'): text=json.dumps([self.window])
        if label=='failure-front-capture':Image.new('RGB',(1600,1200),'gray').save(Path(argv[-1]))
        elif label.endswith('-capture'): self.png(Path(argv[-1]))
        return SimpleNamespace(returncode=self.codes.get(label,0),stdout=text)

    def execute(self):
        with patch.object(edge,'OwnedWexcomp',return_value=self.comp), \
             patch.object(edge,'verify_native_gl_driver',return_value={'passed':True}), \
             patch.object(edge.secrets,'token_hex',return_value=self.nonce):
            code=edge.run(self.output,self.fixture,self.helpers,now=lambda:self.clock,
                          sleep=lambda seconds:setattr(self,'clock',self.clock+seconds),popen=self.popen,
                          startup_diagnostic=self.startup_diagnostic)
        return code,json.loads((self.output/'results.json').read_text())

    def test_actual_fixture_protocol(self):
        self.assertTrue(edge.page_record(self.title,self.nonce)['passed'])
        self.assertTrue(edge.page_record(self.title+' - Microsoft Edge',self.nonce)['passed'])

    def test_protocol_failures(self):
        for change in [lambda p:p.update(nonce='b'*32),lambda p:p.update(passed=False),
                       lambda p:p['viewport'].update(dpr=float('nan')),lambda p:p['viewport'].update(width=1e100),
                       lambda p:p['apis'].pop('webgl2'),lambda p:p['apis']['webgl'].update(defaultPixel=[]),
                       lambda p:p['apis']['webgl'].update(fboComplete=False),lambda p:p['apis']['webgl'].update(renderer=''),
                       lambda p:p['apis']['webgl'].update(renderer='SwiftShader'),lambda p:p['apis']['webgl'].update(lost=True),
                       lambda p:p['apis']['webgl'].update(error=1280),lambda p:p['apis']['webgl']['rect'].update(x=-2)]:
            p=copy.deepcopy(self.page);change(p)
            with self.subTest(page=p),self.assertRaises(ValueError):edge.page_record('WEXEDGE '+json.dumps(p),self.nonce)
        for title in [self.title+' - Other',self.title+' - Microsoft Edge - Microsoft Edge','WEXEDGE {bad}',
                      self.title.replace('"v":1','"v":1,"v":1')]:
            with self.subTest(title=title),self.assertRaises(ValueError):edge.page_record(title,self.nonce)

    def test_loading_and_missing_pending(self):
        self.assertIsNone(edge.window_record([],self.nonce))
        w=copy.deepcopy(self.window);w['title']='WEXEDGE loading'
        self.assertIsNone(edge.window_record([w],self.nonce))

    def test_owner_aliases_exact(self):
        self.assertIsNotNone(self.state())
        self.window['image']=r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
        self.assertIsNotNone(self.state())
        self.assertEqual(edge.require_payload()['alias_mapping']['c:'],str(self.prefix/'drive_c'))
        for wrong in ['msedge.exe',r'C:\Other\msedge.exe',r'Z:\another-prefix\msedge.exe']:
            self.window['image']=wrong
            with self.assertRaises(ValueError):self.state()

    def test_wrong_alias_mapping(self):
        (self.prefix/'dosdevices/c:').unlink();(self.prefix/'dosdevices/c:').symlink_to('/')
        with self.assertRaises(ValueError):edge.require_payload()

    def test_window_negative_shapes_geometry_and_identity(self):
        with self.assertRaises(ValueError):edge.window_record([self.window,self.window],self.nonce)
        with self.assertRaises(ValueError):edge.window_record([None],self.nonce)
        for key,value in [('visible',False),('geometry_valid',False),('pid',0),('fixed',0),('fixed',float('nan')),
                          ('client',dict(x=0,y=0,width=640,height=480)),('client',dict(x=30,y=60,width=640,height=479)),
                          ('window',dict(x=-1,y=30,width=660,height=520)),('client',dict(x=30,y=60,width=1e40,height=480))]:
            w=copy.deepcopy(self.window);w[key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):edge.window_record([w],self.nonce)

    def test_decoded_pixels_and_fractional_scale_nonzero_client_origin(self):
        for scale in (1,1.25):
            p=copy.deepcopy(self.page);p['viewport']['dpr']=scale
            self.window['fixed']=round(scale*65536);self.window['title']='WEXEDGE '+json.dumps(p)
            state=self.state();path=self.root/'front.png';self.png(path,state)
            self.assertTrue(edge.front_pixels(path,state)['passed'])
            wrong=copy.deepcopy(state);wrong['native']['client']['y']-=50
            self.assertFalse(edge.front_pixels(path,wrong)['passed'])
            self.png(path,state,blank=True);self.assertFalse(edge.front_pixels(path,state)['passed'])
        path.write_bytes(b'not PNG')
        with self.assertRaises(Exception):edge.front_pixels(path,state)

    def test_partial_canvas_rejected(self):
        path=self.root/'front.png';self.png(path)
        with Image.open(path) as image:
            ImageDraw.Draw(image).rectangle([62,172,130,245],fill=(0,0,0));image.save(path)
        self.assertFalse(edge.front_pixels(path,self.state())['passed'])

    def test_wrong_pixel_scale_and_clipped_output(self):
        path=self.root/'front.png';self.png(path)
        state=self.state();state['scale']=1.25
        self.assertFalse(edge.front_pixels(path,state)['passed'])
        with Image.open(path) as image:image.crop((0,0,200,200)).save(path)
        with self.assertRaises(ValueError):edge.front_pixels(path,self.state())

    def test_capture_order_is_not_pixel_driven_and_lateness_recorded(self):
        records=[]; events=[];state=self.state()
        def capture(path):
            events.append(('capture',self.clock));self.png(path,blank=len(records)==1)
            self.clock+=.2
        def observe():events.append(('observe',self.clock));self.clock+=.1;return state
        passed=edge.collect_fronts(observe,capture,lambda:None,self.root,state,records,
            now=lambda:self.clock,sleep=lambda s:setattr(self,'clock',self.clock+s))
        self.assertTrue(passed);self.assertEqual([r['offset_seconds'] for r in records],[0,1,3])
        self.assertFalse(records[0]['pixels']['passed']);self.assertEqual(len(events),9)
        self.assertAlmostEqual(records[1]['lateness_seconds'],.1)
        self.assertEqual([e[0] for e in events],['observe','capture','observe']*3)

    def test_last_capture_move_detected_and_png_retained(self):
        state=self.state();records=[];count=0
        def observe():
            nonlocal count
            count+=1;value=copy.deepcopy(state)
            if count==6:value['native']['client']['x']+=1
            return value
        with self.assertRaises(ValueError):edge.collect_fronts(observe,lambda p:self.png(p),lambda:None,self.root,state,records,
            now=lambda:self.clock,sleep=lambda s:setattr(self,'clock',self.clock+s))
        self.assertEqual(len(records),3);self.assertTrue((self.root/'front-3.png').is_file())

    def test_all_stale_collects_every_sample_but_fails(self):
        records=[];state=self.state()
        passed=edge.collect_fronts(lambda:state,lambda p:self.png(p,blank=True),lambda:None,self.root,state,records,
            now=lambda:self.clock,sleep=lambda s:setattr(self,'clock',self.clock+s))
        self.assertFalse(passed);self.assertEqual(len(records),3)

    def test_mocked_success_and_exact_command(self):
        code,r=self.execute();self.assertEqual(code,0);self.assertTrue(r['passed'])
        self.assertEqual([s['offset_seconds'] for s in r['front_samples']],[0,1,3])
        argv=r['argv'];self.assertEqual(argv[:2],[str(edge.BUILD/'wine'),str(self.exe)])
        self.assertEqual(argv[2],'--app=file:///Z:'+str(self.fixture)+'#nonce='+self.nonce)
        self.assertIn('--user-data-dir=Z:'+str(self.output/'profile').replace('/','\\'),argv)
        self.assertEqual(argv[4:7],['--no-first-run','--no-default-browser-check','--enable-logging'])
        self.assertFalse(any(v in ' '.join(argv) for v in ('--no-sandbox','--disable-gpu','remote-debugging')))
        for _,env,_,timeout in self.calls:
            self.assertEqual(env['WINEPREFIX'],str(self.prefix));self.assertTrue(Path(env['HOME']).is_relative_to(self.output))
            self.assertLessEqual(timeout,60)
        before=json.loads((self.output/'persistence-before.json').read_text())
        self.assertTrue(before['hives']['system.reg']['pending_blocks'])
        self.assertTrue((self.output/'persistence-after.json').is_file())

    def test_guard_failure_never_calls_process_or_cleanup(self):
        self.exe.write_bytes(b'changed')
        with patch.object(edge,'OwnedWexcomp') as comp,patch.object(edge.subprocess,'Popen') as popen:
            with self.assertRaises(ValueError):edge.run(self.output,self.fixture,self.helpers)
            self.assertEqual(self.calls,[]);popen.assert_not_called();comp.assert_not_called()
        self.assertFalse(self.output.exists())

    def test_space_guard_before_creation(self):
        with patch.object(edge.shutil,'disk_usage',return_value=SimpleNamespace(free=1024**3-1)):
            with self.assertRaises(ValueError):edge.run(self.output,self.fixture,self.helpers)
        self.assertFalse(self.output.exists());self.assertEqual(self.calls,[])

    def test_payload_deleted_by_boot_preserves_after_and_never_browser(self):
        self.boot_action=lambda:self.exe.unlink()
        code,r=self.execute();self.assertEqual(code,1);self.assertNotIn('ready_elapsed',r)
        self.popen.assert_not_called()
        self.assertTrue((self.output/'persistence-after.json').is_file());self.assertEqual(len(r['front_samples']),0)

    def test_boot_failure(self):
        self.codes['wineboot']=1
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('Wineboot',r['error'])

    def test_observer_failure(self):
        self.codes['native-0']=1
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('observer failed',r['error'])

    def test_comp_failure(self):
        self.comp.start.side_effect=RuntimeError('comp failed')
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('comp failed',r['error'])

    def test_browser_exit(self):
        self.browser.poll.return_value=1
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('launcher exited',r['error'])

    def test_timeout_and_cleanup_failure(self):
        self.failures['wineboot']=subprocess.TimeoutExpired('wineboot',60)
        self.failures['cleanup-kill']=OSError('cleanup failed')
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('TimeoutExpired',r['error'])
        self.assertIn('cleanup failed',str(r['cleanup_errors']))

    def test_fatal_health(self):
        self.output.mkdir()
        (self.output/'browser.stderr.log').write_text('GPU process exited unexpectedly\n[FATAL:foo]')
        self.assertTrue(edge.log_health(self.output)['gpu_exit']);self.assertTrue(edge.log_health(self.output)['fatal'])

    def test_process_role_provenance_no_windows_pid_equivalence(self):
        roles=edge.process_roles({'123':{'start_ticks':456,'cmdline':'wine msedge.exe --type=gpu-process --foo'},
                                  '999':{'start_ticks':10,'cmdline':'msedge.exe --type=renderer'},
                                  '77':{'start_ticks':1,'cmdline':'msedge.exe --type=renderer-foreign'}})
        self.assertEqual(set(roles),{'123:456','999:10'})
        self.assertEqual(roles['123:456']['linux_pid'],'123')

    def test_process_role_exit_recorded_only_before_cleanup(self):
        result=dict(observed_process_roles={},role_exits_before_cleanup=[])
        previous=edge.record_roles(result,{}, {'123':{'start_ticks':456,'cmdline':'msedge.exe --type=gpu-process'}},False)
        edge.record_roles(result,previous,{},False)
        self.assertEqual(len(result['role_exits_before_cleanup']),1)
        edge.record_roles(result,previous,{},True)
        self.assertEqual(len(result['role_exits_before_cleanup']),1)

    def test_missing_chromium_log_fails_complete_collection(self):
        self.popen.side_effect=None;self.popen.return_value=self.browser
        code,r=self.execute();self.assertEqual(code,1);self.assertTrue(r['completed'])
        self.assertFalse(r['chromium_log_present'])

    def test_readiness_timeout(self):
        self.window['title']='WEXEDGE loading'
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('No nonce-bound',r['error'])
        self.assertEqual(r['front_samples'],[])

    def test_compilation_failure(self):
        self.codes['observer-build']=1
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('compilation failed',r['error'])
        self.popen.assert_not_called()

    def test_registration_changed_by_boot(self):
        def change():
            path=self.prefix/'system.reg';path.write_text(path.read_text().replace('156.0.4314.42','1.2.3.4'))
        self.boot_action=change
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('registration',r['error'])
        self.popen.assert_not_called()

    def test_exhausted_global_budget_never_launches_browser(self):
        self.boot_action=lambda:setattr(self,'clock',246)
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('budget exhausted',r['error'])
        self.popen.assert_not_called()

    def test_observer_invalid_json(self):
        original=self.helpers.run_logged
        def logged(*args):
            value=original(*args)
            if args[3].startswith('native-'):value.stdout='invalid JSON'
            return value
        self.helpers.run_logged=logged
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('JSONDecodeError',r['error'])

    def test_missing_capture_stops_as_collection_failure(self):
        original=self.helpers.run_logged
        def logged(*args):
            value=original(*args)
            if args[3].endswith('-capture'):Path(args[0][-1]).unlink()
            return value
        self.helpers.run_logged=logged
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('FileNotFoundError',r['error'])
        self.assertEqual(len(r['front_samples']),1)

    def test_context_loss_after_last_capture(self):
        original=self.helpers.run_logged
        def logged(*args):
            value=original(*args)
            if args[3]=='front-3-capture':
                page=copy.deepcopy(self.page);page['passed']=False;page['error']='context lost'
                self.window['title']='WEXEDGE '+json.dumps(page)
            return value
        self.helpers.run_logged=logged
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('failed readiness',r['error'])
        self.assertTrue((self.output/'front-3.png').exists())

    def test_cleanup_nonzero_fails_successful_collection(self):
        self.codes['cleanup-wait']=1
        code,r=self.execute();self.assertEqual(code,1);self.assertTrue(r['completed'])

    def test_final_fatal_invalidates_complete_collection(self):
        original=self.helpers.run_logged
        def logged(*args):
            value=original(*args)
            if args[3]=='cleanup-kill':(self.output/'chromium.log').write_text('renderer process crashed')
            return value
        self.helpers.run_logged=logged
        code,r=self.execute();self.assertEqual(code,1);self.assertTrue(r['completed'])
        self.assertEqual(r['log_errors']['renderer_exit'],1)

    def test_terminal_front_preserves_primary_and_precedes_cleanup(self):
        self.failures['native-0']=RuntimeError('PRIMARY observer')
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('PRIMARY observer',r['error'])
        self.assertTrue(r['failure_front']['captured']);self.assertNotIn('pixels',r['failure_front'])
        self.assertEqual(r['front_samples'],[])
        labels=[c[2] for c in self.calls]
        self.assertEqual(labels.count('failure-front-capture'),1)
        self.assertLess(labels.index('failure-front-capture'),labels.index('cleanup-kill'))
        self.assertLessEqual(next(c[3] for c in self.calls if c[2]=='failure-front-capture'),3)

    def test_failed_terminal_front_preserves_primary(self):
        self.failures['native-0']=RuntimeError('PRIMARY')
        self.failures['failure-front-capture']=OSError('SECONDARY')
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('PRIMARY',r['error'])
        self.assertIn('SECONDARY',r['failure_front']['error']);self.assertEqual(r['cleanup_wait'],0)

    def test_timeout_terminal_front_preserves_primary(self):
        self.failures['native-0']=RuntimeError('PRIMARY')
        self.failures['failure-front-capture']=subprocess.TimeoutExpired('grim',3)
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('PRIMARY',r['error'])
        self.assertIn('TimeoutExpired',r['failure_front']['error']);self.assertEqual(r['cleanup_wait'],0)

    def test_dead_compositor_never_captures(self):
        self.failures['native-0']=RuntimeError('PRIMARY')
        self.comp.verify.side_effect=[None,RuntimeError('compositor dead')]
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('PRIMARY',r['error'])
        self.assertIn('compositor dead',r['failure_front']['error'])
        self.assertFalse(any(c[2]=='failure-front-capture' for c in self.calls))

    def test_no_browser_no_failure_front(self):
        self.popen.side_effect=OSError('browser not created')
        code,r=self.execute();self.assertEqual(code,1);self.assertNotIn('failure_front',r)

    def test_terminal_front_does_not_consume_cleanup_reserve(self):
        original=self.helpers.run_logged
        def logged(*args):
            if args[3]=='native-0':
                self.clock=edge.TOTAL-edge.RESERVE
                raise RuntimeError('PRIMARY')
            return original(*args)
        self.helpers.run_logged=logged
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('PRIMARY',r['error'])
        self.assertIn('budget exhausted',r['failure_front']['error'])
        self.assertFalse(any(c[2]=='failure-front-capture' for c in self.calls));self.assertEqual(r['cleanup_wait'],0)

    def test_diagnostic_mode_never_passes_and_only_changes_logging(self):
        self.startup_diagnostic=True
        code,r=self.execute();self.assertEqual(code,1);self.assertTrue(r['completed']);self.assertFalse(r['passed'])
        self.assertIn('NOT_EVALUATED',r['classification'])
        normal=edge.command(self.fixture,self.output,self.nonce)
        self.assertEqual(r['argv'][:-1],normal);self.assertEqual(r['argv'][-1],'--vmodule='+edge.VLOG_MODULES)
        browser_env=self.popen.call_args.kwargs['env']
        self.assertEqual(browser_env['WINEDEBUG'],'-all,+timestamp,+pid,+tid,+winediag,+process,+file,+seh')
        for _,env,_,_ in self.calls:self.assertNotIn('+file',env['WINEDEBUG'])
        probe=next(c for c in self.calls if c[2]=='fixture-file-probe')
        self.assertEqual(probe[0][-2:],['--fixture-file','Z:'+str(self.fixture).replace('/','\\')])
        self.assertLessEqual(probe[3],5)
        self.assertTrue(r['fixture_file_probe']['ok']);self.assertNotIn('failure_front',r)

    def test_diagnostic_log_quota_stops_wait_and_preserves_logs(self):
        self.startup_diagnostic=True
        with patch.object(edge,'LOG_LIMIT',8):
            code,r=self.execute()
        self.assertEqual(code,1);self.assertIn('log quota exceeded',r['error'])
        self.assertGreater(r['log_quota']['total_bytes'],8)
        self.assertTrue(r['failure_front']['captured']);self.assertTrue((self.output/'chromium.log').read_text())
        self.assertEqual(r['front_samples'],[]);self.assertEqual(r['cleanup_wait'],0)

    def test_normal_mode_does_not_enable_probe_or_log_quota(self):
        with patch.object(edge,'LOG_LIMIT',1):code,r=self.execute()
        self.assertEqual(code,0);self.assertNotIn('fixture_file_probe',r);self.assertNotIn('log_quota',r)
        self.assertNotIn('+file',self.popen.call_args.kwargs['env']['WINEDEBUG'])

    def test_probe_failure_gates_browser(self):
        self.startup_diagnostic=True;self.codes['fixture-file-probe']=1
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('file probe failed',r['error'])
        self.popen.assert_not_called();self.assertNotIn('failure_front',r)

    def test_probe_protocol_positive_negative(self):
        data=self.fixture.read_bytes()
        value=dict(ok=True,error=0,size=len(data),read=min(64,len(data)),prefix_hex=data[:64].hex())
        self.assertTrue(edge.fixture_probe_record(json.dumps(value),self.fixture)['ok'])
        for key,bad in [('ok',False),('size',999),('read',0),('prefix_hex','ff'),('error',5)]:
            wrong=dict(value);wrong[key]=bad
            with self.subTest(key=key),self.assertRaises(ValueError):edge.fixture_probe_record(json.dumps(wrong),self.fixture)
        with self.assertRaises(ValueError):edge.fixture_probe_record(json.dumps(value)+'junk',self.fixture)

    def test_quota_limit_is_combined_and_boundary_inclusive(self):
        self.output.mkdir()
        for name in ('browser.stdout.log','browser.stderr.log','chromium.log'):(self.output/name).write_bytes(b'123')
        with patch.object(edge,'LOG_LIMIT',9):self.assertTrue(edge.diagnostic_log_usage(self.output)['passed'])
        with patch.object(edge,'LOG_LIMIT',8):self.assertFalse(edge.diagnostic_log_usage(self.output)['passed'])

    def test_corrupt_terminal_capture_preserves_primary_and_raw_file(self):
        self.failures['native-0']=RuntimeError('PRIMARY')
        original=self.helpers.run_logged
        def logged(*args):
            value=original(*args)
            if args[3]=='failure-front-capture':Path(args[0][-1]).write_bytes(b'corrupt PNG')
            return value
        self.helpers.run_logged=logged
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('PRIMARY',r['error'])
        self.assertIn('error',r['failure_front']);self.assertEqual((self.output/'failure-front.png').read_bytes(),b'corrupt PNG')

    def test_mismatched_probe_success_exit_still_gates_browser(self):
        self.startup_diagnostic=True
        original=self.helpers.run_logged
        def logged(*args):
            value=original(*args)
            if args[3]=='fixture-file-probe':value.stdout='{"ok":true,"error":0,"size":0,"read":0,"prefix_hex":""}'
            return value
        self.helpers.run_logged=logged
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('probe failed/mismatched',r['error'])
        self.popen.assert_not_called();self.assertNotIn('failure_front',r)

    def test_probe_timeout_gates_browser(self):
        self.startup_diagnostic=True;self.failures['fixture-file-probe']=subprocess.TimeoutExpired('probe',5)
        code,r=self.execute();self.assertEqual(code,1);self.assertIn('TimeoutExpired',r['error'])
        self.popen.assert_not_called();self.assertNotIn('failure_front',r)

    def test_log_close_failure_preserves_primary_and_results(self):
        self.failures['native-0']=RuntimeError('PRIMARY')
        original=Path.open
        class FailingClose:
            def __init__(self,stream):self.stream=stream
            def close(self):self.stream.close();raise OSError('SECONDARY LOG CLOSE')
        def opened(path,*args,**kwargs):
            stream=original(path,*args,**kwargs)
            return FailingClose(stream) if path.name=='browser.stdout.log' and args and args[0]=='wb' else stream
        with patch.object(Path,'open',opened):code,r=self.execute()
        self.assertEqual(code,1);self.assertIn('PRIMARY',r['error'])
        self.assertIn('SECONDARY LOG CLOSE',str(r['cleanup_errors']))

    def test_cli_dispatches_explicit_mode(self):
        import sys
        with patch.object(sys,'argv',['check-g4-edge-headful.py','--output-dir',str(self.output),
                                     '--fixture',str(self.fixture),'--startup-diagnostic']), \
             patch.object(edge,'run',return_value=1) as run,patch.object(edge,'load',return_value=self.helpers):
            self.assertEqual(edge.main(),1)
            self.assertTrue(run.call_args.kwargs['startup_diagnostic'])


if __name__=='__main__':unittest.main()
