#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""ONE original-child held-handle TOOL calibration; never Chromium/debugger."""
import argparse
import importlib.util
import json
from pathlib import Path
import secrets
import shutil
import subprocess
import time

import graphics_resources
from graphics_wexcomp import OwnedWexcomp,verify_native_gl_driver
from g4_edge_owned_threads import linux_identity,stat_record

spec=importlib.util.spec_from_file_location('native_exit_install',Path(__file__).with_name('check-g4-edge-install.py'))
install=importlib.util.module_from_spec(spec);spec.loader.exec_module(install)
spec=importlib.util.spec_from_file_location('native_exit_isolation',Path(__file__).with_name('run-isolated-graphics.py'))
isolation=importlib.util.module_from_spec(spec);spec.loader.exec_module(isolation)
LAB,BUILD=install.LAB,install.BUILD
NAME='native-exit-calibration-r1-20261023'
PREFIX=LAB/'edge-install-r1-v2-20261022/prefix'
EDGE=PREFIX/'drive_c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
EDGE_SHA='bb74db4c753133706f4ab0a483a87aa28a90ddd6eeb305fe001376ba73a40fc3'
PES={'native-exit.exe':'366c69286a7eb6469e4bfd9921643202b746fc987de46e9f9ded2bf6713b8d60',
     'exit-child.exe':'6087e3ef18afca2381d7d675e3c7b3ef19141be34d5427c8293684fdfab6c5f9'}
CORE=['wine','server/wineserver','dlls/win32u/win32u.so','dlls/winex11.drv/winex11.so','dlls/winewayland.drv/winewayland.so']
CORE += ['dlls/'+dll+'/'+arch+'-windows/'+dll+'.dll' for dll in ('d3d11','dxgi','dcomp','wined3d','win32u') for arch in ('x86_64','i386')]
CORE += ['programs/winedbg/x86_64-windows/winedbg.exe','dlls/dbghelp/x86_64-windows/dbghelp.dll'] # hash ONLY, never invoke
TOTAL,CLEANUP,OPERATION,LOADER,REAP=285,55,42,30,5
EVENT_LIMIT,LOG_LIMIT=16384,1024**2


def win(path):return 'Z:'+str(path).replace('/','\\')
def save(path,value):path.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n')
def uint(value,bits=64):return type(value) is int and 0<=value<2**bits


def command(output,nonce):
    stage=LAB/(NAME+'-stage')
    return [str(BUILD/'wine'),str(stage/'original/native-exit.exe'),'--calibration',
        '--image',win(stage/'original/exit-child.exe'),'--events',win(output/'native-exit.jsonl'),'--nonce',nonce]


def decode(line):
    def pairs(items):
        result={}
        for k,v in items:
            if k in result:raise ValueError('Duplicate native event key')
            result[k]=v
        return result
    return json.loads(line,object_pairs_hook=pairs,parse_constant=lambda _:(_ for _ in ()).throw(ValueError('Nonfinite native event')))


def binding(value,nonce,image):
    if not isinstance(nonce,str) or len(nonce)!=32 or any(c not in '0123456789abcdef' for c in nonce):
        raise ValueError('Exact lowercase32hex calibration nonce required')
    keys={'v','event','mode','nonce','helper_pid','child_pid','creation_time','image','command_line',
          'creation_flags','inherit_handles','environment','manifest_sha256','manifest_verified_by_native','tick_ms'}
    expected=' '.join('"'+v+'"' for v in (image,'--exit-status','3221225599','--nonce',nonce))
    if (not isinstance(value,dict) or set(value)!=keys or type(value.get('v')) is not int or value['v']!=1
            or value.get('event')!='bound' or value.get('mode')!='calibration' or value.get('nonce')!=nonce
            or not uint(value.get('helper_pid'),32) or not value['helper_pid']
            or not uint(value.get('child_pid'),32) or not value['child_pid'] or value['child_pid']==value['helper_pid']
            or not uint(value.get('creation_time')) or not value['creation_time']
            or not isinstance(value.get('image'),str) or value['image'].casefold()!=image.casefold()
            or value.get('command_line')!=expected or type(value.get('creation_flags')) is not int or value['creation_flags']!=0
            or value.get('inherit_handles') is not False or value.get('environment')!='inherited_NULL'
            or value.get('manifest_sha256') is not None or value.get('manifest_verified_by_native') is not False
            or not uint(value.get('tick_ms'))):raise ValueError('Missing/foreign/malformed native binding')
    return value


