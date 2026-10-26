# SPDX-License-Identifier: GPL-2.0-or-later
import contextlib
import io
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

import validate_g4_appcontainer_independent as r

LAB=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')
def empty(*args):return {'status':'complete','busy':False}

class RunnerTests(unittest.TestCase):
    def test_real_cell_flow_override_is_only_on_init_and_scoped_init_cleanup(self):
        for failed in (False,True):
            with self.subTest(failed=failed),tempfile.TemporaryDirectory(prefix='ac-r2-test-',dir=LAB) as folder:
                root=Path(folder);(root/'prefixes').mkdir();calls=[]
                def popen(argv,**kwargs):
                    calls.append((argv,dict(kwargs['env'])))
                    init=argv[-1]=='--init'
                    if not init:
                        kwargs['stdout'].write(b'0040:security: 49 tests executed (0 marked as todo, 0 failures), 0 skipped.\n')
                        kwargs['stdout'].flush()
                        kwargs['stderr'].write(b'\n'.join(('trace:loaddll:build_module '+n+'.dll: builtin').encode() for n in ('kernelbase','userenv','ntdll')))
                        kwargs['stderr'].flush()
                    return SimpleNamespace(pid=123,wait=lambda **k:1 if init and failed else 0,poll=lambda:0)
                cleanups=[]
                def shutdown(argv,**kwargs):cleanups.append((argv,dict(kwargs['env'])));return SimpleNamespace(returncode=0)
                with mock.patch.object(r,'EVIDENCE',root),mock.patch.object(r,'PREFIX_ROOT',root/'prefixes'),mock.patch.object(r,'sha',return_value='hash'),mock.patch.object(r,'pe_machine',return_value=0x8664),mock.patch.object(r,'scan',side_effect=empty),mock.patch.object(r.subprocess,'Popen',side_effect=popen),mock.patch.object(r.subprocess,'run',side_effect=shutdown):
                    value=r.cell('x86_64','kernelbase',':owned')
                self.assertEqual(calls[0][0][-2:],['wineboot','--init'])
                self.assertEqual(calls[0][1]['WINEDLLOVERRIDES'],'mscoree,mshtml=')
                self.assertTrue(value['cleanup_complete'])
                self.assertEqual(len(calls),1 if failed else 2)
                if failed:self.assertFalse(value['passed']);self.assertIn('initialization failed',value['error'])
                else:
                    self.assertNotIn('WINEDLLOVERRIDES',calls[1][1]);self.assertTrue(value['passed'])
                    self.assertFalse(value['unit_environment_has_override'])
                self.assertTrue(all(c[0][0]==str(r.BUILD/'server/wineserver') for c in cleanups))
                self.assertTrue(all(c[1]['WINEPREFIX']==value['prefix'] for c in cleanups))
                self.assertTrue(all('WINEDLLOVERRIDES' not in env for _,env in cleanups[2:]))

    def test_init_timeout_checked_reap_and_cleanup_preserve_primary(self):
        for cleanup_failed in (False,True):
            with tempfile.TemporaryDirectory(prefix='ac-r2-timeout-',dir=LAB) as folder:
                root=Path(folder);child=mock.Mock(pid=123);child.poll.return_value=None
                child.wait.side_effect=[subprocess.TimeoutExpired('wineboot',90),-9]
                def shutdown(*args,**kwargs):
                    if cleanup_failed:raise RuntimeError('SECONDARY cleanup')
                    return SimpleNamespace(returncode=0)
                value={};env={'WINEPREFIX':str(root/'prefix')}
                with mock.patch.object(r,'EVIDENCE',root),mock.patch.object(r,'scan',side_effect=empty),mock.patch.object(r.os,'killpg') as kill:
                    with self.assertRaises(subprocess.TimeoutExpired):r.initialize(env,'owned',value,popen=lambda *a,**k:child,run_command=shutdown)
                kill.assert_called_once();self.assertTrue(value['initialization']['observer_reaped'])
                self.assertIn('TimeoutExpired',value['initialization']['error'])
                self.assertEqual(bool(value['initialization']['cleanup_errors']),cleanup_failed)
                self.assertNotIn('WINEDLLOVERRIDES',env)

    def test_init_cleanup_or_owner_failure_rejects_successful_init(self):
        with tempfile.TemporaryDirectory(prefix='ac-r2-cleanup-',dir=LAB) as folder:
            child=SimpleNamespace(pid=123,wait=lambda **k:0,poll=lambda:0)
            with mock.patch.object(r,'EVIDENCE',Path(folder)),mock.patch.object(r,'scan',return_value={'status':'unknown','busy':None}):
                value={}
                with self.assertRaisesRegex(RuntimeError,'Init cleanup failed'):
                    r.initialize({'WINEPREFIX':folder},'owned',value,popen=lambda *a,**k:child,run_command=lambda *a,**k:SimpleNamespace(returncode=0))
                self.assertFalse(value['initialization']['passed'])

    def test_existing_override_rejected_before_any_init_process(self):
        popen=mock.Mock()
        with self.assertRaises(ValueError):r.initialize({'WINEDLLOVERRIDES':'foreign'},'x',{},popen=popen)
        popen.assert_not_called()

    def test_matrix_failfast_and_success_order(self):
        for failed in (True,False):
            calls=[];summary={'cells':[]}
            def run_cell(a,d,display):calls.append((a,d));return {'passed':not failed,'cleanup_complete':True}
            with contextlib.redirect_stdout(io.StringIO()):
                if failed:
                    with self.assertRaisesRegex(RuntimeError,'first init/test/cleanup'):r.matrix(':owned',summary,run_cell)
                else:r.matrix(':owned',summary,run_cell)
            self.assertEqual(len(calls),1 if failed else 4)
            self.assertEqual(len(summary['cells']),len(calls))
            if not failed:self.assertEqual(calls,[('x86_64','kernelbase'),('x86_64','userenv'),('i386','kernelbase'),('i386','userenv')])

if __name__=='__main__':unittest.main()
