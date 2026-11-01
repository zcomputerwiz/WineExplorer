# SPDX-License-Identifier: GPL-2.0-or-later
"""Timeout-only, <=8 explicit owned Edge threads. NOT UI resume/graphics proof."""
import importlib.util
import json
from pathlib import Path
import re
import time

import g4_edge_owned_threads as owned
from g4_stack_trace import selected_trace

# Reuse the calibrated fixed owned-operation transport and exact tool pins.
spec=importlib.util.spec_from_file_location('edge_stack_transport',Path(__file__).with_name('check-g4-stack-calibration.py'))
transport=importlib.util.module_from_spec(spec);spec.loader.exec_module(transport)
ROLES=('GUI','Chrome_IOThread','BrokerEvent','ThreadPoolSingleThreadCOMSTASharedForegroundBlocking',
       'ThreadPoolSingleThreadForegroundBlocking','ThreadPoolSingleThreadCOMSTASharedForeground')
IMAGES=(r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
        r'Z:\home\debian\wex-g4\edge-install-r1-v2-20261022\prefix\drive_c\Program Files (x86)\Microsoft\Edge\Application\msedge.exe')


def guard_tools():
    actual={p:transport.install.digest(Path(p)) for p in transport.TOOLS}
    if actual!=transport.TOOLS: raise ValueError('Calibrated WineDbg/dbghelp pins differ')
    return {'on_disk_sha256':actual,'loaded_file_identity':'NOT_INDEPENDENTLY_TRACED',
            'loader_policy':'Exact BUILD wine/WINE/WINESERVER; builtin dbghelp=b; empty symbol paths'}


def valid_thread(t,pid,*,selected=True):
    if (t['queried'] is not True or t['query_error']!=0 or t['owner_pid']!=pid
        or not owned.integer(t['creation_time']) or t['name_error']<0
        or (selected and (t['exit_code']!=259 or not t['name']))):
        raise ValueError('Unsuccessful/inactive/unnamed thread; positive SUCCEEDED HRESULT is valid')


def ordered(threads,gui):
    selected=[];seen=set()
    for role in ROLES:
        rows=([t for t in threads if t['tid']==gui] if role=='GUI' else [t for t in threads if t['name']==role])
        if role in ROLES[:3] and len(rows)!=1: raise ValueError('Missing/ambiguous required role: '+role)
        for t in sorted(rows,key=lambda t:t['tid']):
            if t['tid'] not in seen:
                selected.append({k:t[k] for k in ('tid','owner_pid','creation_time','name')}|{'role':role});seen.add(t['tid'])
    if len(selected)>8: raise ValueError('More than eight selected threads; never truncate')
    return selected


def selection(value):
    if type(value) is not dict or set(value)!={'hwnd','pid','creation_time','nonce','image','gui_tid','threads'}:
        raise ValueError('Exact owned selection schema required')
    for key in ('hwnd','pid','creation_time','gui_tid'):
        if not owned.integer(value[key],2**32-1 if key in ('pid','gui_tid') else 2**64-1): raise ValueError('Invalid selection numeric: '+key)
    if (not isinstance(value['nonce'],str) or not re.fullmatch('[0-9a-f]{32}',value['nonce'])
        or not isinstance(value['image'],str) or value['image'].casefold() not in [s.casefold() for s in IMAGES]
        or type(value['threads']) is not list or not 1<=len(value['threads'])<=8): raise ValueError('Invalid selection nonce/image/count')
    seen=set()
    for t in value['threads']:
        if (type(t) is not dict or set(t)!={'tid','owner_pid','creation_time','name','role'}
            or not owned.integer(t['tid'],2**32-1) or t['tid'] in seen
            or type(t['owner_pid']) is not int or t['owner_pid']!=value['pid']
            or not owned.integer(t['creation_time']) or not isinstance(t['name'],str) or not 1<=len(t['name'])<=256
            or t['role'] not in ROLES): raise ValueError('Invalid selected thread schema/identity')
        seen.add(t['tid'])
    if ordered(value['threads'],value['gui_tid'])!=value['threads']: raise ValueError('Noncanonical selected roles/order')
    return value


def select(inventory,nonce,aliases):
    if inventory.get('native_error') or inventory.get('linux_error') or inventory.get('within_deadline') is not True:
        raise ValueError('Prior fresh inventory incomplete')
    w=owned.select_window([inventory['selected_native_window']],nonce,aliases)
    n=owned.native_record(json.dumps(inventory['native']),w,nonce)
    if n['description_available'] is not True: raise ValueError('Native thread names unavailable')
    # A FAILED initial name/query could hide a required role. Successful empty
    # names are legitimate unnamed nonselected threads, not failed queries.
    for t in n['threads']: valid_thread(t,w['pid'],selected=False)
    threads=ordered(n['threads'],n['before']['gui_tid'])
    selected={t['tid'] for t in threads}
    for t in n['threads']:
        if t['tid'] in selected: valid_thread(t,w['pid'])
    value=selection({k:w[k] for k in ('hwnd','pid','creation_time','image')}|
                    {'gui_tid':n['before']['gui_tid'],'nonce':nonce,'threads':threads})
    return value,[{'tid':t['tid'],'name':t['name'],'reason':('Unnamed nonselected (successful name query)' if not t['name'] else 'Outside explicitly scoped roles')} for t in n['threads'] if t['tid'] not in selected]