def parse(raw,nonce,image):
    if (not isinstance(raw,bytes) or len(raw)>EVENT_LIMIT or raw.count(b'\n')!=2 or not raw.endswith(b'\n')):
        raise ValueError('Exactly two complete bounded native event lines required')
    lines=raw.decode('utf-8').splitlines()
    if len(lines)!=2:raise ValueError('Extra/trailing native lines')
    before=binding(decode(lines[0]),nonce,image);end=decode(lines[1])
    keys={'v','event','nonce','child_pid','creation_time','wait_result','exit_code','exit_hex','tick_ms'}
    if (not isinstance(end,dict) or set(end)!=keys or type(end.get('v')) is not int or end['v']!=1
            or end.get('event')!='exit' or end.get('nonce')!=nonce
            or type(end.get('child_pid')) is not int or end['child_pid']!=before['child_pid']
            or type(end.get('creation_time')) is not int or end['creation_time']!=before['creation_time']
            or type(end.get('wait_result')) is not int or end['wait_result']!=0
            or not uint(end.get('tick_ms')) or not 0<=end['tick_ms']-before['tick_ms']<=7000):
        raise ValueError('Native signaled exit identity/time mismatch')
    if (not uint(end.get('exit_code'),32) or end['exit_code']!=0xc000007f or end.get('exit_hex')!='0xc000007f'):
        raise ValueError('Original calibration status mismatch; signaled259 is not liveness')
    return {'bound':before,'exit':end,'native_child_status':end['exit_code'],'native_child_hex':end['exit_hex'],
            'manifest_marker':'Calibration null; native helper does NOT attest runtime bytes',
            'classification':'Held-handle original-child calibration ONLY; not Chromium or graphics'}


def guards(output,helpers):
    stage=LAB/(NAME+'-stage')
    if output!=LAB/NAME or output.exists() or output.is_symlink():raise ValueError('Exact fresh calibration output required')
    for p in (output,stage,PREFIX,EDGE,stage/'native-exit-pins.json',*[stage/'original'/n for n in PES]):
        if p.resolve()!=p.absolute():raise ValueError('Symlink/noncanonical owned path')
    if shutil.disk_usage(LAB).free<1024**3:raise ValueError('At least1GiB free before output/operation')
    if graphics_resources.sample(PREFIX) or helpers.matching_servers(BUILD/'server/wineserver',PREFIX):raise ValueError('Retained prefix not idle')
    lo=isolation.verify_private_loopback()
    if ((PREFIX/'dosdevices/c:').resolve(strict=True)!=(PREFIX/'drive_c').resolve(strict=True)
            or (PREFIX/'dosdevices/z:').resolve(strict=True)!=Path('/')):raise ValueError('Retained prefix drive mappings changed')
    installed=install.installed_evidence(PREFIX)
    if installed['executable']['path']!=str(EDGE) or installed['executable']['sha256']!=EDGE_SHA:raise ValueError('Installed prerequisite changed')
    pins=json.loads((stage/'native-exit-pins.json').read_text())
    expected={str(BUILD/n) for n in CORE}|{str(EDGE),str(LAB/'labwc')}
    if set(pins)!={'known_binary_sha256','original_pe_sha256'} or pins['original_pe_sha256']!=PES or len(expected)!=19 or set(pins['known_binary_sha256'])!=expected:
        raise ValueError('Exact19 environment +2 original PE pins required')
    if {p:install.digest(Path(p)) for p in expected}!=pins['known_binary_sha256']:raise ValueError('Environment binary hash mismatch')
    pe={n:install.pe_metadata(stage/'original'/n) for n in PES}
    if {n:v['sha256'] for n,v in pe.items()}!=PES:raise ValueError('Original PE hash mismatch')
    return pins,{'private_loopback':lo,'installed':installed,'original_pe':pe}


