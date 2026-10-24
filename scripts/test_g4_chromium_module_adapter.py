# SPDX-License-Identifier: GPL-2.0-or-later
"""Exercise the explicit R2 private adapter with host-only fake guest callbacks."""
from pathlib import Path
import tempfile
import unittest
from unittest import mock

import test_g4_chromium_native_adapter as original
import test_g4_chromium_native_exit as native

PRIVATE=original.REPO/'build/g4-virgl-20261022/chromium-module-trial.py'

@unittest.skipUnless(PRIVATE.is_file() and original.ownership.LAB.is_dir(),'private R2 source/storage unavailable')
class ModuleAdapterTests(original.AdapterTests):
    def setUp(self):
        patch=mock.patch.object(original,'PRIVATE',PRIVATE);patch.start();self.addCleanup(patch.stop)

    def test_fixed_r2_contract_pe_and_cli_agree_with_actual_runner(self):
        p=original.adapter();r=native.r
        self.assertEqual(p.NAME,'chromium-native-module-r2-20261024')
        self.assertEqual(p.STAGE,'/home/debian/wex-g4/'+p.NAME+'-stage')
        self.assertEqual(p.OUTPUT,'/home/debian/wex-g4/'+p.NAME)
        self.assertEqual(p.PE,p.LAB/'native-module-r2-20261024.build/native-exit.exe')
        self.assertEqual(p.guest_consumer().count('--module-warn-r2'),1)
        self.assertIn('--private-loopback',p.guest_consumer())
        try:
            r.configure('module-warn-r2')
            self.assertEqual(str(r.STAGE),p.STAGE);self.assertEqual(str(r.OUTPUT),p.OUTPUT)
            self.assertEqual(str(r.RUNTIME),p.RUNTIME)
            self.assertEqual(p.guest_consumer()[-3:],['--module-warn-r2','--output-dir',str(r.OUTPUT)])
        finally:r.configure('r1')
        self.assertEqual(r.HOST,'chromium-native-exit-r1-20261024')

    def test_actual_contract_reader_rejects_original_output_and_keeps_header_default(self):
        p=original.adapter()
        actual=p.compiled_contract()
        self.assertEqual(len(actual),6)
        self.assertTrue(all(v['observation_complete'] for v in actual.values()))
        header=(p.REPO/'scripts/g4_native_exit_contract.h').read_bytes()
        override=(p.BUILD_DIR/'output-only.h').read_text()
        with tempfile.TemporaryDirectory(prefix='r2-contract-',dir=original.ownership.LAB) as folder:
            root=Path(folder);(root/'scripts').mkdir();(root/'build').mkdir()
            (root/'scripts/g4_native_exit_contract.h').write_bytes(header)
            path=root/'build/output-only.h'
            path.write_text(override.replace(p.NAME,'chromium-headful-native-exit-r1-20261023'))
            with mock.patch.object(p,'REPO',root),mock.patch.object(p,'BUILD_DIR',root/'build'):
                with self.assertRaisesRegex(ValueError,'R2 compiled contract'):p.compiled_contract()
                path.write_text(override)
                # Missing optional receipt observations remain diagnostic, not
                # historical approval gates; fixed contract itself is required.
                self.assertEqual(len(p.compiled_contract()),6)

if __name__=='__main__':unittest.main()
