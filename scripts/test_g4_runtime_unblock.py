# SPDX-License-Identifier: GPL-2.0-or-later
"""Self-contained offline unit tests for the Chromium runtime-unblock guard fix and its fixed
diagnostic identity (chromium-runtime-unblock-20261026). These exercise only pure Python logic
(the runtime_state file-set guard, mode configuration, CLI wiring, and module-view selection);
no VM/Wine/browser is started. Run from scripts/: python3 -B -m unittest test_g4_runtime_unblock."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

LAB=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')
REQUIRED=('chrome.exe','chrome.dll','chrome_elf.dll','D3DCompiler_47.dll','libEGL.dll','libGLESv2.dll')
HOST='chromium-runtime-unblock-20261026'

def _load(name):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(name+'.py'))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module);return module

r=_load('check-g4-chromium-native-exit')
view=_load('g4_userenv_view')

def _pe():
    data=bytearray(128);data[:2]=b'MZ';struct.pack_into('<I',data,60,64);data[64:68]=b'PE\0\0';struct.pack_into('<H',data,68,0x8664)
    return bytes(data)

def _write_runtime(runtime,missing=(),extra=()):
    runtime.mkdir(parents=True)
    for name in REQUIRED:
        if name not in missing:(runtime/name).write_bytes(_pe())
    for name in extra:(runtime/name).write_bytes(b'surplus generated artifact')

def _manifest(root):
    files={f'chrome-win/{name}':{'bytes':128,'sha256':'0'*64} for name in REQUIRED}
    manifest=root/'manifest.json';manifest.write_text(json.dumps(dict(files=files,zip_sha256='0'*64)))
    return manifest

@unittest.skipUnless(LAB.is_dir(),'D: private storage unavailable')
class RuntimeUnblockGuardTests(unittest.TestCase):
    def _state(self,temp,missing=(),extra=()):
        root=Path(temp);runtime=root/'vendor/extracted/chrome-win';manifest=_manifest(root)
        _write_runtime(runtime,missing=missing,extra=extra)
        with mock.patch.object(r,'RUNTIME',runtime),mock.patch.object(r,'MANIFEST',manifest):
            return r.runtime_state()

    def test_canonical_reuse_complete_no_extras(self):
        with tempfile.TemporaryDirectory(prefix='ru-canonical-',dir=LAB) as temp:
            state=self._state(temp)
            self.assertEqual(state['state'],'complete')
            self.assertEqual(state['extras'],[])
            self.assertEqual(set(state['files']),{f'chrome-win/{n}' for n in REQUIRED})

    def test_benign_surplus_completes_and_reports_extras(self):
        with tempfile.TemporaryDirectory(prefix='ru-surplus-',dir=LAB) as temp:
            state=self._state(temp,extra=('debug.log','crashpad.dat'))
            self.assertEqual(state['state'],'complete')
            self.assertEqual(sorted(state['extras']),['chrome-win/crashpad.dat','chrome-win/debug.log'])
            # Required files are still observed; the surplus is reported but not observed.
            self.assertEqual(set(state['files']),{f'chrome-win/{n}' for n in REQUIRED})

    def test_missing_required_file_fails(self):
        with tempfile.TemporaryDirectory(prefix='ru-missing-',dir=LAB) as temp:
            with self.assertRaisesRegex(ValueError,'Missing required runtime files'):
                self._state(temp,missing=('chrome.dll',))

    def test_symlink_inside_runtime_rejected(self):
        with tempfile.TemporaryDirectory(prefix='ru-link-',dir=LAB) as temp:
            root=Path(temp);runtime=root/'vendor/extracted/chrome-win';manifest=_manifest(root)
            _write_runtime(runtime)
            (runtime/'evil.dll').symlink_to(runtime/'chrome.dll')
            with mock.patch.object(r,'RUNTIME',runtime),mock.patch.object(r,'MANIFEST',manifest):
                with self.assertRaisesRegex(ValueError,'Runtime link not allowed'):r.runtime_state()


class RuntimeUnblockConfigTests(unittest.TestCase):
    def test_runtime_unblock_mode_paths_agree_across_scripts(self):
        try:
            r.configure('runtime-unblock')
            self.assertEqual(r.MODE,'runtime-unblock')
            self.assertEqual(r.HOST,HOST)
            self.assertEqual(r.STAGE,r.LAB/(HOST+'-stage'))
            self.assertEqual(r.OUTPUT,r.LAB/HOST)
            self.assertEqual(r.PROFILE,r.OUTPUT/'profile')
        finally:r.configure('r1')
        try:
            view.configure('runtime-unblock')
            self.assertEqual(view.MODE,'runtime-unblock')
            self.assertEqual(view.STAGE,view.LAB/(HOST+'-stage'))
            self.assertEqual(view.OUTPUT,view.LAB/HOST)
        finally:view.configure('userenv-r3')

    def test_runtime_unblock_cli_flag_selects_mode_and_rejects_combination(self):
        try:
            with mock.patch.object(r.sys,'argv',['native','--runtime-unblock']),mock.patch.object(r,'load'),mock.patch.object(r,'run',return_value=1) as run:
                self.assertEqual(r.main(),1)
                self.assertEqual(r.MODE,'runtime-unblock')
                self.assertEqual(run.call_args.args[0],r.LAB/HOST)
            with mock.patch.object(r.sys,'argv',['native','--runtime-unblock','--userenv-r6']),contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:r.main()
                self.assertEqual(error.exception.code,2)
        finally:r.configure('r1')

    def test_runtime_unblock_selected_modules_both_arches(self):
        try:
            view.configure('runtime-unblock')
            sel=view.selected()
            self.assertEqual(len(sel),4)
            self.assertEqual(sorted(Path(t).name for _,t,_ in sel),['kernelbase.dll','kernelbase.dll','userenv.dll','userenv.dll'])
            self.assertEqual(sorted(m for _,_,m in sel),[0x14c,0x14c,0x8664,0x8664])
        finally:view.configure('userenv-r3')


if __name__=='__main__':unittest.main()
