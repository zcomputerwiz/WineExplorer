# SPDX-License-Identifier: GPL-2.0-or-later
import tempfile
from pathlib import Path
import unittest
from unittest import mock
import json
import subprocess

from g4_chromium_wait_snapshot import observe,observe_root,root_snapshot,browser_mappings,mapped_browser


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

    def test_root_invocation_r14_is_fixed_and_propagates_failures(self):
        lab=Path('/home/debian/wex-g4');name='chromium-token-wait-r14-20261101'
        output=lab/name;build=lab/(name+'-wine/build/wow64');prefix=lab/(name+'-prefix')
        env={'WINEPREFIX':str(prefix),'WINESERVER':str(build/'server/wineserver')}
        run=mock.Mock()
        with self.assertRaisesRegex(ValueError,'fixed r13'):
            observe_root(output,env,build,Path('wrong'),run=run)
        run.assert_not_called()
        record={'candidate_server_sha256':'196f24db0bfd402bc5656dc6be09a9d5baf82f863eeedd9596f9f3f78160a800','browser':{}}
        run.return_value=mock.Mock(stdout=json.dumps(record))
        self.assertEqual(observe_root(output,env,build,prefix,run=run),record)
        args,kwargs=run.call_args
        self.assertEqual(args[0][:4],['sudo','-n','python3','-B'])
        self.assertEqual(args[0][-1],'--root-r14')
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


