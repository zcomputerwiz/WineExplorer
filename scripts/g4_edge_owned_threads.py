# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only, separately labelled native owner threads and Linux wait metadata.

Never attach/suspend/query contexts, read stacks/memory, or map Windows IDs to
Linux IDs. Linux selection is ONLY the recorded Popen PID/start/prefix/argv.
"""
import json
from pathlib import Path
import re
import time


def integer(value, maximum=2**64-1):
    return type(value) is int and 0 < value <= maximum


def decode(text, limit=512*1024):
    if not isinstance(text,str) or len(text.encode('utf-8'))>limit: raise ValueError('Oversized native output')
    def pairs(items):
        result={}
        for k,v in items:
            if k in result: raise ValueError('Duplicate native JSON key')
            result[k]=v
        return result
    return json.loads(text,object_pairs_hook=pairs,parse_constant=lambda _: (_ for _ in ()).throw(ValueError('Nonfinite JSON')))


def validate_rect(rect):
    if (not isinstance(rect,dict) or set(rect)!= {'x','y','width','height'} or
        any(type(v) is not int or not 0<=v<=8192 for v in rect.values()) or
        not rect['width'] or not rect['height']): raise ValueError('Invalid owner geometry numeric/schema')


def select_window(rows,nonce,images):
    if not re.fullmatch('[0-9a-f]{32}',nonce): raise ValueError('Invalid nonce')
    if (not isinstance(rows,list) or len(rows)>4096 or
        any(not isinstance(w,dict) or not isinstance(w.get('title'),str) for w in rows)):
        raise ValueError('Malformed native window inventory')
    selected=[w for w in rows if '/'+nonce+'/index.html' in w['title']]
    if len(selected)!=1: raise ValueError('Expected exactly one nonce-bearing native window')
    w=selected[0]
    if (set(w)!= {'hwnd','pid','creation_time','visible','top_level','geometry_valid','fixed','title','image','class','window','client'}
        or type(w.get('fixed')) is not int or not 0<=w['fixed']<=262144
        or w.get('visible') is not True or w.get('top_level') is not True or w.get('geometry_valid') is not True
        or w.get('class')!='Chrome_WidgetWin_1' or len(w['title'])>2048
        or not integer(w.get('hwnd')) or not integer(w.get('pid'),2**32-1)
        or not integer(w.get('creation_time')) or not isinstance(w.get('image'),str)
        or w['image'].casefold() not in [v.casefold() for v in images]):
        raise ValueError('Invalid/foreign/child native owner')
    for name in ('window','client'):
        validate_rect(w.get(name))
    c,b=w['client'],w['window']
    if c['x']<b['x'] or c['y']<b['y'] or c['x']+c['width']>b['x']+b['width'] or c['y']+c['height']>b['y']+b['height']:
        raise ValueError('Native client outside window')
    return w


def native_record(text,w,nonce):
    r=decode(text)
    if not isinstance(r,dict) or r.get('ok') is not True: raise ValueError('Native observation query failure: '+repr(r)[:512])
    if set(r)!= {'ok','hwnd','nonce','before','after','description_available','threads'}: raise ValueError('Unknown native schema fields')
    if r.get('hwnd')!=w['hwnd'] or type(r.get('hwnd')) is not int or r.get('nonce')!=nonce:
        raise ValueError('Native HWND/nonce mismatch')
    for stage in ('before','after'):
        identity=r.get(stage)
        if not isinstance(identity,dict) or set(identity)!= {'pid','gui_tid','creation_time','image','title','window','client'}:
            raise ValueError('Missing/unknown native identity fields')
        for rect in ('window','client'):
            validate_rect(identity.get(rect))
        for field in ('pid','creation_time','image','title','window','client'):
            if identity.get(field)!=w[field] or type(identity.get(field)) is not type(w[field]):
                raise ValueError('Native owner revalidation mismatch: '+field)
        if not integer(identity.get('gui_tid'),2**32-1): raise ValueError('Invalid GUI TID')
    if r['before']!=r['after']: raise ValueError('Native owner changed during observation')
    threads=r.get('threads')
    if (type(r.get('description_available')) is not bool or not isinstance(threads,list)
        or not 1<=len(threads)<=128): raise ValueError('Invalid native thread count/schema')
    seen=set()
    for t in threads:
        if (not isinstance(t,dict) or set(t)!= {'tid','owner_pid','queried','query_error','creation_time','exit_code','name_error','name'}
            or not integer(t.get('tid'),2**32-1) or t['tid'] in seen
            or type(t.get('owner_pid')) is not int or t['owner_pid']!=w['pid']
            or type(t.get('queried')) is not bool or type(t.get('query_error')) is not int
            or not 0<=t['query_error']<=2**32-1 or not isinstance(t.get('name'),str) or len(t['name'])>256
            or type(t.get('name_error')) is not int or not -2**31<=t['name_error']<2**31
            or type(t.get('exit_code')) is not int or not 0<=t['exit_code']<=2**32-1):
            raise ValueError('Native TID/owner/schema mismatch')
        if t['queried']:
            if t['query_error'] or not integer(t.get('creation_time')): raise ValueError('Native thread creation/query mismatch')
        elif not t['query_error'] or type(t.get('creation_time')) is not int or t['creation_time']!=0:
            raise ValueError('Missing native query failure evidence')
        seen.add(t['tid'])
    if r['before']['gui_tid'] not in seen: raise ValueError('GUI TID missing from owned snapshot')
    return r


def read(path,limit):
    with path.open('rb') as stream: value=stream.read(limit+1)
    if len(value)>limit: raise ValueError('Oversized proc field: '+path.name)
    return value


def stat_record(raw,tid):
    text=raw.decode();end=text.rfind(')');fields=text[end+2:].split()
    if end<0 or int(text.split(' ',1)[0])!=tid or len(fields)<20 or not integer(int(fields[19])):
        raise ValueError('Invalid proc stat identity')
    return {'tid':tid,'start_ticks':int(fields[19]),'state':fields[0]}


def linux_identity(proc,pid,start,prefix,argv):
    identity=stat_record(read(proc/str(pid)/'stat',4096),pid)
    if identity['start_ticks']!=start: raise ValueError('Linux PID reused')
    env=read(proc/str(pid)/'environ',64*1024).split(b'\0')
    prefixes=[v for v in env if v.startswith(b'WINEPREFIX=')]
    if prefixes!=[('WINEPREFIX='+str(prefix)).encode()]: raise ValueError('Linux prefix mismatch')
    raw=read(proc/str(pid)/'cmdline',16*1024)
    # Wine replaces the command title with an unquoted, padded Linux-image
    # string. Compare its ENTIRE known rendering, never shlex-split paths with
    # spaces or accept an arbitrary basename/flag substring.
    args=raw.rstrip(b'\0').decode().split('\0')
    if args: args[-1]=args[-1].rstrip(' ')
    if args==argv or args==argv[1:]: style='argv_array'
    elif len(args)==1 and args[0]==' '.join(argv[1:]): style='wine_flat_padded'
    else: raise ValueError('Linux image/app URL/profile/argv mismatch')
    if any(v.startswith('--type=') for v in argv): raise ValueError('Child process cannot be selected')
    return {'linux_pid':pid,'start_ticks':start,'prefix':str(prefix),'argv_style':style,'observed_cmdline':args}


def linux_record(pid,sample,prefix,argv,deadline,*,proc=Path('/proc'),now=time.monotonic):
    if not integer(pid,2**31-1) or not isinstance(sample,dict) or not integer(sample.get('start_ticks')):
        raise ValueError('No recorded Popen Linux identity; no scan/fallback')
    def remaining():
        if now()>=deadline: raise TimeoutError('Owned observation deadline')
    remaining();before=linux_identity(proc,pid,sample['start_ticks'],prefix,argv)
    tasks=[]
    for path in (proc/str(pid)/'task').iterdir():
        remaining()
        if not path.name.isdecimal() or not integer(int(path.name),2**31-1): raise ValueError('Invalid task identity')
        tasks.append(path)
        if len(tasks)>128: raise ValueError('Linux task limit exceeded; no truncation')
    if not tasks or not any(p.name==str(pid) for p in tasks): raise ValueError('Missing Linux main task')
    records=[]
    for path in sorted(tasks,key=lambda p:int(p.name)):
        remaining();tid=int(path.name);row={'linux_tid':tid}
        try:
            a=stat_record(read(path/'stat',4096),tid)
            for name,limit in (('comm',64),('wchan',128),('syscall',512)):
                remaining()
                try: row[name]=read(path/name,limit).decode(errors='replace').rstrip('\n')
                except OSError as error: row[name]={'unavailable':repr(error)[:512]}
            b=stat_record(read(path/'stat',4096),tid)
            if a['start_ticks']!=b['start_ticks']: raise ValueError('Linux TID reused')
            row.update(before=a,after=b,identity_verified=True)
        except (OSError,ValueError) as error: row.update(identity_verified=False,error=repr(error)[:512])
        records.append(row)
    remaining();after=linux_identity(proc,pid,sample['start_ticks'],prefix,argv);remaining()
    if before!=after: raise ValueError('Linux owner changed during observation')
    return {'classification':'LINUX ONLY; no Win32 ID mapping/stacks','before':before,'after':after,'tasks':records}


def collect(native,nonce,images,pid,sample,prefix,argv,*,seconds=5,now=time.monotonic,proc=Path('/proc')):
    start=now();deadline=start+min(5,seconds)
    record={'classification':'READ_ONLY_INVENTORIES; Win32 and Linux IDs independent; not stacks/acceptance',
            'started_monotonic':start,'maximum_seconds':min(5,seconds)}
    def budget(cap):
        left=deadline-now()
        if left<=0: raise TimeoutError('Owned observation deadline')
        return min(cap,left)
    try:
        rows=native([], 'owned-window-selection',budget(1))
        if rows.returncode: raise RuntimeError('Native window selection failed')
        w=select_window(decode(rows.stdout),nonce,images)
        record['selected_native_window']=w
        result=native(['--owned-threads',str(w['hwnd']),str(w['pid']),str(w['creation_time']),nonce,w['image']],
                      'owned-native-threads',budget(2.5))
        if result.returncode: raise RuntimeError('Native thread query failed (raw logs retained)')
        record['native']=native_record(result.stdout,w,nonce);budget(5)
    except Exception as error: record['native_error']=repr(error)[:1024]
    try:
        budget(5);record['linux']=linux_record(pid,sample,prefix,argv,deadline,proc=proc,now=now)
    except Exception as error: record['linux_error']=repr(error)[:1024]
    record['finished_monotonic']=now()
    record['within_deadline']=now()<=deadline
    if not record['within_deadline']: record['observation_error']='Whole observation deadline exceeded'
    if len(json.dumps(record).encode())>1024**2: raise ValueError('Owned observation JSON limit exceeded')
    return record
