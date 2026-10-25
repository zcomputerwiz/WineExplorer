# SPDX-License-Identifier: GPL-2.0-or-later
"""Focused R5 adapter tests; remote execution mocked, no SSH/VM/browser/API execution."""
import io
import importlib.util
import json
from pathlib import Path
import shlex
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock
import test_g4_chromium_native_adapter as original

REPO=original.REPO;LAB=original.ownership.LAB
PRIVATE=REPO/'build/g4-virgl-20261022/chromium-userenv-r5-trial.py'

def adapter():
    spec=importlib.util.spec_from_file_location('r5_adapter',PRIVATE);p=importlib.util.module_from_spec(spec);spec.loader.exec_module(p);return p

@unittest.skipUnless(PRIVATE.is_file() and LAB.is_dir(),'private R5 adapter unavailable')
class UserenvR5AdapterTests(unittest.TestCase):
    def test_fixed_r5_paths_mode_and_compiled_contract(self):
        p=adapter();argv=p.guest_consumer()
        self.assertEqual(p.NAME,'chromium-userenv-r5-20261025')
        self.assertNotIn('runuser',argv)
        self.assertEqual(argv[-3:],['-B',p.STAGE+'/scripts/g4_userenv_view.py','--userenv-r5'])
        for name in ('scripts/g4_userenv_view.py','scripts/g4_userenv_probe.py'):self.assertIn(name,p.sources())
        self.assertEqual(p.PE,p.LAB/'userenv-browser-r5-20261025.build/native-exit.exe')
        self.assertEqual(p.API_BUILD_DIR,p.LAB/'userenv-browser-r3-20261024.build')
        self.assertEqual(p.STAGE,'/home/debian/wex-g4/chromium-userenv-r5-20261025-stage')
        self.assertEqual(p.OUTPUT,'/home/debian/wex-g4/chromium-userenv-r5-20261025')
        self.assertEqual(len(p.compiled_contract()),6)

    def test_transitive_source_inventory_includes_actual_imports_and_safe_transfer(self):
        p=adapter();files=p.sources()
        for name in ('check-g4-chromium-native-exit.py','check-g4-native-exit-calibration.py','g4_diagnostic_inventory.py','g4_edge_owned_threads.py','g4_edge_http_fixture.py','g4_chromium_cache.py','graphics_resources.py','graphics_wexcomp.py','run-isolated-graphics.py','check-g4-edge-install.py','check-g3-correctness.py'):
            self.assertIn('scripts/'+name,files)
        self.assertFalse(any('vendor' in n for n in files))

    def test_diagnostic1_with_complete_archive_is_orchestration_not_product_failure(self):
        with tempfile.TemporaryDirectory(prefix='r5-adapter-',dir=LAB) as folder:
            p=adapter();root=Path(folder);p.LAB=root;p.RUN=root/'run';p.RUN.mkdir();p.PREP=root/'prep';p.PREP.mkdir()
            payload=b'x';digest=__import__('hashlib').sha256(payload).hexdigest()
            with tarfile.open(p.PREP/'source-stage.tar','w') as archive:
                member=tarfile.TarInfo('own.py');member.size=1;archive.addfile(member,io.BytesIO(payload))
            record=dict(disk=str(root/'system-native-identity-r4-20261024.qcow2'),archive_sha256=p.sha(p.PREP/'source-stage.tar'),guest_stage=p.STAGE,
                compiled_output=p.OUTPUT,compiled_runtime=p.RUNTIME,guest_consumer_argv=p.guest_consumer(),transfer_files={'own.py':{'sha256':digest}})
            p.save(p.PREP/'preflight.json',record);p.save(p.RUN/'launcher-identity.json',dict(qemu_pid=1,qemu_start_ticks=1,xvfb_pid=2,xvfb_start_ticks=2))
            (root/(p.NAME+'.wsl-session.json')).write_text('{}');calls=[]
            def dispatch(argv,**kwargs):
                c=shlex.split(argv[-1]);calls.append(c)
                if c==p.guest_consumer():return subprocess.CompletedProcess(argv,1)
                if c[:3]==['tar','-C',p.OUTPUT]:
                    with tarfile.open(fileobj=kwargs['stdout'],mode='w:gz') as archive:
                        data=b'{"diagnostic_exit":1,"passed":false}';item=tarfile.TarInfo('results.json');item.size=len(data);archive.addfile(item,io.BytesIO(data))
                    return subprocess.CompletedProcess(argv,0)
                if c==p.outside_argv():out=b'{"module":{"sha256":"observed","bytes":1}}'
                elif c[0]=='sudo':
                    value=original.ownership.complete();value['nonce']=c[-1];out=original.ownership.wire(value)
                elif c[0]=='sha256sum':out=(digest+'  '+p.STAGE+'/own.py\n').encode()
                elif '--runtime-probe' in c:
                    self.assertIn('--userenv-r5',c);out=b'{"state":"complete"}'
                elif c[0]=='python3' and 'disk_usage' in c[-1]:out=b'2000000000\n'
                elif c[0] in ('python3','mkdir','tar'):out=b''
                else:raise AssertionError(c)
                return subprocess.CompletedProcess(argv,0,out,b'')
            with mock.patch.object(p,'validate_identity',return_value={}),mock.patch.object(p,'proc_identity',side_effect=lambda pid:dict(start_ticks=pid,cmdline=['file='+record['disk']+',format=qcow2,if=virtio'])),mock.patch.object(p,'wait_ready',return_value={'ready':True}),mock.patch.object(p,'check_budget',return_value={'passed':True,'remaining_seconds':880}),mock.patch.object(p.subprocess,'run',side_effect=dispatch),mock.patch('builtins.print'):p.run()
            value=json.loads((p.RUN/'execution.json').read_text());self.assertTrue(value['orchestration_complete'])
            self.assertEqual(value['outside_modules_before'],value['outside_modules_after'])
            positions=[i for i,c in enumerate(calls) if c==p.outside_argv()]
            consumer=next(i for i,c in enumerate(calls) if c==p.guest_consumer())
            self.assertEqual(len(positions),2);self.assertLess(positions[0],consumer);self.assertGreater(positions[1],consumer)

if __name__=='__main__':unittest.main()
