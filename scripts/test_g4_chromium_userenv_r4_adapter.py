# SPDX-License-Identifier: GPL-2.0-or-later
import io
import json
from pathlib import Path
import shlex
import subprocess
import tarfile
import tempfile
import unittest
from unittest import mock
import test_g4_chromium_native_adapter as original

PRIVATE=original.REPO/'build/g4-virgl-20261022/chromium-userenv-r4-trial.py'

@unittest.skipUnless(PRIVATE.is_file() and original.ownership.LAB.is_dir(),'private R4 adapter unavailable')
class UserenvAdapterTests(original.AdapterTests):
    def setUp(self):
        patch=mock.patch.object(original,'PRIVATE',PRIVATE);patch.start();self.addCleanup(patch.stop)

    def test_fixed_root_view_instead_of_runuser_passthrough(self):
        p=original.adapter();argv=p.guest_consumer()
        self.assertEqual(p.NAME,'chromium-userenv-r4-20261025')
        self.assertNotIn('runuser',argv)
        self.assertEqual(argv[-3:],['-B',p.STAGE+'/scripts/g4_userenv_view.py','--userenv-r4'])
        for name in ('scripts/g4_userenv_view.py','scripts/g4_userenv_probe.py'):self.assertIn(name,p.sources())
        self.assertEqual(p.PE,p.LAB/'userenv-browser-r4-20261025.build/native-exit.exe')
        self.assertEqual(len(p.compiled_contract()),6)

    def test_diagnostic1_with_complete_archive_is_orchestration_not_product_failure(self):
        with tempfile.TemporaryDirectory(prefix='r3-adapter-',dir=original.ownership.LAB) as folder:
            p=original.adapter();root=Path(folder);p.LAB=root;p.RUN=root/'run';p.RUN.mkdir();p.PREP=root/'prep';p.PREP.mkdir()
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
                    self.assertIn('--userenv-r4',c);out=b'{"state":"complete"}'
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
