# SPDX-License-Identifier: GPL-2.0-or-later
"""Mocked original contract/ownership tests: no production APIs on non-Windows."""
import base64
import contextlib
import ctypes as C
import io
import json
import os
from pathlib import Path
import subprocess
from types import SimpleNamespace
import unittest
from unittest import mock

import g4_appcontainer_registration_oracle as o

A=o.PREFIX+'a'*32
B=o.PREFIX+'b'*32

def hr(code=0,**values):return dict(hresult=code,hresult_hex=f'0x{code:08x}',**values)

class FakeApi:
    def __init__(self):self.events=[];self.db={};self.exports={n:True for n in o.SIGNATURES};self.freed=False;self.calls=[]
    def derive(self,n):self.calls.append(('derive',n));return n
    def lookup(self,s):
        self.calls.append(('lookup',s))
        return hr(output_nonnull=True,text=self.db[s]) if s in self.db else hr(0x80070490,output_nonnull=False,text=None)
    def register(self,s,n,d):
        self.calls.append(('register',s,d))
        if s in self.db:return hr(0x800700b7)
        self.db[s]=n;return hr()
    def unregister(self,s):
        self.calls.append(('unregister',s))
        if s not in self.db:return hr(0x80070490)
        del self.db[s];return hr()
    def close(self):self.freed=True

def run(api,reader=None):
    names=iter((A,B));value={}
    def child(names,deadline):return {'pid':os.getpid()+123,'exit':0,'reaped':True,'result':dict(v=1,pid=os.getpid()+123,read_only=True,observations=[dict(moniker=n,lookup=api.lookup(n)) for n in names])}
    o.experiment(api,value,reader=reader or child,name_factory=lambda:next(names));return value

