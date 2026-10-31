# SPDX-License-Identifier: GPL-2.0-or-later
import tempfile
from pathlib import Path
import subprocess
import unittest
from unittest import mock

from g4_chromium_wait_snapshot import observe


class SnapshotTests(unittest.TestCase):
    def test_private_prefix_and_candidate_server_required(self):
        with self.assertRaisesRegex(ValueError,'exact candidate'):
            observe(Path('.'),{},Path('build'),Path('prefix'))

    def test_bounded_debugger_reaped_on_success_and_timeout(self):
        for timeout in (False,True):
            with tempfile.TemporaryDirectory() as folder:
                root=Path(folder);build=root/'build';prefix=root/'chromium-token-wait-r9-20261031-prefix'
                (build/'programs/winedbg/x86_64-windows').mkdir(parents=True)
                (build/'programs/winedbg/x86_64-windows/winedbg.exe').write_bytes(b'PE')
                (build/'server').mkdir();(build/'server/wineserver').write_bytes(b'server')
                env={'WINEPREFIX':str(prefix),'WINESERVER':str(build/'server/wineserver')}
                proc=mock.Mock()
                proc.wait.side_effect=[subprocess.TimeoutExpired('debugger',8),-9] if timeout else [0]
                with mock.patch('g4_chromium_wait_snapshot.subprocess.Popen',return_value=proc) as launch:
                    result=observe(root,env,build,prefix)
                self.assertEqual(result['timed_out'],timeout)
                self.assertTrue(result['reaped'])
                self.assertEqual(launch.call_args.kwargs['env']['WINEDEBUG'],'-all')
                self.assertEqual(launch.call_args.args[0][-2:],['--command','bt all'])
                if timeout:proc.kill.assert_called_once()
                else:proc.kill.assert_not_called()


if __name__=='__main__':
    unittest.main()
