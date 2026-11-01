# SPDX-License-Identifier: GPL-2.0-or-later
import errno
import json
import os
from pathlib import Path
import select
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import g4_prefix_ownership as ownership

LAB=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')
PREFIX='/owned/exact-prefix'
NONCE='a'*32
SECRET=b'NEVER_SERIALIZE_THIS_ENV_OR_COMM'


def stat_bytes(pid,flags=0,start=123,comm=b'original child'):
    fields=[b'S']+[b'0']*49
    fields[6]=str(flags).encode();fields[19]=str(start).encode()
    return str(pid).encode()+b' ('+comm+b') '+b' '.join(fields)+b'\n'


@unittest.skipUnless(os.name=='posix' and LAB.is_dir(),'Linux/D: private storage required')
class PrefixOwnershipTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='prefix-fixture-',dir=LAB)
        self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)

    def task(self,pid,env=b'',flags=0,start=123,comm=b'original child'):
        path=self.root/str(pid);path.mkdir()
        (path/'stat').write_bytes(stat_bytes(pid,flags,start,comm))
        (path/'environ').write_bytes(env)
        return path

    def scan(self,**kwargs):
        result=ownership.scan(PREFIX,NONCE,proc_root=str(self.root),**kwargs)
        self.assertNotIn(SECRET.decode(),json.dumps(result))
        if result['status']=='unknown':
            self.assertIsNone(result['owners']);self.assertIsNone(result['busy'])
        return result

    def test_kernel_pid2_flag_excludes_without_environment_read(self):
        self.task(2,flags=ownership.PF_KTHREAD,start=0)
        real=ownership._read_at;reads=[]
        def read(fd,name,*args):
            reads.append(name)
            if name=='environ':raise ProcessLookupError(errno.ESRCH,'must never read kernel environ')
            return real(fd,name,*args)
        with mock.patch.object(ownership,'_read_at',side_effect=read):result=self.scan()
        self.assertEqual(reads,['stat','stat']);self.assertEqual(result['status'],'complete')
        self.assertEqual(result['owners'],[]);self.assertEqual(result['counts']['kernel_threads'],1)
        self.assertEqual(result['observations'][0]['flags'],0x00200000)
        self.assertEqual(result['observations'][0]['start_ticks'],0)

    def test_non_kernel_pid2_is_scanned_and_can_own_prefix(self):
        self.task(2,b'WINEPREFIX='+PREFIX.encode()+b'\0SECRET='+SECRET+b'\0')
        result=self.scan();self.assertEqual(result['status'],'complete')
        self.assertEqual(result['owners'],[{'pid':2,'start_ticks':123}]);self.assertTrue(result['busy'])

    def test_exact_nul_matching_near_values_and_successful_empty(self):
        environments=[b'WINEPREFIX='+PREFIX.encode()+b'\0',
            b'WINEPREFIX='+PREFIX.encode()+b'/near\0',b'XWINEPREFIX='+PREFIX.encode()+b'\0',
            b'OTHER=WINEPREFIX='+PREFIX.encode()+b'\0',b'',b'WINEPREFIX=/other\0']
        for pid,data in enumerate(environments,10):self.task(pid,data)
        result=self.scan();self.assertEqual(result['status'],'complete')
        self.assertEqual(result['owners'],[{'pid':10,'start_ticks':123}])
        self.assertEqual(result['counts']['empty_environ'],1)

    def test_stable_user_esrch_permissions_and_eio_unknown_no_environment_leak(self):
        self.task(42,b'SECRET='+SECRET+b'\0')
        real=ownership._read_at
        for number in (errno.ESRCH,errno.EPERM,errno.EACCES,errno.EIO):
            def read(fd,name,*args):
                if name=='environ':raise OSError(number,SECRET.decode())
                return real(fd,name,*args)
            with mock.patch.object(ownership,'_read_at',side_effect=read):result=self.scan()
            self.assertEqual(result['status'],'unknown');self.assertEqual(result['error']['errno'],number)
            self.assertTrue(result['error']['pid_path_present'])
            self.assertEqual(result['error']['identity_before'],result['error']['identity_after'])

    def test_missing_stat_and_unusual_comm_and_bad_stat_values(self):
        path=self.task(42,comm=SECRET+b' weird (nested) ) \n name')
        result=self.scan();self.assertEqual(result['status'],'complete')
        for raw in (b'',stat_bytes(42)[:-1],stat_bytes(41),stat_bytes(42,flags=-1),
                    stat_bytes(42,flags=2**32),stat_bytes(42,start=-1),stat_bytes(42,start=2**64),b'42 no-parens\n',
                    b' '.join(stat_bytes(42).split()[:22])+b'\n'):
            (path/'stat').write_bytes(raw)
            self.assertEqual(self.scan()['status'],'unknown')
        (path/'stat').unlink()
        result=self.scan();self.assertEqual(result['error']['errno'],errno.ENOENT)
        self.assertTrue(result['error']['pid_path_present'])

    def test_stat_parser_exact_types_and_parentheses(self):
        self.assertEqual(ownership.parse_stat(stat_bytes(9,comm=b'a ) (b) c\n'),9),dict(pid=9,start_ticks=123,flags=0))
        for expected in (True,'9',10):
            with self.assertRaises(ownership.ScanError):ownership.parse_stat(stat_bytes(9),expected)

    def test_pid_reuse_flag_change_and_directory_replacement_unknown(self):
        path=self.task(42,b'WINEPREFIX='+PREFIX.encode()+b'\0')
        real=ownership._read_at
        for change in ('start','flag','directory'):
            def read(fd,name,*args):
                if name=='environ':
                    if change=='directory':
                        path.rename(self.root/'old-task');path.mkdir()
                        (path/'stat').write_bytes(stat_bytes(42));(path/'environ').write_bytes(b'')
                    else:(path/'stat').write_bytes(stat_bytes(42,start=124 if change=='start' else 123,flags=ownership.PF_KTHREAD if change=='flag' else 0))
                return real(fd,name,*args)
            (path/'stat').write_bytes(stat_bytes(42))
            with mock.patch.object(ownership,'_read_at',side_effect=read):result=self.scan()
            self.assertEqual(result['status'],'unknown')
            self.assertIn(result['error']['code'],('task_identity_changed','task_directory_replaced'))
            if change=='directory':shutil.rmtree(self.root/'old-task')

    def test_disappearance_and_partial_known_owner_are_unknown_not_empty(self):
        self.task(10,b'WINEPREFIX='+PREFIX.encode()+b'\0')
        path=self.task(42,b'')
        real=ownership._read_at
        def read(fd,name,*args):
            if name=='environ' and os.readlink('/proc/self/fd/'+str(fd)).endswith('/42'):
                path.rename(self.root/'gone')
                raise ProcessLookupError(errno.ESRCH,SECRET.decode())
            return real(fd,name,*args)
        with mock.patch.object(ownership,'_read_at',side_effect=read):result=self.scan()
        self.assertEqual(result['status'],'unknown');self.assertFalse(result['error']['pid_path_present'])
        self.assertEqual(result['observations'][0]['classification'],'matching_owner')

    def test_truncated_environment_and_symlink_never_treated_empty(self):
        path=self.task(42,b'WINEPREFIX='+PREFIX.encode())
        self.assertEqual(self.scan()['error']['code'],'environment_truncated')
        (path/'environ').unlink();(self.root/'secret').write_bytes(SECRET)
        (path/'environ').symlink_to(self.root/'secret')
        self.assertEqual(self.scan()['status'],'unknown')

    def test_process_environment_aggregate_detail_and_deadline_quotas(self):
        self.task(10,b'A=12345\0');self.task(11,b'A=67890\0')
        for options in ({'max_processes':1},{'max_env_bytes':4},{'max_total_env_bytes':8}):
            result=self.scan(**options);self.assertEqual(result['status'],'unknown')
        ticks=iter(i*.05 for i in range(1000))
        result=self.scan(maximum_seconds=.1,now=lambda:next(ticks))
        self.assertEqual(result['status'],'unknown');self.assertIn('deadline',result['error']['code'])
        for path in self.root.iterdir():(path/'stat').write_bytes(stat_bytes(int(path.name),ownership.PF_KTHREAD))
        self.assertEqual(self.scan(max_details=1)['error']['code'],'detail_quota')

    def test_output_quota_and_oversized_stat(self):
        for pid in range(1,100):self.task(pid,flags=ownership.PF_KTHREAD)
        result=self.scan(max_output_bytes=8192)
        self.assertEqual(result['status'],'unknown');self.assertTrue(result['output_quota_exceeded'])
        self.assertLessEqual(len(json.dumps(result,separators=(',',':'),ensure_ascii=False).encode())+1,8192)
        (self.root/'1/stat').write_bytes(stat_bytes(1,comm=SECRET+b'x'*8192))
        self.assertEqual(self.scan()['error']['code'],'read_quota')

    def test_invalid_prefix_nonce_and_nonfinite_boolean_limits(self):
        for prefix,nonce in (('relative',NONCE),('/a/../b',NONCE),('/a\0b',NONCE),('//a',NONCE),(PREFIX,'bad'),(PREFIX,True)):
            result=ownership.scan(prefix,nonce,proc_root=str(self.root))
            self.assertEqual(result['status'],'unknown');self.assertIsNone(result['owners'])
        for options in ({'maximum_seconds':9},{'maximum_seconds':float('nan')},{'maximum_seconds':True},
                        {'max_processes':True},{'max_env_bytes':0},{'max_output_bytes':'bad'}):
            self.assertEqual(self.scan(**options)['status'],'unknown')

    def test_maximum_escaped_prefix_and_error_fallback_respects_wire_cap(self):
        for prefix in ('/'+'\\'*4095,'/'+'"'*4095,'/'+'é'*2047,'/'+'\\'*4093+'é'):
            for fault in (False,True):
                root=str(self.root/'missing') if fault else str(self.root)
                result=ownership.scan(prefix,NONCE,proc_root=root,max_output_bytes=8192)
                wire=json.dumps(result,separators=(',',':'),allow_nan=False,ensure_ascii=False).encode()+b'\n'
                self.assertLessEqual(len(wire),8192)
                self.assertEqual(bool(result.get('output_quota_exceeded')),any(v in prefix for v in ('\\','"')))
                if result.get('output_quota_exceeded'):
                    self.assertEqual(result['status'],'unknown');self.assertIsNone(result['owners']);self.assertIsNone(result['busy'])
                    self.assertIsNone(result['requested_prefix']);self.assertTrue(result['requested_prefix_omitted'])
                    self.assertEqual(result['request_omission_reason'],'output_quota')
                    self.assertEqual(result['nonce'],NONCE)
                else:
                    self.assertEqual(result['requested_prefix'],prefix)
                    self.assertEqual(result['status'],'unknown' if fault else 'complete')
                if fault:self.assertEqual(result['error']['errno'],errno.ENOENT)

    def test_real_probe_main_serialization_maxprefix_cap_and_nonzero_unknown(self):
        # Execute the SAME generated source/main in an original child. Only its
        # entrypoint is deferred to inject the controlled root and8KiB test cap;
        # scanner and output serialization are not copied or replaced.
        source=ownership.PROBE.rsplit('\nraise SystemExit',1)[0]
        for prefix in ('/'+'\\'*4095,'/'+'"'*4095,'/'+'é'*2047,'/'+'\\'*4093+'é'):
            for fault in (False,True):
                root=str(self.root/'missing') if fault else str(self.root)
                code=source+'\n_original_scan=scan\ndef scan(prefix,nonce):\n return _original_scan(prefix,nonce,proc_root='+repr(root)+',max_output_bytes=8192)\nraise SystemExit(probe_main(sys.argv))\n'
                done=subprocess.run([sys.executable,'-B','-c',code,prefix,NONCE],capture_output=True,timeout=10,cwd=self.root)
                self.assertEqual(done.stderr,b'');self.assertLessEqual(len(done.stdout),8192)
                result=json.loads(done.stdout)
                self.assertEqual(bool(result.get('output_quota_exceeded')),any(v in prefix for v in ('\\','"')))
                self.assertEqual(done.returncode,1 if result['status']=='unknown' else 0)
                self.assertNotIn(SECRET,done.stdout)
                if result.get('requested_prefix_omitted'):
                    self.assertEqual(result['status'],'unknown');self.assertIsNone(result['owners']);self.assertIsNone(result['busy'])
                else:self.assertEqual(result['requested_prefix'],prefix)

    def test_actual_selfcontained_source_in_subprocess_positive_and_fault(self):
        self.task(2,flags=ownership.PF_KTHREAD)
        self.task(42,b'WINEPREFIX='+PREFIX.encode()+b'\0SECRET='+SECRET+b'\0')
        # Map ONLY the proc-root open to a controlled fixture. All FD reads,
        # parsing, classification and remote main are the actual PROBE source.
        setup='import os,errno\n_original_open=os.open\ndef _open(path,*a,**kw):\n'
        setup+=' if path=="/proc":path='+repr(str(self.root))+'\n'
        for fault in (False,True):
            code=setup+(' if path=="environ":raise OSError(errno.ESRCH,"'+SECRET.decode()+'")\n' if fault else '')
            code+=' return _original_open(path,*a,**kw)\nos.open=_open\n'+ownership.PROBE
            done=subprocess.run([sys.executable,'-B','-c',code,PREFIX,NONCE],capture_output=True,timeout=10,cwd=self.root)
            result=json.loads(done.stdout)
            self.assertEqual(done.stderr,b'');self.assertNotIn(SECRET,done.stdout)
            self.assertEqual(done.returncode,1 if fault else 0)
            self.assertEqual(result['status'],'unknown' if fault else 'complete')
            self.assertEqual(result['owners'],None if fault else [{'pid':42,'start_ticks':123}])
        done=subprocess.run([sys.executable,'-B','-c',ownership.PROBE],capture_output=True,timeout=10,cwd=self.root)
        self.assertEqual(done.returncode,1);self.assertIsNone(json.loads(done.stdout)['owners'])

    def test_real_linux_owned_children_exact_match_release_reap_and_absence(self):
        evidence=Path(tempfile.mkdtemp(prefix='prefix-ownership-control-',dir=LAB))
        prefix=str(evidence/'wine-prefix');children=[];result=None;exits=[]
        script='import os,sys,select;print(os.getpid(),flush=True);r,_,_=select.select([sys.stdin],[],[],30);sys.exit(0 if r and sys.stdin.buffer.read(1)==b"x" else 2)'
        real_scandir=os.scandir
        try:
            for value in (prefix,prefix+'-near'):
                child=subprocess.Popen([sys.executable,'-B','-c',script],stdin=subprocess.PIPE,stdout=subprocess.PIPE,
                    stderr=subprocess.PIPE,env={**os.environ,'WINEPREFIX':value,'PRIVATE_TEST_VALUE':SECRET.decode()},
                    start_new_session=True,cwd=evidence)
                children.append(child)
                self.assertTrue(select.select([child.stdout],[],[],2)[0])
                self.assertEqual(int(child.stdout.readline()),child.pid)
            owned={str(child.pid) for child in children}
            class OwnedEntries:
                def __init__(self,fd):self.entries=real_scandir(fd)
                def __enter__(self):return (entry for entry in self.entries if entry.name in owned)
                def __exit__(self,*args):self.entries.close()
            # Controlled enumeration only; ALL task FDs/stat/environ reads are
            # actual native /proc for these owned children, not fixture bytes.
            with mock.patch.object(ownership.os,'scandir',side_effect=OwnedEntries):
                result=ownership.scan(prefix,NONCE)
            self.assertEqual(result['status'],'complete',result.get('error'))
            self.assertEqual([row['pid'] for row in result['owners']],[children[0].pid])
            self.assertNotIn(SECRET.decode(),json.dumps(result))
        finally:
            for child in children:
                row=dict(pid=child.pid)
                try:
                    if child.poll() is None:
                        try:child.stdin.write(b'x');child.stdin.flush()
                        except BrokenPipeError:pass
                    try:child.wait(timeout=2)
                    except subprocess.TimeoutExpired:child.kill();child.wait(timeout=2)
                except Exception as error:row['cleanup_error']=type(error).__name__
                finally:
                    row.update(exit=child.poll(),absent=not Path('/proc',str(child.pid)).exists());exits.append(row)
                    for stream in (child.stdin,child.stdout,child.stderr):
                        try:stream.close()
                        except Exception as error:row.setdefault('cleanup_error',type(error).__name__)
            (evidence/'control.json').write_text(json.dumps(dict(result=result,children=exits,enumeration='two owned real tasks only'),indent=2)+'\n')
            print('Retained original host control:',evidence)
        self.assertTrue(all(row['exit']==0 and row['absent'] and 'cleanup_error' not in row for row in exits))


if __name__=='__main__':unittest.main()
