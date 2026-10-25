# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded original API probe, intermediate cleanup distinct from browser cleanup."""
import json
import secrets
import subprocess
import time

def decode(raw,nonce,code):
    def pairs(items):
        value={}
        for k,v in items:
            if k in value:raise ValueError('Duplicate API probe key')
            value[k]=v
        return value
    if type(raw) is not bytes or len(raw)>4096:raise ValueError('API probe stdout quota/type')
    v=json.loads(raw,object_pairs_hook=pairs,parse_constant=lambda _:(_ for _ in ()).throw(ValueError('Nonfinite probe value')))
    keys={'v','nonce','pid','module_path','module_path_normal','export_found','checks','failures','passed'}
    if (type(v) is not dict or set(v)!=keys or type(code) is not int or code!=0 or type(v['v']) is not int or v['v']!=1
        or v['nonce']!=nonce or type(v['pid']) is not int or not 0<v['pid']<2**32
        or type(v['module_path']) is not str or v['module_path'].casefold()!=r'c:\windows\system32\userenv.dll'
        or v['module_path_normal'] is not True or v['export_found'] is not True or v['passed'] is not True
        or type(v['checks']) is not int or v['checks']!=45 or type(v['failures']) is not int or v['failures']!=0):
        raise ValueError('Failed/malformed normal-module API probe')
    return v

def phase(output,env,result,probe,build,prefix,helpers,monitor,sample,deadline,*,popen=subprocess.Popen,now=time.monotonic,sleep=time.sleep):
    start=now();child=None;primary=None
    result['probe_started']=False;result['probe_reaped']=False
    if deadline<start+45:raise TimeoutError('Need full45s API probe/reap/intermediate cleanup allowance')
    end=start+45
    nonce=secrets.token_hex(16)
    result['api_probe']={'nonce':nonce,'started_monotonic':start,'maximum_seconds':45}
    meta=result['api_probe']
    def left(cap):
        remaining=end-now()
        if remaining<=0:raise TimeoutError('API probe phase deadline')
        return min(cap,remaining)
    def quota():
        for label,cap in (('stdout',4096),('stderr',1024**2)):
            p=output/('api-probe.'+label+'.log')
            if p.is_symlink() or (p.exists() and p.stat().st_size>cap):raise ValueError('API probe '+label+' quota/path')
    try:
        argv=[str(build/'wine'),str(probe),'--nonce',nonce];meta['argv']=argv
        with (output/'api-probe.stdout.log').open('wb') as out,(output/'api-probe.stderr.log').open('wb') as err:
            child=popen(argv,env=env,stdout=out,stderr=err,start_new_session=True);result['probe_started']=True
        run_end=end-20 # startup/run25 + reserved reap5 + intermediate cleanup15
        while child.poll() is None:
            quota();monitor()
            if now()>=run_end:raise TimeoutError('API probe25s timeout')
            sleep(.02)
        meta['linux_exit']=child.wait(timeout=left(1));result['probe_reaped']=True
        quota();meta['result']=decode((output/'api-probe.stdout.log').read_bytes(),nonce,meta['linux_exit'])
    except Exception as error:primary=error;meta['error']=repr(error);raise
    finally:
        if child is not None and not result['probe_reaped']:
            try:
                if child.poll() is None:child.kill();meta['observer_killed']=True
                meta['reap_exit']=child.wait(timeout=left(5));result['probe_reaped']=True
            except Exception as error:meta['reap_error']=repr(error)
        meta['cleanup_started_monotonic']=now() # NEVER browser cleanup_started
        try:
            if result['probe_started'] and not result['probe_reaped']:raise RuntimeError('Probe observer not reaped; no prefix teardown')
            meta['cleanup_kill']=helpers.run_logged([str(build/'server/wineserver'),'-k'],env,output,'probe-cleanup-kill',left(5)).returncode
            meta['cleanup_wait']=helpers.run_logged([str(build/'server/wineserver'),'-w'],env,output,'probe-cleanup-wait',left(8)).returncode
            meta['remaining_servers']=helpers.matching_servers(build/'server/wineserver',prefix);meta['remaining_processes']=sample(prefix)
            if meta['cleanup_kill'] not in (0,1) or meta['cleanup_wait']!=0 or meta['remaining_servers'] or meta['remaining_processes']:raise RuntimeError('Probe prefix not idle after intermediate cleanup')
            quota();monitor();left(1);meta['finished_monotonic']=now()
        except Exception as error:
            meta['cleanup_error']=repr(error)
            if primary is None:raise
    meta['passed']=True
