#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Future maintenance stdin probe ONLY: fixed read-only commands, no Wine/boot.

No arguments or file deployment. Output is evidence, not host acceptance.
"""
import json
import os
from pathlib import Path
import signal
import subprocess
import time

BUILD='/var/tmp/wex-g3-repair-20261018/clean-source/build/wow64'
PREFIX='/home/debian/wex-g4/edge-install-r1-v2-20261022/prefix'
CORE=['wine','server/wineserver','dlls/win32u/win32u.so','dlls/winex11.drv/winex11.so','dlls/winewayland.drv/winewayland.so']
CORE += ['dlls/'+d+'/'+a+'-windows/'+d+'.dll' for d in ('d3d11','dxgi','dcomp','wined3d','win32u') for a in ('x86_64','i386')]
BINARIES=[BUILD+'/'+n for n in CORE]+['/home/debian/wex-g4/labwc',PREFIX+'/drive_c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe',
    BUILD+'/programs/winedbg/x86_64-windows/winedbg.exe',BUILD+'/dlls/dbghelp/x86_64-windows/dbghelp.dll']
COMMANDS=[
    ('lsblk',['lsblk','--json','-b','-o','NAME,PATH,TYPE,SIZE,FSTYPE,UUID,PARTUUID,START,LOG-SEC']),
    ('root',['findmnt','--json','--target','/','--output','SOURCE,FSTYPE,UUID,TARGET']),
    ('partition',['sudo','-n','sfdisk','--json','/dev/vda']),
    ('partition_dump',['sudo','-n','sfdisk','--dump','/dev/vda']),
    ('filesystem',['sudo','-n','dumpe2fs','-h','/dev/vda1']),
    ('df',['df','-B1','--output=source,fstype,size,used,avail,target','/']),
    ('cloud_init',['cloud-init','status','--long','--format','json']),
    ('services',['systemctl','is-system-running']),
    ('binaries',['sha256sum','--',*BINARIES])]


def collect(*,now=time.monotonic):
    start=now();deadline=start+120;r={'classification':'CAPACITY MAINTENANCE ONLY','records':[],'maximum_seconds':120}
    for label,argv in COMMANDS:
        row={'label':label,'argv':argv,'maximum_seconds':min(15,deadline-now()),'started_monotonic':now()};r['records'].append(row)
        child=None;end=now()+row['maximum_seconds'];out=err=b''
        try:
            if row['maximum_seconds']<=1:raise TimeoutError('No operation/reap budget')
            # These fixed metadata outputs are small. communicate is capped in
            # time; oversized evidence is rejected, never silently truncated.
            child=subprocess.Popen(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True,env={**os.environ,'LC_ALL':'C'})
            out,err=child.communicate(timeout=max(0,end-now()-1))
            row.update(stdout=out.decode(errors='replace'),stderr=err.decode(errors='replace'),exit=child.returncode)
            if len(out)+len(err)>1024**2:raise ValueError('Read-only probe output quota')
            if child.returncode:raise RuntimeError('Read-only probe nonzero: '+label)
        except Exception as error:
            if isinstance(error,subprocess.TimeoutExpired):out,err=error.output or b'',error.stderr or b''
            row['error']=repr(error);r['error']=repr(error)
        finally:
            if child is not None:
                try:
                    if child.poll() is None:
                        if os.getpgid(child.pid)!=child.pid:raise ValueError('Owned probe group changed; refusing signal')
                        os.killpg(child.pid,signal.SIGKILL)
                        remaining=end-now()
                        if remaining<=0:raise TimeoutError('Owned probe kill/reap budget exhausted')
                        out,err=child.communicate(timeout=remaining)
                    row.update(exit=child.returncode,reaped=child.returncode is not None)
                except Exception as cleanup:
                    row['cleanup_error']=repr(cleanup);r.setdefault('cleanup_errors',[]).append(label+': '+repr(cleanup))
                    r.setdefault('error','Owned read-only probe cleanup failed')
                    row['reaped']=child.poll() is not None  # nonblocking final reap only; no borrowed tail
            row.update(stdout=out.decode(errors='replace'),stderr=err.decode(errors='replace'))
            row['finished_monotonic']=now();row['within_deadline']=row['finished_monotonic']<=end
        if r.get('error'):break
    r.update(free_bytes=os.statvfs('/').f_bavail*os.statvfs('/').f_frsize,finished_monotonic=now(),elapsed_seconds=now()-start)
    return r


if __name__=='__main__':
    import sys
    if len(sys.argv)!=1:raise SystemExit('No arguments accepted')
    result=collect();print(json.dumps(result,indent=2));raise SystemExit(1 if result.get('error') else 0)
