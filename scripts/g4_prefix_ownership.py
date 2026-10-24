# SPDX-License-Identifier: GPL-2.0-or-later
"""Original bounded /proc prefix observation, NOT an atomic ownership barrier.

No task signalling/mutation, environment output or integration side effects.
PF_KTHREAD is the flag contract from upstream stable Linux6.12.113 sched.h.
Every disappearance remains UNKNOWN; no speculative race suppression.
"""
import errno
import inspect
import json
import math
import os
import posixpath
import re
import stat
import time

PF_KTHREAD = 0x00200000


class ScanError(Exception):
    """Internal constant reason only; never contains task environment data."""


def parse_stat(raw, expected_pid):
    """Parse only PID/flags/starttime; comm may contain spaces, ')' and newline."""
    if type(expected_pid) is not int or type(raw) is not bytes or not raw or len(raw)>8192 or not raw.endswith(b'\n'):
        raise ScanError('stat_malformed_or_truncated')
    left=raw.find(b' (');right=raw.rfind(b') ')
    if left<1 or right<left+2 or not re.fullmatch(rb'[0-9]+',raw[:left]):
        raise ScanError('stat_malformed_or_truncated')
    pid=int(raw[:left]);fields=raw[right+2:].split()
    # Linux6.12 stat includes fields1..52; only9/22 are interpreted here.
    # A short record ending just after starttime is still truncated, not valid.
    if pid!=expected_pid or not 0<pid<2**31 or len(fields)<50:
        raise ScanError('stat_pid_or_fields')
    if fields[0] not in tuple(bytes([v]) for v in b'RSDZTtWXxKPI'):
        raise ScanError('stat_state')
    if not all(re.fullmatch(rb'[0-9]+',fields[i]) for i in (6,19)):
        raise ScanError('stat_numeric_fields')
    flags,start=int(fields[6]),int(fields[19])
    if not 0<=flags<2**32 or not 0<=start<2**64:
        raise ScanError('stat_numeric_range')
    return dict(pid=pid,start_ticks=start,flags=flags)


def _read_at(directory, name, cap, end, now):
    if now()>=end:raise ScanError('deadline')
    fd=os.open(name,os.O_RDONLY|os.O_CLOEXEC|os.O_NOFOLLOW|os.O_NONBLOCK,dir_fd=directory)
    try:
        if not stat.S_ISREG(os.fstat(fd).st_mode):raise ScanError('nonregular_proc_file')
        data=bytearray()
        while True:
            if now()>=end:raise ScanError('deadline')
            chunk=os.read(fd,min(65536,cap+1-len(data)))
            if not chunk:break
            data.extend(chunk)
            if len(data)>cap:raise ScanError('read_quota')
        if now()>=end:raise ScanError('deadline')
        return bytes(data)
    finally:os.close(fd)


