# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile/run shared pure native logic on Linux; NEVER execute Win32 PE/Wine."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class NativeContractTests(unittest.TestCase):
    def test_actual_shared_native_logic_with_strict_compiler(self):
        if os.name!='posix':self.fail('Run this actual host control on authorized Linux/WSL, no shim or skip')
        cc=shutil.which('cc')
        self.assertIsNotNone(cc,'Actual host C compiler required')
        lab=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')
        self.assertTrue(lab.is_dir(),'All test output/temp must stay on D:')
        scripts=Path(__file__).resolve().parent
        with tempfile.TemporaryDirectory(prefix='native-exit-host-test-',dir=lab) as folder:
            self.compile_run(cc,scripts,Path(folder))
            override=Path(folder)/'output-only.h'
            override.write_text('#define WEX_EXIT_OUTPUT L"Z:\\\\home\\\\debian\\\\wex-g4\\\\chromium-native-module-r2-20261024"\n')
            self.compile_run(cc,scripts,Path(folder),override)

    def compile_run(self,cc,scripts,folder,override=None):
            output=Path(folder)/'contract-test'
            built=subprocess.run([cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',
                *(['-include',str(override)] if override is not None else []),
                str(scripts/'test_g4_native_exit_contract.c'),'-o',str(output)],
                capture_output=True,text=True,timeout=20)
            self.assertEqual(built.returncode,0,built.stderr)
            self.assertEqual(built.stderr,'')
            result=subprocess.run([str(output)],capture_output=True,text=True,timeout=5)
            self.assertEqual(result.returncode,0,result.stderr)
            self.assertIn('PASS shared native schema/quoting/decision sequence',result.stdout)


if __name__=='__main__':unittest.main()
