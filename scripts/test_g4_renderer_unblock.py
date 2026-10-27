# SPDX-License-Identifier: GPL-2.0-or-later
"""Self-contained offline unit tests for the Chromium renderer-unblock mode-family wiring.
These exercise only pure Python selection logic in check-g4-chromium-native-exit.run()
(the compositor strip_wineprefix option and the pre-browser probe dispatch), the fixed
renderer-unblock diagnostic identity (chromium-renderer-unblock-20261026) agreed across both
scripts, module-view selection, and CLI wiring. No VM/Wine/browser is started. Run from
scripts/: python3 -B -m unittest test_g4_renderer_unblock."""
import contextlib
import importlib.util
import io
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import g4_userenv_probe as probe3
import g4_userenv_r6_probe as probe6

HOST='chromium-renderer-unblock-20261026'

def _load(name):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(name+'.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

r=_load('check-g4-chromium-native-exit')
view=_load('g4_userenv_view')

def _drive(mode):
    """Drive the real run() far enough to observe the compositor option and probe dispatch for
    mode, with every external effect mocked. Return (owned_wexcomp_kwargs, r3r4_probe_called,
    r6_probe_called)."""
    r.configure(mode)
    with tempfile.TemporaryDirectory(prefix='renunblock-') as temp:
        output=Path(temp)/'output'
        env={k:str(output/k.lower()) for k in ('HOME','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','TMPDIR')}
        helpers=mock.Mock();comp=mock.Mock();owc=mock.Mock(return_value=comp)
        server=mock.Mock();server.record={};server.start.return_value='http://127.0.0.1:43210/nonce/index.html#nonce=nonce'
        with mock.patch.object(r,'guards',return_value={'private_loopback':{'same':1}}), \
             mock.patch.object(r.cal.install,'environment',return_value=env), \
             mock.patch.object(r.cal,'OwnedWexcomp',owc), \
             mock.patch.object(r.cal,'verify_native_gl_driver',return_value={}), \
             mock.patch.object(r,'FixtureServer',return_value=server), \
             mock.patch.object(r.cal,'collect',return_value=None), \
             mock.patch.object(r.cal,'journal',return_value=b''), \
             mock.patch.object(r.cal,'logs',return_value=None), \
             mock.patch.object(r.cal,'save',return_value=None), \
             mock.patch.object(r.cal.graphics_resources,'sample',return_value={}), \
             mock.patch.object(r.cal.isolation,'verify_private_loopback',return_value={'same':1}), \
             mock.patch.object(probe3,'phase') as p3, \
             mock.patch.object(probe6,'phase') as p6:
            r.run(output,helpers)
    return owc.call_args.kwargs,p3.called,p6.called


class RendererUnblockCompositorTests(unittest.TestCase):
    def test_strip_wineprefix_selected_for_scoped_modes_only(self):
        try:
            for mode in ('userenv-r4','userenv-r6','runtime-unblock','renderer-unblock'):
                kwargs,_,_=_drive(mode)
                self.assertEqual(kwargs.get('strip_wineprefix'),True,mode)
            for mode in ('r1','module-warn-r2','userenv-r3'):
                kwargs,_,_=_drive(mode)
                self.assertNotIn('strip_wineprefix',kwargs,mode)
        finally:r.configure('r1')

    def test_probe_dispatch_r6_for_scoped_and_normal_for_r3_r4(self):
        try:
            for mode in ('userenv-r6','runtime-unblock','renderer-unblock'):
                _,p3,p6=_drive(mode)
                self.assertTrue(p6,mode);self.assertFalse(p3,mode)
            for mode in ('userenv-r3','userenv-r4'):
                _,p3,p6=_drive(mode)
                self.assertTrue(p3,mode);self.assertFalse(p6,mode)
        finally:r.configure('r1')


class RendererUnblockConfigTests(unittest.TestCase):
    def test_renderer_unblock_mode_paths_agree_across_scripts(self):
        try:
            r.configure('renderer-unblock')
            self.assertEqual(r.MODE,'renderer-unblock')
            self.assertEqual(r.HOST,HOST)
            self.assertEqual(r.STAGE,r.LAB/(HOST+'-stage'))
            self.assertEqual(r.OUTPUT,r.LAB/HOST)
            self.assertEqual(r.PROFILE,r.OUTPUT/'profile')
        finally:r.configure('r1')
        try:
            view.configure('renderer-unblock')
            self.assertEqual(view.MODE,'renderer-unblock')
            self.assertEqual(view.STAGE,view.LAB/(HOST+'-stage'))
            self.assertEqual(view.OUTPUT,view.LAB/HOST)
        finally:view.configure('userenv-r3')

    def test_renderer_unblock_selected_modules_both_arches(self):
        try:
            view.configure('renderer-unblock')
            sel=view.selected()
            self.assertEqual(len(sel),4)
            self.assertEqual(sorted(Path(t).name for _,t,_ in sel),['kernelbase.dll','kernelbase.dll','userenv.dll','userenv.dll'])
            self.assertEqual(sorted(m for _,_,m in sel),[0x14c,0x14c,0x8664,0x8664])
        finally:view.configure('userenv-r3')

    def test_renderer_unblock_cli_flag_selects_mode_and_rejects_combination(self):
        try:
            with mock.patch.object(r.sys,'argv',['native','--renderer-unblock']),mock.patch.object(r,'load'),mock.patch.object(r,'run',return_value=1) as run:
                self.assertEqual(r.main(),1)
                self.assertEqual(r.MODE,'renderer-unblock')
                self.assertEqual(run.call_args.args[0],r.LAB/HOST)
            with mock.patch.object(r.sys,'argv',['native','--renderer-unblock','--runtime-unblock']),contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:r.main()
                self.assertEqual(error.exception.code,2)
        finally:r.configure('r1')


if __name__=='__main__':unittest.main()
