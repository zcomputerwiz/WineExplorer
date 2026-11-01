# SPDX-License-Identifier: GPL-2.0-or-later
import tempfile
from pathlib import Path
import unittest
from unittest import mock
import json
import subprocess

from g4_chromium_wait_snapshot import observe,observe_root,root_snapshot


class SnapshotTests(unittest.TestCase):
    def test_root_snapshot_rejects_unprivileged_or_shared_namespace(self):
        with mock.patch('sys.argv',['observer','--root-r13']),mock.patch('os.geteuid',return_value=1000):
            with self.assertRaisesRegex(ValueError,'Fixed root'):root_snapshot()
        with mock.patch('sys.argv',['observer','--root-r13']),mock.patch('os.geteuid',return_value=0),mock.patch('os.readlink',return_value='shared'):
            with self.assertRaisesRegex(ValueError,'shared namespaces'):root_snapshot()

    def test_root_invocation_is_fixed_and_propagates_failures(self):
        lab=Path('/home/debian/wex-g4');name='chromium-token-wait-r13-20261101'
        output=lab/name;build=lab/(name+'-wine/build/wow64');prefix=lab/(name+'-prefix')
        env={'WINEPREFIX':str(prefix),'WINESERVER':str(build/'server/wineserver')}
        run=mock.Mock()
        with self.assertRaisesRegex(ValueError,'fixed r13'):
            observe_root(output,env,build,Path('wrong'),run=run)
        run.assert_not_called()
        record={'candidate_server_sha256':'196f24db0bfd402bc5656dc6be09a9d5baf82f863eeedd9596f9f3f78160a800','processes':[]}
        run.return_value=mock.Mock(stdout=json.dumps(record))
        self.assertEqual(observe_root(output,env,build,prefix,run=run),record)
        args,kwargs=run.call_args
        self.assertEqual(args[0][:4],['sudo','-n','python3','-B'])
        self.assertEqual(args[0][-1],'--root-r13')
        self.assertEqual(kwargs['timeout'],10);self.assertTrue(kwargs['check'])
        run.side_effect=subprocess.TimeoutExpired('sudo',10)
        with self.assertRaises(subprocess.TimeoutExpired):observe_root(output,env,build,prefix,run=run)

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

    def test_oversized_identity_field_rejected_without_recording_contents(self):
        with tempfile.TemporaryDirectory() as folder:
            root,env,build,prefix,proc=self.fixture(folder)
            (proc/'1'/'environ').write_bytes(b'x'*65537)
            with self.assertRaisesRegex(ValueError,'field quota'):
                observe(root,env,build,prefix,proc_root=proc)
            self.assertFalse((root/'wait-snapshot.json').exists())

    def test_more_than_512_thread_entries_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root,env,build,prefix,proc=self.fixture(folder,threads=513)
            with self.assertRaisesRegex(ValueError,'thread quota'):
                observe(root,env,build,prefix,proc_root=proc)


if __name__=='__main__':
    unittest.main()
