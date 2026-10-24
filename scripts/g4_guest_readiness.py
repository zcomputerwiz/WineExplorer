# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded read-only SSH readiness evidence, NOT VM lifecycle or acceptance.

Linux/WSL caller supplies its pinned SSH prefix. No CLI, VM start/stop, guest
writes, service repair or launcher budget extension. Caller still owns650/435
guards and90-second shutdown reserve.
"""
import base64
import json
import math
import os
import re
import secrets
import selectors
import shlex
import signal
import subprocess
import time
import uuid


# Fixed guest stdlib probe; each query has a1.5s execution +0.5s reap bound.
# Pipe quotas enforced WHILE reading, not just after an error storm finishes.
PROBE = r'''import base64,json,os,pwd,selectors,subprocess,sys,time
from pathlib import Path
def query(argv):
 p=subprocess.Popen(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,env={**os.environ,'LC_ALL':'C'})
 out=[bytearray(),bytearray()];s=selectors.DefaultSelector();end=time.monotonic()+1.5
 try:
  for i,f in enumerate((p.stdout,p.stderr)):os.set_blocking(f.fileno(),False);s.register(f,selectors.EVENT_READ,i)
  while s.get_map():
   if time.monotonic()>=end:raise TimeoutError('guest query timeout')
   for k,_ in s.select(min(.05,max(0,end-time.monotonic()))):
    b=os.read(k.fileobj.fileno(),4096)
    if not b:s.unregister(k.fileobj);continue
    out[k.data].extend(b)
    if len(out[k.data])>4096:raise ValueError('guest query quota')
  p.wait(timeout=max(.001,end-time.monotonic()))
  return dict(returncode=p.returncode,stdout=base64.b64encode(out[0]).decode(),stderr=base64.b64encode(out[1]).decode())
 finally:
  s.close()
  try:
   if p.poll() is None:p.kill()
   p.wait(timeout=.5)
  finally:p.stdout.close();p.stderr.close()
queries={
 'cloud_init':query(['cloud-init','status','--long','--format','json']),
 'systemd':query(['systemctl','is-system-running']),
 'power_handler':query(['systemctl','show','systemd-logind.service','--property=LoadState,ActiveState,SubState'])}
print(json.dumps(dict(nonce=sys.argv[1],uid=os.getuid(),username=pwd.getpwuid(os.getuid()).pw_name,
 boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip(),queries=queries)))
'''


def _json(raw):
    def pairs(items):
        obj = {}
        for key, value in items:
            if key in obj:
                raise ValueError('Duplicate JSON key: '+key)
            obj[key] = value
        return obj
    return json.loads(raw, object_pairs_hook=pairs,
                      parse_constant=lambda value: (_ for _ in ()).throw(ValueError('Nonfinite JSON')))


def _keys(value, expected):
    if type(value) is not dict or set(value) != set(expected):
        raise ValueError('Unexpected record schema')


def decode_response(raw, nonce):
    """Return normalized identity/state; raw queries remain caller evidence."""
    if type(raw) is not bytes or len(raw) > 16384:
        raise ValueError('Response byte type/size')
    value = _json(raw.decode('utf-8', 'strict'))
    _keys(value, ('nonce','uid','username','boot_id','queries'))
    if (type(value['nonce']) is not str or value['nonce'] != nonce or
            type(value['uid']) is not int or value['uid'] != 1000 or
            type(value['username']) is not str or value['username'] != 'debian'):
        raise ValueError('Nonce or authenticated guest identity mismatch')
    boot = value['boot_id']
    if type(boot) is not str or str(uuid.UUID(boot)) != boot or uuid.UUID(boot).int == 0:
        raise ValueError('Invalid boot UUID')
    _keys(value['queries'], ('cloud_init','systemd','power_handler'))
    decoded = {}
    for name, row in value['queries'].items():
        _keys(row, ('returncode','stdout','stderr'))
        if type(row['returncode']) is not int or not -255 <= row['returncode'] <= 255:
            raise ValueError('Query exit schema')
        decoded[name] = {}
        for stream in ('stdout','stderr'):
            if type(row[stream]) is not str or len(row[stream]) > 5464:
                raise ValueError('Query output schema/quota')
            data = base64.b64decode(row[stream], validate=True)
            if len(data) > 4096:
                raise ValueError('Query output quota')
            decoded[name][stream] = data
    cloud = _json(decoded['cloud_init']['stdout'].decode('utf-8', 'strict'))
    if (type(cloud) is not dict or type(cloud.get('status')) is not str or
            type(cloud.get('errors')) is not list or
            type(cloud.get('recoverable_errors')) is not dict):
        raise ValueError('Cloud-init status schema')
    state = decoded['systemd']['stdout'].decode('utf-8', 'strict').strip()
    if (cloud['errors'] or cloud['recoverable_errors'] or
            cloud['status'] not in ('done','running','not started') or
            state not in ('running','starting','initializing')):
        raise ValueError('Cloud-init error or unsupported/degraded systemd state')
    ready = (cloud['status'] == 'done' and state == 'running' and
             value['queries']['cloud_init']['returncode'] == 0 and
             value['queries']['systemd']['returncode'] == 0)
    return dict(boot_id=boot, ready=ready, cloud_init=cloud['status'], systemd=state,
                power_handler=value['queries']['power_handler'])


def run_attempt(argv, maximum_seconds, *, now=time.monotonic):
    """Owned process group; <=8s I/O plus reserved2s kill/reap within10s."""
    if not math.isfinite(maximum_seconds) or not 2 < maximum_seconds <= 10:
        raise ValueError('Attempt needs >2 and <=10 seconds including reap')
    start = now(); end = start+maximum_seconds
    row = dict(argv=list(argv),started_monotonic=start,maximum_seconds=maximum_seconds)
    child = None; buffers = [bytearray(),bytearray()]; selector = selectors.DefaultSelector()
    try:
        child = subprocess.Popen(argv,stdout=subprocess.PIPE,stderr=subprocess.PIPE,start_new_session=True)
        row['pid'] = child.pid
        for i, stream in enumerate((child.stdout,child.stderr)):
            os.set_blocking(stream.fileno(),False);selector.register(stream,selectors.EVENT_READ,i)
        while selector.get_map():
            if now() >= end-2:
                raise TimeoutError('Readiness attempt execution deadline')
            for key,_ in selector.select(min(.05,max(0,end-2-now()))):
                data = os.read(key.fileobj.fileno(),4096)
                if not data:selector.unregister(key.fileobj);continue
                cap = (16384,8192)[key.data]
                buffers[key.data].extend(data[:max(0,cap+1-len(buffers[key.data]))])
                if len(buffers[key.data]) > cap:
                    raise ValueError('Readiness output quota')
        child.wait(timeout=max(.001,end-2-now()))
    except Exception as error:
        row['error'] = repr(error)
    finally:
        selector.close()
        if child is not None:
            try:
                if child.poll() is None:
                    os.killpg(child.pid,signal.SIGKILL)
                remaining=end-now()
                if remaining <= 0:raise TimeoutError('Readiness reap budget exhausted')
                child.wait(timeout=remaining)
                row['reaped'] = True
            except Exception as cleanup:
                row['cleanup_error'] = repr(cleanup)
                row['reaped'] = False  # failed reap is never claimed successful
            finally:
                for stream in (child.stdout,child.stderr):
                    try:stream.close()
                    except Exception as cleanup:
                        row.setdefault('cleanup_error',repr(cleanup))
            row['returncode'] = child.returncode
        for i,name in enumerate(('stdout','stderr')):
            cap=(16384,8192)[i]
            row[name] = dict(base64=base64.b64encode(buffers[i][:cap]).decode(),
                             observed_bytes=len(buffers[i]),truncated=len(buffers[i])>cap)
        row['finished_monotonic']=now();row['elapsed_seconds']=row['finished_monotonic']-start
        row['within_deadline']=row['finished_monotonic']<=end
    return row


def wait_ready(ssh_prefix, *, maximum_seconds=180, interval_seconds=2,
               now=time.monotonic, sleep=time.sleep, attempt=run_attempt, nonce=None):
    """Retain every attempt; callers persist result, never interpret as G4 proof."""
    prefix=list(ssh_prefix)
    # Exact existing pinned connection shape; do not add fallback/auth options.
    if (len(prefix)!=14 or prefix[0]!='ssh' or prefix[-1]!='debian@127.0.0.1' or
            prefix[1]!='-i' or not prefix[2] or prefix[3:5]!=['-o','BatchMode=yes'] or
            prefix[5:7]!=['-o','ConnectTimeout=5'] or
            prefix[7:9]!=['-o','StrictHostKeyChecking=yes'] or prefix[9]!='-o' or
            not prefix[10].startswith('UserKnownHostsFile=') or not prefix[10].split('=',1)[1] or
            prefix[11:13]!=['-p','2235']):
        raise ValueError('Expected pinned SSH prefix')
    for v,lower,upper in ((maximum_seconds,2,180),(interval_seconds,.1,5)):
        if type(v) not in (int,float) or not math.isfinite(v) or not lower < v <= upper:
            raise ValueError('Readiness gate budget/interval')
    nonce=secrets.token_hex(16) if nonce is None else nonce
    if type(nonce) is not str or not re.fullmatch('[0-9a-f]{32}',nonce):raise ValueError('Nonce schema')
    start=now();end=start+maximum_seconds;boot=None;consecutive=0
    result=dict(ready=False,nonce=nonce,attempts=[],started_monotonic=start,maximum_seconds=maximum_seconds)
    while end-now()>2:
        row=attempt(prefix+[shlex.join(['python3','-B','-c',PROBE,nonce])],min(10,end-now()))
        result['attempts'].append(row)
        try:
            if row.get('cleanup_error') or not row.get('reaped') or not row.get('within_deadline'):
                raise ValueError('Attempt cleanup/deadline failure')
            if row.get('error') or row.get('returncode') != 0:
                consecutive=0
                stderr=base64.b64decode(row['stderr']['base64']).lower()
                if any(v in stderr for v in (b'permission denied',b'host key verification failed',b'remote host identification has changed')):
                    raise ValueError('SSH authentication/host-key failure')
            else:
                state=decode_response(base64.b64decode(row['stdout']['base64']),nonce)
                if boot is not None and boot!=state['boot_id']:raise ValueError('Guest boot changed; never adopt')
                boot=state['boot_id'];row['validated']=state
                consecutive=consecutive+1 if state['ready'] else 0
                if consecutive==2:
                    if now()>end:raise TimeoutError('Whole readiness deadline')
                    result.update(ready=True,boot_id=boot);break
        except Exception as error:
            result['error']=repr(error);break
        remaining=end-now()
        if remaining<=2:break
        sleep(min(interval_seconds,remaining))
    result.update(finished_monotonic=now(),elapsed_seconds=now()-start)
    if not result['ready']:result.setdefault('error','Readiness deadline; two valid replies not obtained')
    return result