class OracleTests(unittest.TestCase):
    def test_default_help_and_output_without_run_never_load_api(self):
        api=mock.Mock(side_effect=AssertionError('no APIs'))
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(o.main([],api_factory=api),0)
            self.assertEqual(o.main(['--output','D:/not-created.json'],api_factory=api),0)
        api.assert_not_called()

    def test_lifecycle_duplicate_independent_reader_isolation_and_cleanup(self):
        api=FakeApi();value=run(api)
        self.assertTrue(value['completed']);self.assertTrue(api.freed);self.assertEqual(api.db,{})
        order=[v['step'] for v in value['order']]
        self.assertLess(order.index('initial_missing'),order.index('register_a'))
        self.assertIn('b_survives_a_unregister',order)
        self.assertEqual(next(v['record']['hresult'] for v in value['order'] if v['step']=='duplicate_a'),0x800700b7)
        self.assertEqual([v['moniker'] for v in value['cleanup']],[B])

    def test_collision_and_access_denied_refuse_all_mutation(self):
        for collision in (True,False):
            api=FakeApi()
            if collision:api.db[A]=A
            else:api.lookup=lambda s:hr(0x80070005,output_nonnull=False,text=None)
            value=run(api)
            self.assertFalse(value['completed']);self.assertEqual(value['cleanup'],[])
            self.assertFalse(any(c[0] in ('register','unregister') for c in api.calls))
            self.assertTrue(api.freed)

    def test_failed_registration_never_owned_or_unregistered(self):
        for failed in (A,B):
            api=FakeApi();original=api.register
            api.register=lambda s,n,d:hr(0x80070005) if s==failed else original(s,n,d)
            value=run(api);self.assertFalse(value['completed'])
            self.assertNotIn(('unregister',failed),api.calls);self.assertEqual(api.db,{})

    def test_midsequence_failure_cleans_both_and_preserves_primary(self):
        api=FakeApi()
        def fail(*args):raise RuntimeError('PRIMARY child failure')
        value=run(api,fail);self.assertIn('PRIMARY',value['error'])
        self.assertEqual({v['moniker'] for v in value['cleanup']},{A,B});self.assertEqual(api.db,{})
        api=FakeApi();api.unregister=mock.Mock(side_effect=RuntimeError('SECONDARY cleanup failure'))
        value=run(api,fail);self.assertIn('PRIMARY',value['error']);self.assertFalse(value['cleanup_complete'])
        self.assertTrue(all('SECONDARY' in v['error'] for v in value['cleanup']));self.assertTrue(api.freed)

    def test_child_persistence_mismatch_and_child_error_evidence_retained(self):
        for reader in (lambda n,d:{'result':{'pid':os.getpid(),'read_only':True,'observations':[]}},
                       lambda n,d:(_ for _ in ()).throw(o.ChildError('PRIMARY',{'reaped':True,'exit':1}))):
            api=FakeApi();value=run(api,reader);self.assertFalse(value['completed']);self.assertEqual(api.db,{})
            self.assertIn('error',value)
            if 'PRIMARY' in value['error']:self.assertEqual(value['child']['exit'],1)

    def test_generated_name_collision_refuses_before_derive_or_mutation(self):
        api=FakeApi();value={};o.experiment(api,value,name_factory=lambda:A)
        self.assertEqual(api.calls,[]);self.assertFalse(value['completed'])

    def test_bounded_child_normal_exit_and_raw_non_utf8_failure(self):
        for code in (0,1):
            child=mock.Mock(pid=123,returncode=code)
            child.communicate.return_value=(b'{"v":1}',b'\xff')
            popen=mock.Mock(return_value=child)
            if code:
                with self.assertRaises(o.ChildError) as caught:o.child_read([A,B],100,now=lambda:0,popen=popen)
                value=caught.exception.record
            else:value=o.child_read([A,B],100,now=lambda:0,popen=popen)
            self.assertTrue(value['reaped']);self.assertEqual(base64.b64decode(value['stderr_base64']),b'\xff')
            child.kill.assert_not_called();self.assertEqual(child.communicate.call_args.kwargs['timeout'],10)

    def test_child_timeout_kill_reap_and_failed_reap_preserve_primary(self):
        for reapfail in (False,True):
            child=mock.Mock(pid=123,returncode=-9);child.poll.return_value=None
            primary=subprocess.TimeoutExpired('owned child',10)
            child.communicate.side_effect=[primary,RuntimeError('reap failure') if reapfail else (b'partial',b'error')]
            with self.assertRaises(o.ChildError) as caught:o.child_read([A,B],100,now=lambda:0,popen=lambda *a,**k:child)
            value=caught.exception.record;child.kill.assert_called_once()
            self.assertEqual(value['reaped'],not reapfail);self.assertIn('TimeoutExpired',value['error'])
            if reapfail:self.assertIn('reap failure',value['cleanup_error'])

    def test_child_no_budget_or_popen_failure_never_adopts_child(self):
        popen=mock.Mock(side_effect=OSError('no child'))
        with self.assertRaises(o.ChildError) as caught:o.child_read([A,B],0,now=lambda:0,popen=popen)
        popen.assert_not_called();self.assertFalse(caught.exception.record['reaped'])
        with self.assertRaises(o.ChildError) as caught:o.child_read([A,B],100,now=lambda:0,popen=popen)
        self.assertNotIn('pid',caught.exception.record);self.assertFalse(caught.exception.record['reaped'])

    def test_child_read_path_is_readonly_and_releases_derived_sids(self):
        api=FakeApi();api.db={A:A,B:B};out=io.StringIO()
        with mock.patch.object(o.sys,'stdin',SimpleNamespace(buffer=io.BytesIO(json.dumps({'monikers':[A,B]}).encode()))),contextlib.redirect_stdout(out):
            self.assertEqual(o.child_main(lambda:api),0)
        value=json.loads(out.getvalue());self.assertTrue(value['read_only']);self.assertTrue(api.freed)
        self.assertFalse(any(c[0] in ('register','unregister') for c in api.calls))

    def test_child_request_wrong_names_or_quota_reject_before_api(self):
        factory=mock.Mock(side_effect=AssertionError('no API'))
        for raw in (b'x'*1025,json.dumps({'monikers':['foreign',B]}).encode(),json.dumps({'monikers':[A,A]}).encode()):
            with mock.patch.object(o.sys,'stdin',SimpleNamespace(buffer=io.BytesIO(raw))),self.assertRaises(ValueError):o.child_main(factory)
        factory.assert_not_called()

    def test_output_guard_rejects_existing_foreign_or_noncanonical_before_api(self):
        for fault in ('exists','drive','canonical','symlink'):
            parent=mock.Mock();parent.resolve.return_value=object() if fault=='canonical' else parent
            path=SimpleNamespace(parent=parent,drive='C:' if fault=='drive' else 'D:',suffix='.json',
                                 is_absolute=lambda:True,is_symlink=lambda:fault=='symlink')
            with mock.patch.object(o.os,'name','nt'),mock.patch.object(o,'Path',return_value=path),mock.patch.object(o.os.path,'lexists',return_value=fault=='exists'):
                with self.assertRaises(ValueError):o.output_path('not-actually-created')

    def test_no_production_api_load_on_non_windows(self):
        if os.name!='nt':
            with self.assertRaisesRegex(RuntimeError,'Native Windows'):o.NativeApi()