class MappedBrowserTests(unittest.TestCase):
    IMAGE=r'Z:\home\debian\wex-g4\chromium-headful-r1-20261023-stage\vendor\extracted\chrome-win\chrome.exe'

    def trace(self,name,win_pid,win_tid,unix_pid,unix_tid):
        return 'trace:wex_process:%s win_pid=%s win_tid=%s unix_pid=%d unix_tid=%d'%(name,win_pid,win_tid,unix_pid,unix_tid)

    def fixture(self,folder,mappings=((1176,1200),(1176,1201)),alive=(1200,1201),wine_pid=252,
                journal=None,cmdline_args=None,status=None):
        root=Path(folder).resolve()
        name='chromium-token-wait-r14-20261101'
        output=root/name;output.mkdir(parents=True)
        proc=root/'proc';proc.mkdir()
        profile='--user-data-dir=Z:'+str(output/'profile').replace('/','\\')
        for unix_pid,tid in mappings:
            if tid not in alive:continue
            task=proc/str(tid);task.mkdir(parents=True)
            args=(self.IMAGE+'\0'+profile) if cmdline_args is None else cmdline_args
            (task/'cmdline').write_bytes(args.encode()+b'\0')
            (task/'status').write_text(status if status is not None else 'Name:\tchrome\nTgid:\t%d\nPid:\t%d\n'%(unix_pid,tid))
            (task/'comm').write_text('chrome')
            (task/'wchan').write_text('do_poll')
        lines=[self.trace('helper','0020','0001',999,1000)]
        lines.extend(self.trace('main','%04x'%wine_pid,'0001',unix_pid,tid) for unix_pid,tid in mappings)
        (output/'helper.stderr.log').write_bytes(('\n'.join(lines)+'\n').encode())
        (output/'native-exit.jsonl').write_text(journal if journal is not None else json.dumps({'event':'bound','child_pid':wine_pid})+'\n')
        return output,proc

    def test_mappings_bind_single_wine_pid_to_one_unix_process_and_ignore_helper(self):
        log=self.trace('helper','0020','0001',999,1000)+'\n'
        log+=self.trace('main','00fc','0001',1176,1200)+'\n'+self.trace('main','00fc','0002',1176,1201)+'\n'
        journal=json.dumps({'event':'started','child_pid':32})+'\n'+json.dumps({'event':'bound','child_pid':252})+'\n'
        self.assertEqual(browser_mappings(log,journal),(252,[(1176,1200),(1176,1201)]))

    def test_mappings_require_exactly_one_binding(self):
        log=self.trace('main','00fc','0001',1176,1200)+'\n'
        with self.assertRaisesRegex(ValueError,'Exactly one browser binding'):
            browser_mappings(log,json.dumps({'event':'started'})+'\n')
        with self.assertRaisesRegex(ValueError,'Exactly one browser binding'):
            browser_mappings(log,json.dumps({'event':'bound','child_pid':252})+'\n'+json.dumps({'event':'bound','child_pid':252})+'\n')

    def test_mappings_reject_invalid_bound_pid(self):
        log=self.trace('main','00fc','0001',1176,1200)+'\n'
        for bad in ('252',0,-1,True):
            with self.assertRaisesRegex(ValueError,'Invalid bound browser PID'):
                browser_mappings(log,json.dumps({'event':'bound','child_pid':bad})+'\n')
        with self.assertRaisesRegex(ValueError,'Invalid bound browser PID'):
            browser_mappings(log,json.dumps({'event':'bound'})+'\n')

    def test_mappings_reject_missing_or_ambiguous_unix_mapping(self):
        journal=json.dumps({'event':'bound','child_pid':252})+'\n'
        with self.assertRaisesRegex(ValueError,'Unambiguous bound browser Unix mapping'):
            browser_mappings(self.trace('helper','0020','0001',999,1000)+'\n',journal)
        ambiguous=self.trace('main','00fc','0001',1176,1200)+'\n'+self.trace('main','00fc','0002',2222,1300)+'\n'
        with self.assertRaisesRegex(ValueError,'Unambiguous bound browser Unix mapping'):
            browser_mappings(ambiguous,journal)

    def test_mappings_enforce_thread_quota_and_positive_ids(self):
        journal=json.dumps({'event':'bound','child_pid':252})+'\n'
        ok='\n'.join(self.trace('main','00fc','%04x'%(i+1),1176,2000+i) for i in range(512))+'\n'
        self.assertEqual(browser_mappings(ok,journal),(252,[(1176,tid) for tid in range(2000,2512)]))
        with self.assertRaisesRegex(ValueError,'Mapped browser thread quota'):
            browser_mappings(ok+self.trace('main','00fc','0200',1176,2512)+'\n',journal)
        with self.assertRaisesRegex(ValueError,'Invalid browser Unix mapping'):
            browser_mappings(self.trace('main','00fc','0001',0,1200)+'\n',journal)

    def test_mapped_browser_verifies_live_threads_with_dead_leader(self):
        with tempfile.TemporaryDirectory() as folder:
            output,proc=self.fixture(folder)
            record=mapped_browser(output,proc_root=proc)
            self.assertEqual(record['wine_pid'],252)
            self.assertEqual(record['unix_pid'],1176)
            self.assertFalse((proc/'1176').exists())
            self.assertEqual([t['tid'] for t in record['threads']],[1200,1201])
            for thread in record['threads']:
                self.assertTrue(thread['identity_verified'])
                self.assertEqual(thread['comm'],'chrome')
                self.assertEqual(thread['wchan'],'do_poll')
                self.assertEqual(thread['syscall'],{'error':'FileNotFoundError'})
            self.assertNotIn(1000,[t['tid'] for t in record['threads']])

    def test_mapped_browser_records_absent_mapped_tid(self):
        with tempfile.TemporaryDirectory() as folder:
            output,proc=self.fixture(folder,alive=(1200,))
            record=mapped_browser(output,proc_root=proc)
            self.assertEqual(record['threads'][0]['tid'],1200)
            self.assertTrue(record['threads'][0]['identity_verified'])
            self.assertEqual(record['threads'][1],{'tid':1201,'identity_error':{'error':'FileNotFoundError'}})

    def test_mapped_browser_requires_at_least_one_live_thread(self):
        with tempfile.TemporaryDirectory() as folder:
            output,proc=self.fixture(folder,alive=())
            with self.assertRaisesRegex(ValueError,'No live mapped browser thread identity verified'):
                mapped_browser(output,proc_root=proc)

    def test_mapped_browser_rejects_command_identity_mismatch(self):
        for case in ('wrong-image','wrong-profile','utility-type'):
            with tempfile.TemporaryDirectory() as folder:
                root=Path(folder).resolve()
                profile='--user-data-dir=Z:'+str(root/'chromium-token-wait-r14-20261101'/'profile').replace('/','\\')
                if case=='wrong-image':args=('/usr/bin/firefox',profile)
                elif case=='wrong-profile':args=(self.IMAGE,'--user-data-dir=Z:\\other\\profile')
                else:args=(self.IMAGE,profile,'--type=utility')
                output,proc=self.fixture(folder,cmdline_args='\0'.join(args))
                with self.assertRaisesRegex(ValueError,'command identity mismatch'):
                    mapped_browser(output,proc_root=proc)

    def test_mapped_browser_rejects_wrong_thread_group(self):
        for status in ('Name:\tchrome\nTgid:\t9999\nPid:\t1200\n','Name:\tchrome\nPid:\t1200\n'):
            with tempfile.TemporaryDirectory() as folder:
                output,proc=self.fixture(folder,status=status)
                with self.assertRaisesRegex(ValueError,'thread group mismatch'):
                    mapped_browser(output,proc_root=proc)

    def test_mapped_browser_deadline_uses_injected_now(self):
        with tempfile.TemporaryDirectory() as folder:
            output,proc=self.fixture(folder)
            clock=iter([0.0,100.0])
            def now():
                try:return next(clock)
                except StopIteration:return 100.0
            with self.assertRaisesRegex(TimeoutError,'Mapped browser snapshot deadline'):
                mapped_browser(output,proc_root=proc,now=now)

    def test_mapped_browser_enforces_log_and_field_quotas(self):
        with tempfile.TemporaryDirectory() as folder:
            output,proc=self.fixture(folder)
            (output/'helper.stderr.log').write_bytes(b'x'*(1024**2+1))
            with self.assertRaisesRegex(ValueError,'Mapped browser log quota'):
                mapped_browser(output,proc_root=proc)
        with tempfile.TemporaryDirectory() as folder:
            output,proc=self.fixture(folder,journal='x'*65537)
            with self.assertRaisesRegex(ValueError,'Mapped browser field quota'):
                mapped_browser(output,proc_root=proc)


if __name__=='__main__':
    unittest.main()