def logs(output):
    for name,limit in (('helper.stdout.log',LOG_LIMIT),('helper.stderr.log',LOG_LIMIT),('native-exit.jsonl',EVENT_LIMIT)):
        p=output/name
        if p.is_symlink() or (p.exists() and p.stat().st_size>limit):raise ValueError('Calibration log quota/path: '+name)


def journal(output):
    p=output/'native-exit.jsonl'
    if not p.exists():return b''
    with p.open('rb') as stream:value=stream.read(EVENT_LIMIT+1)
    if len(value)>EVENT_LIMIT:raise ValueError('Native journal quota')
    return value


def collect(output,env,result,deadline,begin_cleanup,monitor,*,now=time.monotonic,sleep=time.sleep,popen=subprocess.Popen):
    start=now();end=min(deadline,start+OPERATION);child=None;stamp=None
    result.update(helper_started=False,helper_reaped=False)
    result.update(nonce=secrets.token_hex(16),helper_operation_maximum_seconds=OPERATION)
    argv=command(output,result['nonce']);result['helper_argv']=argv
    def remaining():
        left=end-now()
        if left<=0:raise TimeoutError('Owned helper reap deadline exhausted')
        return left
    try:
        if end-start<OPERATION:raise TimeoutError('Insufficient complete helper42s/reap budget')
        with (output/'helper.stdout.log').open('wb') as out,(output/'helper.stderr.log').open('wb') as err:
            child=popen(argv,env=env,stdout=out,stderr=err,start_new_session=True)
            result['helper_started']=True
        stamp=stat_record((Path('/proc')/str(child.pid)/'stat').read_bytes(),child.pid)['start_ticks']
        result['linux_helper_popen']={'pid':child.pid,'start_ticks':stamp,'not_native_child_pid':True}
        while True:
            code=child.poll()
            if code is not None:
                result['linux_helper_exit_observed']={'code':code,'monotonic':now(),'before_cleanup':True}
                result['linux_helper_wait_exit']=child.wait(timeout=min(2,remaining()));result['helper_reaped']=True
                if type(code) is not int or code!=0 or type(result['linux_helper_wait_exit']) is not int or result['linux_helper_wait_exit']!=code:raise ValueError('Linux helper failed/exit mismatch')
                if 'linux_helper_first_live' not in result:raise ValueError('Helper exited before independent Linux identity')
                break
            record=linux_identity(Path('/proc'),child.pid,stamp,PREFIX,argv)
            result.setdefault('linux_helper_first_live',record);result['linux_helper_last_live']=record
            monitor();logs(output)
            raw=journal(output)
            if b'\n' in raw:
                binding(decode(raw.split(b'\n')[0].decode()),result['nonce'],win(LAB/(NAME+'-stage')/'original/exit-child.exe'))
                result.setdefault('binding_received_monotonic',now())
            elif now()-start>=LOADER:raise TimeoutError('Initial native binding30s deadline')
            if now()>=end-REAP:raise TimeoutError('Helper operation timeout; reserved5s kill/reap')
            sleep(.02)
        monitor();logs(output)
        result['native']=parse(journal(output),result['nonce'],win(LAB/(NAME+'-stage')/'original/exit-child.exe'))
        if now()>end:raise TimeoutError('Helper readback exceeded operation bound')
        result['calibration_observation_complete']=True
    finally:
        if child is not None and not result['helper_reaped']:
            try:
                if child.poll() is None:
                    actual=stat_record((Path('/proc')/str(child.pid)/'stat').read_bytes(),child.pid)['start_ticks']
                    if actual!=stamp:raise ValueError('Owned helper PID reused; no signal')
                    begin_cleanup('owned_helper_abort');child.kill() # ONLY exact Popen, not child PID/whole group
                    result['observer_killed']=True
                else:
                    result.setdefault('linux_helper_exit_observed',{'code':child.poll(),'monotonic':now(),'before_cleanup':'cleanup_started' not in result})
                result['linux_helper_reap_exit']=child.wait(timeout=min(REAP,remaining()));result['helper_reaped']=True
            except Exception as error:result.setdefault('cleanup_errors',[]).append('helper reap: '+repr(error))
        result['helper_finished_monotonic']=now()


