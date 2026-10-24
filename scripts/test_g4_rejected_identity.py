# SPDX-License-Identifier: GPL-2.0-or-later
"""Actual FD reader controls; no Wine/Windows/guest execution."""
import base64
import errno
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

import g4_edge_owned_threads as owned

LAB=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')
SECRET=b'UNRELATED_ENV_SECRET_MUST_NOT_BE_RETAINED'
ARGV=['/build/wine','/owned path/native-exit.exe','--calibration','--nonce','a'*32]


def write_stat(path,pid,start):
    path.write_bytes((str(pid)+' (owned fixture) '+' '.join(['S']+['0']*18+[str(start)])+'\n').encode())


@unittest.skipUnless(LAB.is_dir(),'private D: storage unavailable')
class ReaderTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(prefix='rejected-identity-',dir=LAB);self.addCleanup(self.temp.cleanup)
        self.proc=Path(self.temp.name);self.pid=1234;self.start=99;self.task=self.proc/str(self.pid);self.task.mkdir()
        write_stat(self.task/'stat',self.pid,self.start)
        (self.task/'environ').write_bytes(b'SECRET='+SECRET+b'\0WINEPREFIX=/owned/prefix\0')
        self.good=b'\0'.join(v.encode() for v in ARGV)+b'\0';(self.task/'cmdline').write_bytes(self.good)

    def observe(self,**kwargs):return owned.linux_identity(self.proc,self.pid,self.start,Path('/owned/prefix'),ARGV,**kwargs)

    def rejected(self,**kwargs):
        with self.assertRaises(owned.LinuxIdentityError) as caught:self.observe(**kwargs)
        value=caught.exception.observation
        self.assertNotIn(SECRET.decode(),json.dumps(value));self.assertIsInstance(caught.exception,ValueError)
        self.assertNotIn('base64',value['fields'].get('environ',{}))
        return caught.exception,value

    def test_original_forms_positive_shape_then_exact_rejected_bytes(self):
        for raw,style in ((self.good,'argv_array'),(b'\0'.join(v.encode() for v in ARGV[1:])+b'\0','argv_array'),
                ((' '.join(ARGV[1:])+' '*50+'\0\0').encode(),'wine_flat_padded')):
            (self.task/'cmdline').write_bytes(raw);result=self.observe()
            self.assertEqual(set(result),{'linux_pid','start_ticks','prefix','argv_style','observed_cmdline'})
            self.assertEqual(result['argv_style'],style)
        for raw in (b'"/owned path/native-exit.exe" --calibration\0',b'bad\0with spaces\0',b'\xff\0',b'',b'bad\0\0   \0'):
            (self.task/'cmdline').write_bytes(raw);error,value=self.rejected()
            self.assertEqual(base64.b64decode(value['fields']['cmdline']['base64']),raw)
            self.assertEqual(value['before']['start_ticks'],value['after']['start_ticks'])
            self.assertTrue(value['prefix_match']['matched']);self.assertTrue(value['fields']['cmdline']['complete'])
            self.assertIn(str(error),value['rejection_reason'])

    def test_actual_read_caps_partial_bytes_and_no_environment_capture(self):
        real_open,real_read,real_close=os.open,os.read,os.close
        for name,cap in (('stat',4096),('cmdline',16384),('environ',65536)):
            with self.subTest(name=name):
                write_stat(self.task/'stat',self.pid,self.start);(self.task/'cmdline').write_bytes(self.good)
                (self.task/'environ').write_bytes(b'WINEPREFIX=/owned/prefix\0'+SECRET)
                (self.task/name).write_bytes(b'x'*(cap+1000));handles={};reads=[];closed=[]
                def opening(path,flags,*args,**kwargs):
                    fd=real_open(path,flags,*args,**kwargs);handles[fd]=str(path);reads.append([str(path),0,fd]);return fd
                def reading(fd,size):
                    data=real_read(fd,size)
                    next(r for r in reversed(reads) if r[2]==fd)[1]+=len(data)
                    return data
                def closing(fd):real_close(fd);closed.append(fd)
                with mock.patch.object(os,'open',side_effect=opening),mock.patch.object(os,'read',side_effect=reading),mock.patch.object(os,'close',side_effect=closing):
                    error,value=self.rejected()
                self.assertIn('Oversized',str(error))
                for label,total,fd in reads:
                    if label in ('stat','cmdline','environ'):self.assertLessEqual(total,{'stat':4096,'cmdline':16384,'environ':65536}[label])
                    self.assertIn(fd,closed)
                label='stat_before' if name=='stat' else name
                self.assertEqual(value['fields'][label]['bytes_read'],cap)
                self.assertTrue(value['fields'][label]['limit_reached'])
                if name!='environ':self.assertEqual(base64.b64decode(value['fields'][label]['base64']),b'x'*cap)

    def test_start_drift_before_never_reads_env_or_cmdline_and_after_fails(self):
        write_stat(self.task/'stat',self.pid,100)
        _,value=self.rejected();self.assertEqual(value['read_stages'],['stat_before'])
        write_stat(self.task/'stat',self.pid,self.start);real_open=os.open;stat_count=0
        def opening(path,*args,**kwargs):
            nonlocal stat_count
            if path=='stat':
                stat_count+=1
                if stat_count==2:write_stat(self.task/'stat',self.pid,100)
            return real_open(path,*args,**kwargs)
        with mock.patch.object(os,'open',side_effect=opening):error,value=self.rejected()
        self.assertIn('PID reused after',str(error));self.assertEqual(value['before']['start_ticks'],99)
        self.assertEqual(value['after']['start_ticks'],100)

    def test_fd_bound_directory_replacement_never_reads_foreign_task(self):
        real_open,real_stat=os.open,os.stat;foreign=self.proc/'foreign';foreign.mkdir();switched=False
        write_stat(foreign/'stat',self.pid,200)
        (foreign/'cmdline').write_bytes(b'FOREIGN_TASK_MUST_NOT_BE_READ\0')
        def opening(path,*args,**kwargs):
            nonlocal switched
            fd=real_open(path,*args,**kwargs)
            if path=='cmdline' and not switched:
                switched=True
            return fd
        def replaced(path,*args,**kwargs):
            # DrvFS disallows renaming an open directory. Inject only the
            # changed path lookup; actual field reads still use the task FD.
            if switched and path==str(self.pid) and 'dir_fd' in kwargs:return real_stat(foreign)
            return real_stat(path,*args,**kwargs)
        with mock.patch.object(os,'open',side_effect=opening),mock.patch.object(os,'stat',side_effect=replaced):error,value=self.rejected()
        self.assertIn('directory changed',str(error));self.assertEqual(value['after']['start_ticks'],99)
        self.assertEqual(base64.b64decode(value['fields']['cmdline']['base64']),self.good)
        self.assertNotIn('FOREIGN_TASK',json.dumps(value))

    def test_prefix_mismatch_io_faults_partial_read_and_fd_closure(self):
        (self.task/'environ').write_bytes(b'WINEPREFIX=/foreign\0SECRET='+SECRET+b'\0')
        error,value=self.rejected();self.assertEqual(str(error),'Linux prefix mismatch');self.assertFalse(value['prefix_match']['matched'])
        self.assertNotIn('cmdline',value['fields'])
        for code in (errno.EACCES,errno.ESRCH,errno.EIO):
            real_open,real_close=os.open,os.close;opened=[];closed=[]
            def opening(path,*args,**kwargs):
                if path=='environ':raise OSError(code,'read failure')
                fd=real_open(path,*args,**kwargs);opened.append(fd);return fd
            def closing(fd):real_close(fd);closed.append(fd)
            with mock.patch.object(os,'open',side_effect=opening),mock.patch.object(os,'close',side_effect=closing):_,value=self.rejected()
            self.assertEqual(value['prefix_match']['error']['errno'],code);self.assertEqual(sorted(opened),sorted(closed))
        (self.task/'environ').write_bytes(b'WINEPREFIX=/owned/prefix\0');real_read=os.read;real_open=os.open;cmdfd=None;count=0
        def opening(path,*args,**kwargs):
            nonlocal cmdfd
            fd=real_open(path,*args,**kwargs)
            if path=='cmdline':cmdfd=fd
            return fd
        def reading(fd,size):
            nonlocal count
            if fd==cmdfd:
                count+=1
                if count==2:raise OSError(errno.EIO,'partial read')
                return real_read(fd,3)
            return real_read(fd,size)
        with mock.patch.object(os,'open',side_effect=opening),mock.patch.object(os,'read',side_effect=reading):_,value=self.rejected()
        self.assertEqual(base64.b64decode(value['fields']['cmdline']['base64']),self.good[:3])

    def test_deadline_and_secondary_postcheck_close_failures_preserve_primary(self):
        real_open,real_read,real_close=os.open,os.read,os.close;cmdfd=None;clock=[0]
        def opening(path,*args,**kwargs):
            nonlocal cmdfd
            fd=real_open(path,*args,**kwargs)
            if path=='cmdline':cmdfd=fd
            return fd
        def reading(fd,size):
            value=real_read(fd,size)
            if fd==cmdfd:clock[0]=2
            return value
        with mock.patch.object(os,'open',side_effect=opening),mock.patch.object(os,'read',side_effect=reading):error,value=self.rejected(now=lambda:clock[0])
        self.assertIn('deadline',str(error));self.assertEqual(base64.b64decode(value['fields']['cmdline']['base64']),self.good)
        (self.task/'cmdline').write_bytes(b'bad\0');count=0
        def fail_after(path,*args,**kwargs):
            nonlocal count
            if path=='stat':
                count+=1
                if count==2:raise OSError(errno.EIO,'secondary stat failure')
            return real_open(path,*args,**kwargs)
        def closing(fd):real_close(fd);raise OSError(errno.EIO,'secondary close failure')
        with mock.patch.object(os,'open',side_effect=fail_after),mock.patch.object(os,'close',side_effect=closing):error,value=self.rejected()
        self.assertEqual(str(error),'Linux image/app URL/profile/argv mismatch')
        self.assertEqual(value['postcheck_error']['errno'],errno.EIO);self.assertTrue(value['cleanup_errors'])
        (self.task/'cmdline').write_bytes(self.good)
        with mock.patch.object(os,'close',side_effect=closing):error,value=self.rejected()
        self.assertIn('cleanup failed',str(error))

    def test_malformed_stat_and_owned_real_linux_child_reaped(self):
        (self.task/'stat').write_bytes(b'not a stat\xff\n');_,value=self.rejected()
        self.assertEqual(base64.b64decode(value['fields']['stat_before']['base64']),b'not a stat\xff\n')
        env=dict(os.environ,WINEPREFIX='/owned/prefix',UNRELATED_SECRET=SECRET.decode())
        argv=[sys.executable,'-c','import time; time.sleep(10)'];child=subprocess.Popen(argv,env=env,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        try:
            raw=(Path('/proc')/str(child.pid)/'stat').read_bytes();start=owned.stat_record(raw,child.pid)['start_ticks']
            result=owned.linux_identity(Path('/proc'),child.pid,start,Path('/owned/prefix'),argv)
            self.assertEqual(result['argv_style'],'argv_array')
            with self.assertRaises(owned.LinuxIdentityError) as caught:
                owned.linux_identity(Path('/proc'),child.pid,start,Path('/owned/prefix'),argv+['foreign'])
            self.assertEqual(base64.b64decode(caught.exception.observation['fields']['cmdline']['base64']),b'\0'.join(v.encode() for v in argv)+b'\0')
            self.assertNotIn(SECRET.decode(),json.dumps(caught.exception.observation))
        finally:
            child.terminate()
            try:child.wait(timeout=2)
            except subprocess.TimeoutExpired:child.kill();child.wait(timeout=2)
        self.assertIsNotNone(child.returncode);self.assertFalse((Path('/proc')/str(child.pid)).exists())

    def test_expired_nonfinite_deadline_and_clock_fail_before_field_access(self):
        for options in ({'deadline':0,'now':lambda:0},{'deadline':True},{'deadline':float('nan')},
                {'now':lambda:float('nan')},{'now':lambda:True}):
            with mock.patch.object(os,'open',side_effect=AssertionError('No field access')) as opening:
                _,value=self.rejected(**options)
            opening.assert_not_called();self.assertEqual(value['read_stages'],[])
            json.dumps(value,allow_nan=False)


if __name__=='__main__':unittest.main()
