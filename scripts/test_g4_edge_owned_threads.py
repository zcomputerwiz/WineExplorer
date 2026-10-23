# SPDX-License-Identifier: GPL-2.0-or-later
"""Synthetic native API records, fake proc files and bounded helper transports.

These are NOT execution of Windows/Wine thread APIs or a Win32/Linux mapping.
"""
import copy
import json
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import g4_edge_owned_threads as owned

NONCE='8933d94f574aea8473129850412e076f'
IMAGE=r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe'
LINUX='/home/debian/wex-g4/edge-install-r1-v2-20261022/prefix/drive_c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
PROFILE=r'Z:\home\debian\wex-g4\edge-headful-http-r1-20261023\profile'
APP='http://127.0.0.1:42967/'+NONCE+'/index.html#nonce='+NONCE
ARGV=['/owned/build/wine',LINUX,'--app='+APP,'--user-data-dir='+PROFILE,'--no-first-run']


def window():
    return dict(hwnd=655478,pid=408,creation_time=123456789,visible=True,top_level=True,fixed=65536,
                geometry_valid=True,title='127.0.0.1_/'+NONCE+'/index.html',image=IMAGE,
                **{'class':'Chrome_WidgetWin_1'},window=dict(x=10,y=10,width=1050,height=700),
                client=dict(x=14,y=40,width=1042,height=666))


def native():
    w=window();identity={k:w[k] for k in ('pid','creation_time','image','title','window','client')}
    identity['gui_tid']=412
    return dict(ok=True,hwnd=w['hwnd'],nonce=NONCE,before=copy.deepcopy(identity),after=copy.deepcopy(identity),
                description_available=True,threads=[dict(tid=412,owner_pid=408,queried=True,query_error=0,
                creation_time=1234,exit_code=259,name_error=0,name='CrBrowserMain')])


def write_stat(path,tid,start=9664,state='S'):
    path.write_text(str(tid)+' (name with ) parens) '+' '.join([state]+['0']*18+[str(start)])+'\n')


class OwnedThreadsTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.proc=Path(self.temp.name);self.pid=1040;self.prefix=Path('/owned/prefix')
        p=self.proc/str(self.pid);p.mkdir();write_stat(p/'stat',self.pid)
        (p/'cmdline').write_bytes((' '.join(ARGV[1:])+' '*200+'\0').encode())
        (p/'environ').write_bytes(b'HOME=/private\0WINEPREFIX=/owned/prefix\0')
        self.task=p/'task'/str(self.pid);self.task.mkdir(parents=True)
        write_stat(self.task/'stat',self.pid)
        for name,value in [('comm','CrBrowserMain\n'),('wchan','futex_wait_queue\n'),('syscall','202 0x123 0 0\n')]:
            (self.task/name).write_text(value)

    def linux(self,**kwargs):
        return owned.linux_record(self.pid,{'start_ticks':9664},self.prefix,ARGV,5,proc=self.proc,now=lambda:0,**kwargs)

    def test_unique_native_selection_and_archived_caption(self):
        self.assertEqual(owned.select_window([window()],NONCE,[IMAGE]),window())

    def test_zero_multiple_foreign_child_wrong_nonce_selection(self):
        for rows in ([],[window(),window()]):
            with self.assertRaises(ValueError): owned.select_window(rows,NONCE,[IMAGE])
        for key,value in [('title','127.0.0.1_/'+('b'*32)+'/index.html'),('image',r'C:\foreign\msedge.exe'),
                          ('image','msedge.exe'),('top_level',False),('visible',False),('class','DComp Swapchain'),
                          ('geometry_valid',False),('creation_time',0),('pid',True),('hwnd',2**64),
                          ('window',dict(x=0,y=0,width=True,height=10))]:
            w=window();w[key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):owned.select_window([w],NONCE,[IMAGE])

    def test_native_strict_parse_and_query_names_are_not_stacks(self):
        self.assertEqual(owned.native_record(json.dumps(native()),window(),NONCE)['threads'][0]['name'],'CrBrowserMain')
        r=native();r['description_available']=False;r['threads'][0]['name']='';r['threads'][0]['name_error']=0
        self.assertFalse(owned.native_record(json.dumps(r),window(),NONCE)['description_available'])
        r['threads'][0].update(queried=False,query_error=5,creation_time=0)
        self.assertEqual(owned.native_record(json.dumps(r),window(),NONCE)['threads'][0]['query_error'],5)

    def test_native_identity_creation_tid_owner_count_schema_negative(self):
        mutations=[lambda r:r.update(hwnd=True),lambda r:r.update(nonce='b'*32),
            lambda r:r['after'].update(creation_time=99),lambda r:r['before'].update(pid=660),
            lambda r:r['after'].update(image=r'C:\other\msedge.exe'),lambda r:r['after'].update(gui_tid=444),
            lambda r:r['after']['window'].update(x=True),lambda r:r['threads'][0].update(owner_pid=660),
            lambda r:r['threads'][0].update(tid=True),lambda r:r['threads'][0].update(creation_time=0),
            lambda r:r['threads'][0].update(queried=False,query_error=0),lambda r:r['threads'][0].update(name='x'*257),
            lambda r:r.update(threads=[]),lambda r:r.update(threads=r['threads']*129),
            lambda r:r['threads'].append(copy.deepcopy(r['threads'][0]))]
        for mutate in mutations:
            r=native();mutate(r)
            with self.subTest(mutate=mutate),self.assertRaises(ValueError):owned.native_record(json.dumps(r),window(),NONCE)
        for text in ('[]','{','{"ok":true,"ok":true}', 'NaN','x'*(512*1024+1)):
            with self.assertRaises(ValueError):owned.native_record(text,window(),NONCE)

    def test_native_nested_geometry_bool_zero_one_and_extra_keys_rejected(self):
        for rect in ('window','client'):
            for value in (0,1):
                w=window();w['window']['x']=value if rect=='window' else 0
                w['client']['x']=value if rect=='client' else value+4
                r=native()
                for stage in ('before','after'):
                    r[stage]['window']=copy.deepcopy(w['window']);r[stage]['client']=copy.deepcopy(w['client'])
                    r[stage][rect]['x']=bool(value)
                with self.subTest(rect=rect,value=value),self.assertRaises(ValueError):owned.native_record(json.dumps(r),w,NONCE)
        r=native();r['before']['client']['extra']=0
        with self.assertRaises(ValueError):owned.native_record(json.dumps(r),window(),NONCE)

    def test_linux_actual_unquoted_padded_image_app_profile_format(self):
        r=self.linux();self.assertEqual(r['before']['argv_style'],'wine_flat_padded')
        self.assertEqual(r['before'],r['after']);self.assertTrue(r['tasks'][0]['identity_verified'])
        self.assertEqual(r['tasks'][0]['wchan'],'futex_wait_queue')
        self.assertNotIn('win32_pid',r)

    def test_complete_archived_http_command_title_not_conventionally_quoted(self):
        # Same form and complete flag set recorded for actual Popen1040/start9664;
        # no assumption that spaces delimit the executable path or profile.
        suffix=['--no-default-browser-check','--enable-logging',
                r'--log-file=Z:\home\debian\wex-g4\edge-headful-http-r1-20261023\chromium.log',
                '--v=1','--vmodule=file_url_loader_factory=2,navigation_request=2,render_process_host_impl=2,child_process_launcher_helper=2,child_process_launcher_helper_win=2']
        expected=ARGV+suffix
        literal=('/home/debian/wex-g4/edge-install-r1-v2-20261022/prefix/drive_c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe '
                 '--app=http://127.0.0.1:42967/8933d94f574aea8473129850412e076f/index.html#nonce=8933d94f574aea8473129850412e076f '
                 r'--user-data-dir=Z:\home\debian\wex-g4\edge-headful-http-r1-20261023\profile '
                 '--no-first-run --no-default-browser-check --enable-logging '
                 r'--log-file=Z:\home\debian\wex-g4\edge-headful-http-r1-20261023\chromium.log '
                 '--v=1 --vmodule=file_url_loader_factory=2,navigation_request=2,render_process_host_impl=2,child_process_launcher_helper=2,child_process_launcher_helper_win=2')
        (self.proc/str(self.pid)/'cmdline').write_bytes((literal+' '*200+'\0').encode())
        result=owned.linux_record(self.pid,{'start_ticks':9664},self.prefix,expected,5,proc=self.proc,now=lambda:0)
        self.assertEqual(result['before']['argv_style'],'wine_flat_padded')

    def test_no_read_outside_exact_linux_pid_and_explicit_native_error_when_expired(self):
        original=owned.read;paths=[]
        def read(path,limit):
            paths.append(path);return original(path,limit)
        with patch.object(owned,'read',side_effect=read):self.linux()
        self.assertTrue(paths);self.assertTrue(all(p.is_relative_to(self.proc/str(self.pid)) for p in paths))
        def query(*args):raise AssertionError('No native call after expired budget')
        record=owned.collect(query,NONCE,[IMAGE],self.pid,{'start_ticks':9664},self.prefix,ARGV,
                             seconds=0,proc=self.proc,now=lambda:0)
        self.assertIn('deadline',record['native_error']);self.assertIn('deadline',record['linux_error'])

    def test_linux_array_format_and_wrong_image_profile_prefix_child_reuse(self):
        p=self.proc/str(self.pid)
        (p/'cmdline').write_bytes(b'\0'.join(v.encode() for v in ARGV[1:])+b'\0')
        self.assertEqual(self.linux()['before']['argv_style'],'argv_array')
        for args in ([*ARGV[1:],'--type=renderer'],[LINUX,'--app='+APP,'--user-data-dir=wrong'],
                     ['msedge.exe',*ARGV[2:]], [LINUX,ARGV[2].replace(NONCE,'b'*32),*ARGV[3:]]):
            (p/'cmdline').write_bytes((' '.join(args)+' '*30+'\0').encode())
            with self.assertRaises(ValueError):self.linux()
        (p/'cmdline').write_bytes((' '.join(ARGV[1:])+'\0').encode())
        (p/'environ').write_bytes(b'WINEPREFIX=/foreign/prefix\0')
        with self.assertRaises(ValueError):self.linux()
        (p/'environ').write_bytes(b'WINEPREFIX=/owned/prefix\0')
        write_stat(p/'stat',self.pid,9665)
        with self.assertRaisesRegex(ValueError,'PID reused'):self.linux()

    def test_linux_missing_sample_deadline_and_task_limit(self):
        with self.assertRaises(ValueError):owned.linux_record(self.pid,None,self.prefix,ARGV,5,proc=self.proc)
        with self.assertRaises(TimeoutError):owned.linux_record(self.pid,{'start_ticks':9664},self.prefix,ARGV,0,proc=self.proc,now=lambda:1)
        for tid in range(2000,2128):(self.task.parent/str(tid)).mkdir()
        with self.assertRaisesRegex(ValueError,'task limit'):self.linux()

    def test_linux_thread_disappearance_and_denied_queries_are_explicit(self):
        original=owned.read
        def read(path,limit):
            if path.name=='syscall':raise PermissionError('denied')
            return original(path,limit)
        with patch.object(owned,'read',side_effect=read):r=self.linux()
        self.assertIn('denied',r['tasks'][0]['syscall']['unavailable'])
        (self.task/'stat').unlink();r=self.linux()
        self.assertFalse(r['tasks'][0]['identity_verified']);self.assertIn('FileNotFoundError',r['tasks'][0]['error'])

    def test_linux_process_and_thread_revalidation_after_collection(self):
        original=owned.read
        def changed(path,limit):
            if path.name=='syscall':write_stat(self.proc/str(self.pid)/'stat',self.pid,9999)
            return original(path,limit)
        with patch.object(owned,'read',side_effect=changed):
            with self.assertRaisesRegex(ValueError,'PID reused'):self.linux()
        write_stat(self.proc/str(self.pid)/'stat',self.pid)
        def thread_changed(path,limit):
            if path.name=='syscall':write_stat(self.task/'stat',self.pid,9999)
            return original(path,limit)
        with patch.object(owned,'read',side_effect=thread_changed):r=self.linux()
        self.assertFalse(r['tasks'][0]['identity_verified']);self.assertIn('TID reused',r['tasks'][0]['error'])

    def test_collect_passes_only_explicit_selected_identity_and_distinct_ids(self):
        calls=[]
        def query(args,label,timeout):
            calls.append((args,label,timeout))
            return SimpleNamespace(returncode=0,stdout=json.dumps([window()] if not args else native()))
        r=owned.collect(query,NONCE,[IMAGE],self.pid,{'start_ticks':9664},self.prefix,ARGV,proc=self.proc,now=lambda:0)
        self.assertEqual(calls[1][0],['--owned-threads','655478','408','123456789',NONCE,IMAGE])
        self.assertLessEqual(calls[0][2],1);self.assertLessEqual(calls[1][2],2.5)
        self.assertEqual(r['native']['before']['pid'],408);self.assertEqual(r['linux']['before']['linux_pid'],1040)
        self.assertTrue(r['within_deadline'])

    def test_collect_retains_timeout_malformed_and_ambiguity_without_broader_native_call(self):
        for text in ('[]','{','x'*(512*1024+1),json.dumps([window(),window()])):
            calls=[]
            def query(*args):calls.append(args);return SimpleNamespace(returncode=0,stdout=text)
            r=owned.collect(query,NONCE,[IMAGE],self.pid,{'start_ticks':9664},self.prefix,ARGV,proc=self.proc,now=lambda:0)
            self.assertIn('native_error',r);self.assertEqual(len(calls),1)
            self.assertIn('linux',r)  # independently selected exact Popen, not a fallback scan
        def query(*args):raise subprocess.TimeoutExpired('owned helper',1)
        r=owned.collect(query,NONCE,[IMAGE],self.pid,{'start_ticks':9664},self.prefix,ARGV,proc=self.proc,now=lambda:0)
        self.assertIn('TimeoutExpired',r['native_error'])

    def test_linux_oversized_fields_failclosed_and_unavailable_wchan_is_limited(self):
        (self.proc/str(self.pid)/'cmdline').write_bytes(b'x'*16385)
        with self.assertRaisesRegex(ValueError,'Oversized'):self.linux()
        (self.proc/str(self.pid)/'cmdline').write_bytes((' '.join(ARGV[1:])+'\0').encode())
        (self.task/'wchan').unlink();r=self.linux()
        self.assertIn('unavailable',r['tasks'][0]['wchan']);self.assertTrue(r['tasks'][0]['identity_verified'])


if __name__=='__main__':unittest.main()