class BindingTests(unittest.TestCase):
    def api(self):
        self.calls=[];self.buffers=[]
        class Fn:
            def __init__(self,fn):self.fn=fn
            def __call__(self,*args):return self.fn(*args)
        def buffer(out,text):
            value=C.create_unicode_buffer(text);self.buffers.append(value)
            C.cast(out,C.POINTER(C.c_void_p))[0]=C.addressof(value)
        functions={}
        for name in o.SIGNATURES:
            def invoke(*args,n=name):
                self.calls.append(n)
                if n=='DeriveAppContainerSidFromAppContainerName':C.cast(args[1],C.POINTER(C.c_void_p))[0]=42;return 0
                if n=='ConvertSidToStringSidW':buffer(args[1],'S-1-15-2-1');return 1
                if n=='AppContainerLookupMoniker':buffer(args[1],A);return 0
                return None if n in ('FreeSid','LocalFree','AppContainerFreeMemory') else 0
            functions[name]=Fn(invoke)
        self.functions=functions
        return o.NativeApi(loader=lambda n:SimpleNamespace(**functions),set_error=lambda v:None,get_error=lambda:123)

    def test_actual_ctypes_signatures_and_matching_deallocators(self):
        api=self.api()
        self.assertEqual(self.functions['AppContainerRegisterSid'].argtypes,[C.c_void_p,C.c_wchar_p,C.c_wchar_p])
        self.assertEqual(self.functions['AppContainerUnregisterSid'].argtypes,[C.c_void_p])
        self.assertEqual(self.functions['AppContainerLookupMoniker'].argtypes,[C.c_void_p,C.POINTER(C.c_void_p)])
        self.assertIs(self.functions['AppContainerLookupMoniker'].restype,C.c_int32)
        self.assertIsNone(self.functions['AppContainerFreeMemory'].restype)
        sid=api.derive(A);value=api.lookup(sid);api.close()
        self.assertEqual(value['text'],A);self.assertEqual(value['last_error'],123);self.assertEqual(value['hresult'],0)
        self.assertEqual(self.calls,['DeriveAppContainerSidFromAppContainerName','ConvertSidToStringSidW','LocalFree','AppContainerLookupMoniker','AppContainerFreeMemory','FreeSid'])

    def test_conversion_failure_still_frees_successful_sid_and_failed_lookup_never_frees(self):
        api=self.api();self.functions['ConvertSidToStringSidW'].fn=lambda *args:0
        with self.assertRaises(RuntimeError):api.derive(A)
        api.close();self.assertEqual(self.calls[-1],'FreeSid')
        api=self.api();self.functions['AppContainerLookupMoniker'].fn=lambda *args:-2147023728
        value=api.lookup(42);self.assertTrue(o.absent(value));self.assertNotIn('AppContainerFreeMemory',self.calls)

    def test_unavailable_export_reports_presence_without_any_native_calls(self):
        with self.assertRaises(o.ApiUnavailable) as caught:o.NativeApi(loader=lambda n:SimpleNamespace(),set_error=lambda v:None,get_error=lambda:0)
        self.assertFalse(any(caught.exception.exports.values()))

if __name__=='__main__':unittest.main()