def run(output,helpers,*,now=time.monotonic,sleep=time.sleep,popen=subprocess.Popen):
    start=now();pins,approved=guards(output,helpers) # no output/process/finally on failed guards
    if now()-start>=TOTAL-CLEANUP:raise TimeoutError('Preflight exhausted total budget')
    output.mkdir();result={'passed':False,'classification':'Original native API/tool calibration ONLY; never browser/G4 acceptance',
        'approved':approved,'helper_started':False,'helper_reaped':False};comp=None;env=None;deadline=start+TOTAL
    def bound(cap,cleanup=False):
        left=deadline-now()-(0 if cleanup else CLEANUP)
        if left<=0:raise TimeoutError('Whole calibration deadline')
        return min(cap,left)
    def cleanup_started(reason):result.setdefault('cleanup_started',{'monotonic':now(),'reason':reason})
    try:
        env=install.environment(output);env.update(WINEPREFIX=str(PREFIX),WINEDEBUG='-all,+timestamp,+pid,+tid,+winediag,+process')
        for key in ('HOME','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','TMPDIR'):Path(env[key]).mkdir()
        result['environment']={k:v for k,v in env.items() if k.startswith(('WINE','WEX_','XDG_')) or k in ('HOME','TMPDIR')}
        helpers.configure_gl_driver(env,'virgl')
        comp=OwnedWexcomp(LAB/'labwc',LAB/'wayland',output,env,'gles2')
        if bound(55)<55:raise TimeoutError('Insufficient compositor bound')
        result['compositor']=comp.start();result['outer']=comp.verify_gpu_renderer('virgl')
        result['inner']=verify_native_gl_driver(env,output,'virgl')
        collect(output,env,result,deadline-CLEANUP,cleanup_started,comp.verify,now=now,sleep=sleep,popen=popen)
    except Exception as error:result['error']=repr(error)
    finally:
        cleanup_started('prefix_teardown')
        if env is not None and (not result.get('helper_started') or result.get('helper_reaped') is True):
            for label,args in (('kill',['-k']),('wait',['-w'])):
                try:result['cleanup_'+label]=helpers.run_logged([str(BUILD/'server/wineserver')]+args,env,output,'cleanup-'+label,bound(10,True)).returncode
                except Exception as error:result.setdefault('cleanup_errors',[]).append(repr(error))
        elif result.get('helper_started') and not result.get('helper_reaped'):result.setdefault('cleanup_errors',[]).append('Prefix teardown skipped: observer not reaped')
        try:
            if comp is not None:comp.stop()
            result['remaining_servers']=helpers.matching_servers(BUILD/'server/wineserver',PREFIX)
            result['remaining_prefix_processes']=graphics_resources.sample(PREFIX)
            result['private_loopback_after']=isolation.verify_private_loopback()
            logs(output)
        except Exception as error:result.setdefault('cleanup_errors',[]).append(repr(error))
        finally:
            result['elapsed_seconds']=now()-start
            result['passed']=(result.get('calibration_observation_complete') is True and result.get('helper_reaped') is True
                and result.get('native',{}).get('native_child_status')==0xc000007f
                and result.get('native',{}).get('native_child_hex')=='0xc000007f'
                and not result.get('error') and not result.get('cleanup_errors') and not result.get('observer_killed')
                and result.get('linux_helper_exit_observed',{}).get('before_cleanup') is True
                and result.get('linux_helper_exit_observed',{}).get('code')==0
                and result.get('linux_helper_wait_exit')==0 and result.get('cleanup_kill') in (0,1) and result.get('cleanup_wait')==0
                and result.get('remaining_servers')==[] and result.get('remaining_prefix_processes')=={}
                and result.get('private_loopback_after')==approved['private_loopback'] and result['elapsed_seconds']<=TOTAL)
            save(output/'results.json',result)
    return 0 if result['passed'] else 1


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output-dir',type=Path,required=True)
    output=parser.parse_args().output_dir
    spec=importlib.util.spec_from_file_location('native_exit_helpers',Path(__file__).with_name('check-g3-correctness.py'))
    helpers=importlib.util.module_from_spec(spec);spec.loader.exec_module(helpers)
    return run(output,helpers)


if __name__=='__main__':raise SystemExit(main())
