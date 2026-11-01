# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline source-format/callback tests; NOT native/Wine/debugger execution."""
import copy
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

import g4_edge_selected_stacks as stacks
from test_g4_edge_owned_threads import window,native,NONCE,IMAGE


def inventory():
    n=native();threads=[]
    names=['CrBrowserMain','Chrome_IOThread','BrokerEvent',stacks.ROLES[3],stacks.ROLES[3],
           stacks.ROLES[4],stacks.ROLES[4],stacks.ROLES[5]]+['']*7+['ThreadPoolService']*41
    for i,name in enumerate(names):
        threads.append(dict(tid=412 if i==0 else 1000+i,owner_pid=408,queried=True,query_error=0,
            creation_time=123400+i,exit_code=259,name_error=0x10000000,name=name))
    n['threads']=list(reversed(threads))  # no accidental input enumeration order
    return dict(within_deadline=True,selected_native_window=window(),native=n,linux={'before':{'linux_pid':1040}})


def trace(value):
    return ('WineDbg attached to pid '+f"{value['pid']:04x}"+'\n0x000000007000 ntdll+0x1: retq\n'+
        ''.join(f"\nBacktracing for thread {t['tid']:04x} ({t['name']}) in process {value['pid']:04x} ({value['image']}):\nBacktrace:\n"
                '=>0 0x000000007000 in ntdll (+0x10) (0000000000000000)\n'
                '  1 0x000000007100 WaitForSingleObject+0x45(handle=<internal error>) in kernelbase (0x000000008100)\n'
                '  2 0x00000140002000 in msedge (+0x2000) (0000000000000000)\n' for t in value['threads']))


