# SPDX-License-Identifier: GPL-2.0-or-later
"""Read-only, separately labelled native owner threads and Linux wait metadata.

Never attach/suspend/query contexts, read stacks/memory, or map Windows IDs to
Linux IDs. Linux selection is ONLY the recorded Popen PID/start/prefix/argv.
"""
import base64
import json
import math
import os
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


class LinuxIdentityError(ValueError):
    """Rejected observation only; never a successful/incomplete owner record."""
    def __init__(self,reason,observation):
        super().__init__(reason)
        self.observation=observation


def linux_identity(proc,pid,start,prefix,argv,*,deadline=None,now=time.monotonic):
    # FD-relative reads cannot retarget a reused numeric PID. This does not
    # freeze exec/title changes, and scheduling/syscalls are not hard real time.
    begun=now();valid_clock=type(begun) in (int,float) and math.isfinite(begun)
    end=begun+1.0 if valid_clock else 0
    evidence=dict(owned_popen_pid=pid,expected_start_ticks=start,started_monotonic=begun if valid_clock else None,
        maximum_seconds=1.0,limits=dict(stat_bytes=4096,cmdline_bytes=16384,environ_bytes=65536),
        fields={},read_stages=[],prefix_match=dict(matched=None),cleanup_errors=[])
    root=task=None;matched=False;after_attempted=False;failure=None;result=None
    def metadata(error):return dict(error_type=type(error).__name__,errno=getattr(error,'errno',None))
    def remaining():
        current=now()
        if type(current) not in (int,float) or not math.isfinite(current):raise ValueError('Invalid Linux identity clock')
        if current>=end:raise ValueError('Linux identity observation deadline')
    def field(name,cap,label,raw=True):
        evidence['read_stages'].append(label)
        item=dict(cap_bytes=cap,bytes_read=0,complete=False,limit_reached=False)
        evidence['fields'][label]=item;fd=None;data=bytearray()
        try:
            remaining()
            fd=os.open(name,os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=task)
            remaining()
            while len(data)<cap:
                remaining();chunk=os.read(fd,min(4096,cap-len(data)))
                if not chunk:item['complete']=True;break
                data.extend(chunk)
            remaining()
            # Never read cap+1 bytes to discover overflow. A full cap with
            # unproven EOF is conservatively incomplete, including exact-cap.
            if not item['complete']:
                item['limit_reached']=True
                raise ValueError('Oversized or EOF-unproven proc field: '+name)
            return bytes(data)
        except Exception as error:
            item['error']=metadata(error);raise
        finally:
            item['bytes_read']=len(data)
            item['truncation_possible']=not item['complete']
            if raw:item.update(encoding='base64',base64=base64.b64encode(data).decode('ascii'))
            if fd is not None:
                try:os.close(fd)
                except OSError as error:evidence['cleanup_errors'].append(dict(stage=label,**metadata(error)))
    def postcheck():
        nonlocal after_attempted
        after_attempted=True
        after=stat_record(field('stat',4096,'stat_after'),pid);evidence['after']=after
        if after['start_ticks']!=start:raise ValueError('Linux PID reused after observation')
        remaining();current=os.stat(str(pid),dir_fd=root,follow_symlinks=False)
        pinned=os.fstat(task)
        evidence['directory_after']=dict(device=current.st_dev,inode=current.st_ino)
        if ((current.st_dev,current.st_ino)!=(pinned.st_dev,pinned.st_ino)
                or evidence['directory_before']!=dict(device=pinned.st_dev,inode=pinned.st_ino)):
            raise ValueError('Linux PID directory changed during observation')
        remaining()
    try:
        if not integer(pid,2**31-1) or not integer(start):raise ValueError('No recorded Popen Linux identity; no scan/fallback')
        if not valid_clock:raise ValueError('Invalid Linux identity clock')
        if deadline is not None:
            if type(deadline) not in (int,float) or not math.isfinite(deadline):raise ValueError('Invalid Linux identity deadline')
            end=min(end,deadline)
        evidence['deadline_monotonic']=end
        remaining();root=os.open(proc,os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC|os.O_NOFOLLOW)
        remaining();task=os.open(str(pid),os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC|os.O_NOFOLLOW,dir_fd=root)
        s=os.fstat(task);evidence['directory_before']=dict(device=s.st_dev,inode=s.st_ino)
        identity=stat_record(field('stat',4096,'stat_before'),pid);evidence['before']=identity
        if identity['start_ticks']!=start:raise ValueError('Linux PID reused')
        matched=True
        env=field('environ',65536,'environ',raw=False).split(b'\0')
        prefixes=[v for v in env if v.startswith(b'WINEPREFIX=')]
        prefix_ok=prefixes==[('WINEPREFIX='+str(prefix)).encode()]
        evidence['prefix_match']=dict(matched=prefix_ok)
        del env,prefixes
        if not prefix_ok:raise ValueError('Linux prefix mismatch')
        raw=field('cmdline',16384,'cmdline')
        # The accepted forms/normalization are intentionally unchanged.
        args=raw.rstrip(b'\0').decode().split('\0')
        if args:args[-1]=args[-1].rstrip(' ')
        if args==argv or args==argv[1:]:style='argv_array'
        elif len(args)==1 and args[0]==' '.join(argv[1:]):style='wine_flat_padded'
        else:raise ValueError('Linux image/app URL/profile/argv mismatch')
        if any(v.startswith('--type=') for v in argv):raise ValueError('Child process cannot be selected')
        result={'linux_pid':pid,'start_ticks':start,'prefix':str(prefix),'argv_style':style,'observed_cmdline':args}
    except Exception as error:failure=error
    finally:
        # Do not read another task after initial start mismatch. Revalidation
        # after any other failure is bounded and cannot replace its reason.
        if matched and not after_attempted:
            try:postcheck()
            except Exception as error:
                evidence['postcheck_error']=metadata(error)
                if failure is None:failure=error
        for label,fd in (('task',task),('root',root)):
            if fd is not None:
                try:os.close(fd)
                except OSError as error:evidence['cleanup_errors'].append(dict(stage=label,**metadata(error)))
        if evidence['cleanup_errors'] and failure is None:failure=ValueError('Linux identity FD cleanup failed')
        try:
            finished=now();evidence['finished_monotonic']=finished if math.isfinite(finished) else None
            evidence['elapsed_seconds']=finished-begun if math.isfinite(finished-begun) else None
            if (not math.isfinite(finished) or finished<begun or finished>end) and failure is None:
                failure=ValueError('Linux identity observation deadline')
        except Exception as error:
            evidence['clock_error']=metadata(error)
            if failure is None:failure=error
    if failure is not None:
        reason=(str(failure) if isinstance(failure,ValueError) else 'Linux identity read/query failed: '+type(failure).__name__)
        evidence.update(rejection_reason=reason,rejection=metadata(failure))
        if 'environ' in evidence['fields'] and 'error' in evidence['fields']['environ']:
            evidence['prefix_match']['error']=evidence['fields']['environ']['error']
        raise LinuxIdentityError(reason,evidence) from failure
    return result


def linux_record(pid,sample,prefix,argv,deadline,*,proc=Path('/proc'),now=time.monotonic):
    if not integer(pid,2**31-1) or not isinstance(sample,dict) or not integer(sample.get('start_ticks')):
        raise ValueError('No recorded Popen Linux identity; no scan/fallback')
    def remaining():
        if now()>=deadline: raise TimeoutError('Owned observation deadline')
    remaining();before=linux_identity(proc,pid,sample['start_ticks'],prefix,argv,deadline=deadline,now=now)
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
    remaining();after=linux_identity(proc,pid,sample['start_ticks'],prefix,argv,deadline=deadline,now=now);remaining()
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
