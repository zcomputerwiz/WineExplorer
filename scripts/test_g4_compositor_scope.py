# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual native /proc transport; no compositor, Wine, mount or VM execution."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import graphics_resources
import graphics_wexcomp
import test_g4_chromium_native_exit as native
import g4_userenv_view as view
import g4_userenv_probe as probe
import test_g4_userenv_view as probe_tests
import json
from types import SimpleNamespace

LAB=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')

class ScopeTests(unittest.TestCase):
    def test_real_native_children_default_opt_in_and_marked_owner(self):
        self.assertEqual(os.name,'posix')
        real_popen=subprocess.Popen
        with tempfile.TemporaryDirectory(prefix='scope-',dir=LAB) as folder:
            root=Path(folder);prefix=root/'prefix'
            owner=real_popen([sys.executable,'-c','import time;time.sleep(30)'],env={**os.environ,'WINEPREFIX':str(prefix)})
            children=[]
            try:
                for strip in (False,True):
                    output=root/str(strip);output.mkdir()
                    env={**os.environ,'WINEPREFIX':str(prefix),'DISPLAY':':unused','WAYLAND_SOCKET':'unused'}
                    comp=graphics_wexcomp.OwnedWexcomp(Path(sys.executable),root,output,env,strip_wineprefix=strip)
                    children.append(comp)
                    def launch(argv,**kwargs):
                        return real_popen([sys.executable,'-c','import time;time.sleep(30)'],**kwargs)
                    def protocol(argv,**kwargs):
                        self.assertEqual(argv,['wayland-info'])
                        kwargs['stdout'].write(b'wex_window_manager_v1\n')
                        return subprocess.CompletedProcess(argv,0)
                    # Endpoint/protocol are fake; the child environment and /proc sampler are real.
                    with mock.patch.object(graphics_wexcomp.subprocess,'Popen',side_effect=launch),mock.patch.object(graphics_wexcomp.subprocess,'run',side_effect=protocol),mock.patch.object(Path,'is_socket',return_value=True):
                        record=comp.start()
                    self.assertEqual(env['WINEPREFIX'],str(prefix))
                    self.assertEqual(env['WAYLAND_DISPLAY'],'wex-shell-0')
                    self.assertEqual(env['XDG_RUNTIME_DIR'],str(output/'wayland-runtime'))
                    self.assertNotIn('DISPLAY',env);self.assertNotIn('WAYLAND_SOCKET',env)
                    self.assertEqual(record['wineprefix_stripped'],strip)
                    sampled=graphics_resources.sample(prefix)
                    self.assertIn(str(owner.pid),sampled)
                    self.assertEqual(str(comp.process.pid) in sampled,not strip)
                    self.assertEqual(sampled[str(owner.pid)]['start_ticks'],int(Path(f'/proc/{owner.pid}/stat').read_text().rsplit(')',1)[1].split()[19]))
                children[0].stop()
                remaining=graphics_resources.sample(prefix)
                self.assertEqual(set(remaining),{str(owner.pid)}) # no PID exclusion in sampler
                result={}
                def fake_probe(argv,**kwargs):
                    kwargs['stdout'].write(json.dumps(probe_tests.payload(argv[-1])).encode());kwargs['stdout'].flush()
                    return SimpleNamespace(poll=lambda:0,wait=lambda **k:0)
                helpers=SimpleNamespace(run_logged=lambda *a:SimpleNamespace(returncode=0),matching_servers=lambda *a:[])
                with self.assertRaisesRegex(RuntimeError,'not idle'):
                    probe.phase(root,env,result,root/'probe',root/'build',prefix,helpers,lambda:None,
                                graphics_resources.sample,__import__('time').monotonic()+60,popen=fake_probe)
                self.assertIn(str(owner.pid),result['api_probe']['remaining_processes'])
            finally:
                for comp in children:
                    comp.stop()
                    if comp.process is not None:self.assertIsNotNone(comp.process.poll())
                owner.terminate();owner.wait(timeout=5)
            self.assertEqual(graphics_resources.sample(prefix),{})

    def test_actual_control_opts_in_only_r4(self):
        r=native.r
        try:
            for mode in ('r1','module-warn-r2','userenv-r3','userenv-r4'):
                r.configure(mode)
                with tempfile.TemporaryDirectory(prefix='scope-runner-',dir=LAB) as folder:
                    output=Path(folder)/'out'
                    env={k:str(output/k.lower()) for k in ('HOME','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','TMPDIR')}
                    comp=mock.Mock();comp.start.side_effect=RuntimeError('stop before rendering')
                    helpers=mock.Mock();helpers.matching_servers.return_value=[]
                    helpers.run_logged.return_value.returncode=0
                    with mock.patch.object(r,'guards',return_value={'private_loopback':{}}),mock.patch.object(r.cal.install,'environment',return_value=env),mock.patch.object(r.cal,'OwnedWexcomp',return_value=comp) as constructor,mock.patch.object(r.cal.graphics_resources,'sample',return_value={}),mock.patch.object(r.cal.isolation,'verify_private_loopback',return_value={}):
                        self.assertEqual(r.run(output,helpers),1)
                    self.assertEqual(constructor.call_args.kwargs,{'strip_wineprefix':True} if mode=='userenv-r4' else {})
                    self.assertEqual(env['WINEPREFIX'],str(r.PREFIX))
        finally:r.configure('r1')

    def test_fixed_r4_view_and_runner_contract(self):
        r=native.r
        try:
            r.configure('userenv-r4');view.configure('userenv-r4')
            self.assertEqual(view.STAGE,r.STAGE);self.assertEqual(view.OUTPUT,r.OUTPUT)
            self.assertEqual(r.HOST,'chromium-userenv-r4-20261025')
            self.assertEqual(len(r.child_args(native.NONCE,native.URL)),8)
            calls=[]
            with mock.patch.object(view,'selected',return_value=[]):
                view.execute(validate_context=lambda:{},run=lambda *a,**k:None,
                             emit=lambda *a,**k:calls.append('emit'),execv=lambda p,a:calls.append(a))
            self.assertEqual(calls[0],'emit')
            self.assertEqual(calls[1][-3:],['--userenv-r4','--output-dir',str(r.OUTPUT)])
            with mock.patch.object(r.sys,'argv',['native','--userenv-r4']),mock.patch.object(r,'load'),mock.patch.object(r,'run',return_value=1) as run:
                self.assertEqual(r.main(),1);self.assertEqual(run.call_args.args[0],r.OUTPUT)
            with self.assertRaises(ValueError):view.configure('arbitrary')
        finally:r.configure('r1');view.configure('userenv-r3')

if __name__=='__main__':unittest.main()
