# SPDX-License-Identifier: GPL-2.0-or-later
import tempfile
from pathlib import Path
import unittest

from g4_chromium_wait_snapshot import observe


class SnapshotTests(unittest.TestCase):
    def test_private_prefix_and_candidate_server_required(self):
        with self.assertRaisesRegex(ValueError,'exact candidate'):
            observe(Path('.'),{},Path('build'),Path('prefix'))

    def test_snapshot_excludes_unrelated_processes_and_reports_missing_fields(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);build=root/'build';prefix=root/'chromium-token-wait-r10-20261031-prefix'
            (build/'server').mkdir(parents=True);(build/'server/wineserver').write_bytes(b'server')
            proc=root/'proc';proc.mkdir()
            for pid,identity in (('10',str(prefix)),('11','unrelated')):
                task=proc/pid/'task'/pid;task.mkdir(parents=True)
                (proc/pid/'environ').write_bytes(('WINEPREFIX='+identity+'\0SECRET=not-captured').encode())
                (proc/pid/'cmdline').write_bytes(b'chrome.exe\0')
                (task/'comm').write_text('Chrome_IOThread')
                (task/'wchan').write_text('do_poll')
            env={'WINEPREFIX':str(prefix),'WINESERVER':str(build/'server/wineserver')}
            result=observe(root,env,build,prefix,proc_root=proc)
            self.assertEqual([p['pid'] for p in result['processes']],[10])
            self.assertEqual(result['processes'][0]['threads'][0]['syscall'],{'error':'FileNotFoundError'})
            self.assertNotIn('SECRET',(root/'wait-snapshot.json').read_text())
            with self.assertRaisesRegex(ValueError,'budget'):
                observe(root,env,build,prefix,maximum_seconds=9,proc_root=proc)

    def fixture(self,folder,processes=1,threads=0):
        root=Path(folder);build=root/'build';prefix=root/'chromium-token-wait-r10-20261031-prefix'
        (build/'server').mkdir(parents=True);(build/'server/wineserver').write_bytes(b'server')
        proc=root/'proc';proc.mkdir()
        for pid in range(1,processes+1):
            p=proc/str(pid);p.mkdir()
            (p/'environ').write_bytes(('WINEPREFIX='+str(prefix)+'\0').encode())
            (p/'cmdline').write_bytes(b'chrome.exe\0')
            for tid in range(1,threads+1):
                (p/'task'/str(tid)).mkdir(parents=True)
        env={'WINEPREFIX':str(prefix),'WINESERVER':str(build/'server/wineserver')}
        return root,env,build,prefix,proc

    def test_rejects_wrong_prefix_name_and_excess_seconds(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);build=root/'build'
            bad=root/'other-prefix'
            env={'WINEPREFIX':str(bad),'WINESERVER':str(build/'server/wineserver')}
            with self.assertRaisesRegex(ValueError,'dedicated fixed diagnostic prefix'):
                observe(root,env,build,bad)
            prefix=root/'chromium-token-wait-r10-20261031-prefix'
            env={'WINEPREFIX':str(prefix),'WINESERVER':str(build/'server/wineserver')}
            with self.assertRaisesRegex(ValueError,'budget'):
                observe(root,env,build,prefix,maximum_seconds=9)

    def test_expired_injected_clock_raises_timeout(self):
        with tempfile.TemporaryDirectory() as folder:
            root,env,build,prefix,proc=self.fixture(folder)
            clock=iter([0.0,100.0])
            def now():
                try:return next(clock)
                except StopIteration:return 100.0
            with self.assertRaises(TimeoutError):
                observe(root,env,build,prefix,proc_root=proc,now=now)

    def test_oversized_field_raises_value_error(self):
        with tempfile.TemporaryDirectory() as folder:
            root,env,build,prefix,proc=self.fixture(folder,threads=1)
            (proc/'1'/'task'/'1'/'comm').write_bytes(b'x'*65537)
            with self.assertRaisesRegex(ValueError,'field quota'):
                observe(root,env,build,prefix,proc_root=proc)

    def test_more_than_64_matching_processes_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root,env,build,prefix,proc=self.fixture(folder,processes=65)
            with self.assertRaisesRegex(ValueError,'process quota'):
                observe(root,env,build,prefix,proc_root=proc)

    def test_more_than_512_thread_entries_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root,env,build,prefix,proc=self.fixture(folder,threads=513)
            with self.assertRaisesRegex(ValueError,'thread quota'):
                observe(root,env,build,prefix,proc_root=proc)


if __name__=='__main__':
    unittest.main()