def commands(value):
    value=selection(value)
    return 'attach '+str(value['pid'])+'\n'+''.join('bt '+str(t['tid'])+'\n' for t in value['threads'])+'detach\nquit\n'


def parse(text,value):
    value=selection(value)
    return selected_trace(text,value['pid'],[t['tid'] for t in value['threads']],value['image'],'msedge')


def revalidate(text,w,value):
    n=owned.native_record(text,w,value['nonce'])
    if n['before']['gui_tid']!=value['gui_tid'] or n['description_available'] is not True:
        raise ValueError('GUI/name capability changed')
    threads={t['tid']:t for t in n['threads']}
    for t in value['threads']:
        actual=threads.get(t['tid'])
        if actual is None: raise ValueError('Selected thread disappeared')
        valid_thread(actual,value['pid'])
        if any(actual[k]!=t[k] for k in ('tid','owner_pid','creation_time','name')): raise ValueError('Selected thread reused/changed')
    return n  # nonselected background churn is retained, not compared


def collect(inventory,nonce,aliases,browser,sample,prefix,argv,env,output,observer,monitor,*,seconds=20,now=time.monotonic):
    start=now();deadline=start+min(20,seconds)
    r={'classification':'TIMEOUT-ONLY SELECTED STACK DIAGNOSTIC; NOT consumer/graphics acceptance',
       'status':'NOT_OBSERVED','ui_resume':'NOT_PROVEN','detach_api_return':'NOT_PROVEN',
       'started_monotonic':start,'maximum_seconds':min(20,seconds),'operations':[],
       'termination_warning':'Timeout/quota/forced debugger kill may terminate attached OWNED Edge; never harmless'}
    attempted=False;value=None
    def bound(cap):
        left=deadline-now()
        if left<=0: raise TimeoutError('Whole selected-stack observation deadline')
        return min(cap,left)
    def linux():
        monitor()
        if browser.poll() is not None: raise ValueError('Owned browser exited')
        if not isinstance(sample,dict) or not owned.integer(sample.get('start_ticks')): raise ValueError('Missing exact original Popen identity')
        return owned.linux_identity(Path('/proc'),browser.pid,sample['start_ticks'],prefix,argv)
    def query(args,label,cap):
        out,_=transport.bounded([str(transport.BUILD/'wine'),str(observer)]+args,env,output,label,bound(cap),r['operations'],monitor=monitor)
        return out
    def native(label):
        selected_window=owned.select_window(owned.decode(query([],label+'-window',1.5)),nonce,aliases)
        if selected_window!=w: raise ValueError('Nonce window/image/title/geometry changed')
        return revalidate(query(['--owned-threads',str(value['hwnd']),str(value['pid']),str(value['creation_time']),nonce,value['image']],label,2.5),w,value)
    try:
        if seconds<20: raise TimeoutError('Insufficient full20s extra observation budget; no attach')
        r['tools']=guard_tools();value,excluded=select(inventory,nonce,aliases)
        r.update(selection=value,excluded_roles=excluded)
        w=inventory['selected_native_window']
        r['linux_before']=linux();r['native_before']=native('stack-native-before')
        text=commands(value);path=output/'selected-stack-commands.txt';path.write_text(text)
        r['command_sha256']=transport.install.digest(path)
        private=dict(env);private.update(WINE=str(transport.BUILD/'wine'),WINESERVER=str(transport.BUILD/'server/wineserver'),
            WINEPREFIX=str(prefix),_NT_SYMBOL_PATH='',_NT_ALT_SYMBOL_PATH='')
        private['WINEDLLOVERRIDES']+=';dbghelp=b'
        r['debugger_loader_policy']={k:private[k] for k in ('WINE','WINESERVER','WINEPREFIX','WINEDLLOVERRIDES','_NT_SYMBOL_PATH','_NT_ALT_SYMBOL_PATH')}
        # After debugger10, allow both native queries4 + Linux/parser/checks1.
        if bound(20)<15: raise TimeoutError('Insufficient debugger/post-ownership budget; no attach')
        monitor();attempted=True  # dispatch attempt, NOT proof DebugActiveProcess succeeded
        out,err=transport.bounded([str(transport.BUILD/'wine'),str(transport.BUILD/'programs/winedbg/x86_64-windows/winedbg.exe'),
            '--file',transport.win(path)],private,output,'selected-debugger',10,r['operations'],monitor=monitor)
        r['trace']=parse(out+'\n'+err,value)
    except Exception as error: r['error']=repr(error)
    finally:
        if attempted:
            # Preserve independent post evidence even if parsing/debugger failed.
            for label,callback in (('linux_after',linux),('native_after',lambda:native('stack-native-after')),('tools_after',guard_tools)):
                try: bound(20);r[label]=callback()
                except Exception as error: r.setdefault('post_observation_errors',[]).append(label+': '+repr(error))
            if r.get('linux_after')!=r.get('linux_before'): r.setdefault('post_observation_errors',[]).append('Linux identity changed/unavailable')
        r.update(finished_monotonic=now(),within_deadline=now()<=deadline,debugger_attempted=attempted,
                 attachment_binding_observed=bool(r.get('trace')))
        if (r.get('trace') and not r.get('error') and not r.get('post_observation_errors') and r['within_deadline']
            and all(t.get('within_deadline') is True and not t.get('error') and not t.get('cleanup_error') for t in r['operations'])):
            r['status']='STACKS_OBSERVED_POST_OWNERSHIP_ONLY'
    return r