def scan(prefix,nonce,*,proc_root='/proc',maximum_seconds=8,max_processes=4096,
         max_env_bytes=1024**2,max_total_env_bytes=16*1024**2,
         max_details=128,max_output_bytes=65536,now=time.monotonic):
    """UNKNOWN owners=None on ANY incomplete task/identity/error/quota.

    Uses opened root/task FDs and a post-read stat+directory identity check.
    Even stable PID/start does not freeze exec/environment or prevent new tasks.
    Time checks surround bounded nonblocking reads; caller retains its10s outer
    kill/reap bound for kernel scheduling/syscall stalls, not hard real time.
    """
    start=now();valid_start=type(start) in (int,float) and math.isfinite(start)
    if not valid_start:start=0
    end=start;root=task=None;pid=None;before=None;stage='request'
    output_cap=max_output_bytes if type(max_output_bytes) is int and 8192<=max_output_bytes<=65536 else 65536
    result=dict(schema='wex.prefix_ownership',version=1,requested_prefix=None,nonce=None,
        status='unknown',owners=None,busy=None,observations=[],
        counts=dict(numeric_entries=0,scanned=0,kernel_threads=0,user_tasks=0,empty_environ=0),
        environment_bytes_read=0,started_monotonic=start)
    owners=[]
    try:
        if (type(prefix) is not str or not prefix.startswith('/') or prefix.startswith('//') or
                posixpath.normpath(prefix)!=prefix or any(ord(v)<32 or ord(v)==127 for v in prefix) or
                len(prefix.encode('utf-8','strict'))>4096 or
                type(nonce) is not str or not re.fullmatch('[0-9a-f]{32}',nonce)):
            raise ScanError('invalid_request')
        result.update(requested_prefix=prefix,nonce=nonce)
        if type(maximum_seconds) not in (int,float) or not math.isfinite(maximum_seconds) or not 0<maximum_seconds<=8:
            raise ScanError('invalid_limits')
        for value,minimum,maximum in ((max_processes,1,4096),(max_env_bytes,1,1024**2),
                (max_total_env_bytes,1,16*1024**2),(max_details,1,128),(max_output_bytes,8192,65536)):
            if type(value) is not int or not minimum<=value<=maximum:raise ScanError('invalid_limits')
        if not valid_start:raise ScanError('invalid_clock')
        end=start+maximum_seconds
        result['limits']=dict(maximum_seconds=maximum_seconds,max_processes=max_processes,
            max_env_bytes=max_env_bytes,max_total_env_bytes=max_total_env_bytes,
            max_details=max_details,max_output_bytes=max_output_bytes)
        token=b'WINEPREFIX='+prefix.encode('utf-8','strict')
        stage='enumerate'
        root=os.open(proc_root,os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC|os.O_NOFOLLOW)
        names=[]
        with os.scandir(root) as entries:
            for entry in entries:
                if now()>=end:raise ScanError('deadline')
                if not re.fullmatch('[0-9]+',entry.name):continue
                if len(entry.name)>10 or not 0<int(entry.name)<2**31 or str(int(entry.name))!=entry.name:raise ScanError('pid_directory_invalid')
                names.append(entry.name)
                if len(names)>max_processes:raise ScanError('process_quota')
        result['counts']['numeric_entries']=len(names)
        for name in sorted(names,key=int):
            pid=int(name);before=None;stage='open_task'
            if now()>=end:raise ScanError('deadline')
            task=os.open(name,os.O_RDONLY|os.O_DIRECTORY|os.O_CLOEXEC|os.O_NOFOLLOW,dir_fd=root)
            pinned=os.fstat(task)
            stage='stat_before';before=parse_stat(_read_at(task,'stat',8192,end,now),pid)
            kernel=bool(before['flags']&PF_KTHREAD)
            matched=False;empty=False
            if not kernel:
                stage='environ'
                remaining=max_total_env_bytes-result['environment_bytes_read']
                if remaining<=0:raise ScanError('aggregate_environment_quota')
                data=_read_at(task,'environ',min(max_env_bytes,remaining),end,now)
                result['environment_bytes_read']+=len(data)
                if data and not data.endswith(b'\0'):raise ScanError('environment_truncated')
                matched=token in data.split(b'\0');empty=not data
                del data
            stage='stat_after';after=parse_stat(_read_at(task,'stat',8192,end,now),pid)
            if (before['pid'],before['start_ticks'],kernel)!=(after['pid'],after['start_ticks'],bool(after['flags']&PF_KTHREAD)):
                raise ScanError('task_identity_changed')
            stage='directory_after'
            current=os.stat(name,dir_fd=root,follow_symlinks=False)
            if not stat.S_ISDIR(current.st_mode) or (current.st_dev,current.st_ino)!=(pinned.st_dev,pinned.st_ino):
                raise ScanError('task_directory_replaced')
            result['counts']['scanned']+=1
            result['counts']['kernel_threads' if kernel else 'user_tasks']+=1
            if empty:result['counts']['empty_environ']+=1
            if kernel or matched:
                if len(result['observations'])>=max_details:raise ScanError('detail_quota')
                result['observations'].append(dict(**before,flags_after=after['flags'],
                    classification='kernel_thread_excluded' if kernel else 'matching_owner'))
            if matched:owners.append(dict(pid=pid,start_ticks=before['start_ticks']))
            os.close(task);task=None
        if now()>=end:raise ScanError('deadline')
        result.update(status='complete',owners=owners,busy=bool(owners))
    except Exception as error:
        # Never serialize error messages/comm/environment, and NEVER continue.
        detail=dict(code=str(error) if isinstance(error,ScanError) else 'process_io_or_internal_error',
                    stage=stage,pid=pid,error_type=type(error).__name__[:64],
                    errno=error.errno if isinstance(error,OSError) else None)
        if before is not None:detail['identity_before']=before
        if task is not None and before is not None:
            try:detail['identity_after']=parse_stat(_read_at(task,'stat',8192,end,now),pid)
            except Exception:detail['identity_after_available']=False
        if root is not None and pid is not None:
            try:os.stat(str(pid),dir_fd=root,follow_symlinks=False);detail['pid_path_present']=True
            except FileNotFoundError:detail['pid_path_present']=False
            except OSError:detail['pid_path_present']=None
        result.update(status='unknown',owners=None,busy=None,error=detail)
    finally:
        for fd in (task,root):
            if fd is not None:
                try:os.close(fd)
                except OSError:
                    result.update(status='unknown',owners=None,busy=None,cleanup_error='directory_fd_close_failed')
        finished=now()
        result['finished_monotonic']=finished if math.isfinite(finished) else None
        result['elapsed_seconds']=finished-start if math.isfinite(finished-start) else None
        if not math.isfinite(finished) or finished<start or finished>end:
            result.update(status='unknown',owners=None,busy=None)
            result.setdefault('error',dict(code='deadline_or_invalid_clock'))
    if len(json.dumps(result,separators=(',',':'),allow_nan=False,ensure_ascii=False).encode())+1>output_cap:
        result.update(status='unknown',owners=None,busy=None,observations=[],output_quota_exceeded=True)
        result.setdefault('error',dict(code='output_quota'))
    return result


def probe_main(argv):
    if len(argv)!=3:
        result=scan(None,None);result['error']=dict(code='argument_count')
    else:result=scan(argv[1],argv[2])
    print(json.dumps(result,separators=(',',':'),allow_nan=False,ensure_ascii=False))
    return 0 if result['status']=='complete' else 1


# Build a self-contained -c source from the SAME original implementation tested
# above. Remote needs stdlib only, not this module or any staged guest file.
PROBE = ('import errno,json,math,os,posixpath,re,stat,time,sys\n'+f'PF_KTHREAD={PF_KTHREAD}\n'+
         '\n'.join(inspect.getsource(value) for value in (ScanError,parse_stat,_read_at,scan,probe_main))+
         '\nraise SystemExit(probe_main(sys.argv))\n')