class SelectionTests(unittest.TestCase):
    def select(self,inv=None):return stacks.select(inv or inventory(),NONCE,[IMAGE])

    def test_actual_r1_shaped_56_with_seven_successful_unnamed_selects_eight(self):
        inv=inventory();value,excluded=self.select(inv)
        self.assertEqual(len(inv['native']['threads']),56);self.assertEqual(len(value['threads']),8)
        self.assertEqual([t['role'] for t in value['threads']],list(stacks.ROLES[:3])+[stacks.ROLES[3]]*2+[stacks.ROLES[4]]*2+[stacks.ROLES[5]])
        self.assertEqual(len(excluded),48);self.assertEqual(sum(not t['name'] for t in excluded),7)
        for role in stacks.ROLES[3:]:
            tids=[t['tid'] for t in value['threads'] if t['role']==role];self.assertEqual(tids,sorted(tids))

    def test_missing_ambiguous_overflow_unavailable_or_incomplete(self):
        for action in ('missing-io','duplicate-broker','overflow','negative-name','failed-query','exited-selected','unnamed-gui','no-description','inventory-error'):
            inv=inventory();threads=inv['native']['threads']
            def named(name):return next(t for t in threads if t['name']==name)
            if action=='missing-io':named('Chrome_IOThread')['name']='Other'
            if action=='duplicate-broker':
                t=copy.deepcopy(named('BrokerEvent'));t['tid']=5000;threads.append(t)
            if action=='overflow':
                t=copy.deepcopy(named(stacks.ROLES[3]));t['tid']=5001;threads.append(t)
            if action=='negative-name':threads[0]['name_error']=-1
            if action=='failed-query':threads[0].update(queried=False,query_error=5,creation_time=0)
            if action=='exited-selected':named('Chrome_IOThread')['exit_code']=0
            if action=='unnamed-gui':named('CrBrowserMain')['name']=''
            if action=='no-description':inv['native']['description_available']=False
            if action=='inventory-error':inv['linux_error']='wrong prefix/start/argv'
            with self.subTest(action=action),self.assertRaises(ValueError): self.select(inv)

    def test_gui_dedup_by_identity_and_variable_lists(self):
        inv=inventory();n=inv['native'];n['before']['gui_tid']=n['after']['gui_tid']=1001
        value,_=self.select(inv)
        self.assertEqual(value['threads'][0]['tid'],1001);self.assertEqual(len(value['threads']),7)
        n['threads']=[t for t in n['threads'] if t['name'] in ('CrBrowserMain','Chrome_IOThread','BrokerEvent')]
        value,_=self.select(inv);self.assertEqual(len(value['threads']),2)  # GUI==IO dedup

    def test_strict_schema_booleans_reuse_foreign_image_and_commands(self):
        value,_=self.select()
        for field,bad in [('pid',True),('hwnd',0),('creation_time',False),('gui_tid',-1),('nonce','a'*32+'\nbt all'),('image','msedge.exe')]:
            r=copy.deepcopy(value);r[field]=bad
            with self.subTest(field=field),self.assertRaises(ValueError):stacks.commands(r)
        for field,bad in [('tid',True),('owner_pid',True),('creation_time',0),('role','bt all'),('name','')]:
            r=copy.deepcopy(value);r['threads'][0][field]=bad
            with self.assertRaises(ValueError):stacks.commands(r)
        for action in ('unknown','duplicate','reorder'):
            r=copy.deepcopy(value)
            if action=='unknown':r['command']='bt all'
            if action=='duplicate':r['threads'][1]=r['threads'][0]
            if action=='reorder':r['threads'].reverse()
            with self.assertRaises(ValueError):stacks.commands(r)
        expected='attach 408\n'+''.join(f"bt {t['tid']}\n" for t in value['threads'])+'detach\nquit\n'
        self.assertEqual(stacks.commands(value),expected)

    def test_post_native_selected_changes_rejected_nonselected_churn_allowed(self):
        inv=inventory();value,_=self.select(inv);n=copy.deepcopy(inv['native'])
        selected={t['tid'] for t in value['threads']}
        n['threads']=[t for t in n['threads'] if t['tid'] in selected]
        n['threads'].append(dict(tid=9000,owner_pid=408,queried=False,query_error=5,creation_time=0,exit_code=0,name_error=-1,name=''))
        stacks.revalidate(json.dumps(n),window(),value)
        for field,bad in [('creation_time',999),('owner_pid',999),('name','changed'),('name_error',-1),('exit_code',0),('queried',False)]:
            changed=copy.deepcopy(n);changed['threads'][0][field]=bad
            with self.subTest(field=field),self.assertRaises(ValueError):stacks.revalidate(json.dumps(changed),window(),value)
        changed=copy.deepcopy(n);changed['before']['title']=changed['after']['title']='WEXEDGE ready'
        with self.assertRaises(ValueError):stacks.revalidate(json.dumps(changed),window(),value)

    def test_variable_source_format_full_image_msedge_zero_rbp(self):
        value,_=self.select();r=stacks.parse(trace(value),value)
        self.assertEqual(len(r['selected_stacks']),8);self.assertIn('ntdll+0x1',r['initial_unbound_output'])
        self.assertFalse(r['detach_api_return_proven'])
        for old,new in [('in process 0198','in process 0199'),(IMAGE,'msedge.exe'),('in msedge','in other'),
                        ('0x000000007000 in','0x000000000000 in'),('  2 0x','  4 0x'),('Backtrace:\n','')]:
            with self.subTest(old=old),self.assertRaises(ValueError):stacks.parse(trace(value).replace(old,new),value)
        for extra in ('Unknown thread id (ffff) in process (0198)\n','WineDbg attached to pid 9999\n','No symbols found\n'):
            with self.assertRaises(ValueError):stacks.parse(trace(value)+extra,value)
        first=value['threads'][0]
        text=trace(value).replace(f"Backtracing for thread {first['tid']:04x} ({first['name']}) in process 0198 ({IMAGE}):",'')
        with self.assertRaises(ValueError):stacks.parse(text,value)


class CollectionTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.output=Path(self.tmp.name)
        self.inv=inventory();self.value,_=stacks.select(self.inv,NONCE,[IMAGE])
        self.calls=[];self.clock=0;self.fail={};self.browser=Mock(pid=1040);self.browser.poll.return_value=None
        self.monitor=Mock();self.post=copy.deepcopy(self.inv['native']);self.linux=[{'pid':1040,'start':9664}]*2

    def bounded(self,argv,env,output,label,seconds,operations,**kwargs):
        kwargs['monitor']();self.calls.append((argv,dict(env),label,seconds))
        operations.append({'within_deadline':True,'argv':argv});self.clock+=.1
        if label in self.fail: raise self.fail[label]
        if label.endswith('-window'):return json.dumps([window()]),''
        if label=='selected-debugger':return trace(self.value),''
        return json.dumps(self.post if label.endswith('-after') else self.inv['native']),''

    def collect(self,seconds=20):
        with patch.object(stacks,'guard_tools',return_value={'pins':'mock'}), \
             patch.object(stacks.transport,'bounded',side_effect=self.bounded), \
             patch.object(stacks.owned,'linux_identity',side_effect=self.linux):
            return stacks.collect(self.inv,NONCE,[IMAGE],self.browser,{'start_ticks':9664},Path('/owned/prefix'),
                ['wine','msedge','--app=exact','--user-data-dir=exact'],dict(WINEDLLOVERRIDES='d3d11=b'),
                self.output,self.output/'observer.exe',self.monitor,seconds=seconds,now=lambda:self.clock)

    def test_success_exact_dispatch_and_post_ownership_not_ui_resume(self):
        r=self.collect();self.assertEqual(r['status'],'STACKS_OBSERVED_POST_OWNERSHIP_ONLY')
        self.assertTrue(r['attachment_binding_observed']);self.assertTrue(r['debugger_attempted'])
        self.assertEqual(r['ui_resume'],'NOT_PROVEN');self.assertEqual(r['detach_api_return'],'NOT_PROVEN')
        self.assertEqual([c[2] for c in self.calls],['stack-native-before-window','stack-native-before','selected-debugger','stack-native-after-window','stack-native-after'])
        argv,env,_,timeout=self.calls[2]
        self.assertEqual(timeout,10);self.assertEqual(argv,[str(stacks.transport.BUILD/'wine'),str(stacks.transport.BUILD/'programs/winedbg/x86_64-windows/winedbg.exe'),'--file',stacks.transport.win(self.output/'selected-stack-commands.txt')])
        self.assertEqual(env['WINEDLLOVERRIDES'],'d3d11=b;dbghelp=b');self.assertEqual(env['_NT_SYMBOL_PATH'],'')
        self.assertEqual(env['_NT_ALT_SYMBOL_PATH'],'')

    def test_insufficient_initial_or_post_query_budget_never_dispatches_debugger(self):
        self.assertEqual(self.collect(seconds=19)['status'],'NOT_OBSERVED');self.assertEqual(self.calls,[])
        original=self.bounded
        def slow(*a,**kw):
            result=original(*a,**kw);self.clock+=4;return result
        self.bounded=slow
        r=self.collect();self.assertIn('Insufficient debugger',r['error']);self.assertFalse(r['debugger_attempted'])

    def test_wrong_linux_identity_and_native_before_never_attach(self):
        self.linux=ValueError('Wrong Linux PID/start/prefix/profile')
        r=self.collect();self.assertIn('Wrong Linux',r['error']);self.assertEqual(self.calls,[])
        self.linux=[{'pid':1040}]*2;self.inv['native']['before']['creation_time']+=1
        r=self.collect();self.assertEqual(r['status'],'NOT_OBSERVED');self.assertFalse(r['debugger_attempted'])

    def test_timeout_quota_cleanup_error_primary_retained_and_post_attempted(self):
        for error in (TimeoutError('debugger timeout may kill browser'),ValueError('quota'),RuntimeError('kill/reap failed')):
            self.linux=[{'pid':1040}]*2;self.fail={'selected-debugger':error};self.calls=[]
            r=self.collect();self.assertIn(str(error),r['error']);self.assertEqual(r['status'],'NOT_OBSERVED')
            self.assertIn('native_after',r);self.assertTrue(r['debugger_attempted']);self.assertFalse(r['attachment_binding_observed'])
            self.assertEqual(r['ui_resume'],'NOT_PROVEN')

    def test_post_selected_reuse_or_browser_death_and_nonselected_churn(self):
        self.post['threads'][0]['name']='different background';r=self.collect()
        self.assertEqual(r['status'],'STACKS_OBSERVED_POST_OWNERSHIP_ONLY')
        self.linux=[{'pid':1040}]*2
        next(t for t in self.post['threads'] if t['tid']==412)['creation_time']+=1
        r=self.collect();self.assertEqual(r['status'],'NOT_OBSERVED');self.assertIn('reused',str(r['post_observation_errors']))
        self.linux=[{'pid':1040},ValueError('browser died')]
        r=self.collect();self.assertEqual(r['status'],'NOT_OBSERVED');self.assertIn('browser died',str(r['post_observation_errors']))

    def test_browser_log_monitor_error_never_claims_observed_stacks(self):
        self.monitor.side_effect=ValueError('browser log quota')
        r=self.collect();self.assertIn('quota',r['error']);self.assertEqual(self.calls,[])


if __name__=='__main__':unittest.main()
