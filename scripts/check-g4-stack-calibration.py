#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""One original-child selected-stack TOOL CALIBRATION ONLY; never launches Edge."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time
import uuid

import graphics_resources
from graphics_wexcomp import OwnedWexcomp, verify_native_gl_driver
from g4_edge_owned_threads import linux_identity, stat_record
from g4_stack_commands import identity, commands
from g4_stack_trace import parse


def load(name, file):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(file))
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    return module


install=load('stack_install','check-g4-edge-install.py')
isolation=load('stack_isolation','run-isolated-graphics.py')
LAB,BUILD=install.LAB,install.BUILD
PREFIX=LAB/'edge-install-r1-v2-20261022/prefix'
TOOLS={str(BUILD/'programs/winedbg/x86_64-windows/winedbg.exe'):'71b6beda29d24a525143c074956c125f67e8bd2b81f9e9730108280b8666b7f9',
       str(BUILD/'dlls/dbghelp/x86_64-windows/dbghelp.dll'):'5338dbd72c7b9d7fb6e4d3dcbb799542739308a53c409f46040c6b32fd6e3302'}
EDGE=PREFIX/'drive_c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
EDGE_HASH='bb74db4c753133706f4ab0a483a87aa28a90ddd6eeb305fe001376ba73a40fc3'
CORE=['wine','server/wineserver','dlls/win32u/win32u.so','dlls/winex11.drv/winex11.so','dlls/winewayland.drv/winewayland.so']
CORE += ['dlls/'+dll+'/'+arch+'-windows/'+dll+'.dll' for dll in ('d3d11','dxgi','dcomp','wined3d','win32u') for arch in ('x86_64','i386')]
TOTAL,CLEANUP,LOADER,DEBUGGER=285,55,30,10
# Conservative native 29s wait starts before fflush; reserve scheduling/delivery
# headroom. This is not a direct observation of the native main entry timestamp.
POST_READY=24
LOG_LIMIT=1024**2


def win(path): return 'Z:'+str(path).replace('/','\\')


def fresh_path(path):
    if path.resolve()!=path.absolute(): raise ValueError('Symlink/noncanonical owned path')


def payload():
    record=install.installed_evidence(PREFIX)
    if record['executable']['path']!=str(EDGE) or record['executable']['sha256']!=EDGE_HASH:
        raise ValueError('Pinned installed payload mismatch')
    return record


def guards(output,helpers):
    if (not output.is_absolute() or output.parent!=LAB or not re.fullmatch('stack-calibration-[A-Za-z0-9._-]+',output.name)
        or output.exists() or output.is_symlink()): raise ValueError('Fresh direct stack-calibration-* output required')
    stage=LAB/(output.name+'-stage');fixture=stage/'original/stack-waiter.exe'
    for path in (output,stage,fixture,stage/'calibration-pins.json',PREFIX,EDGE): fresh_path(path)
    if shutil.disk_usage(LAB).free<1024**3: raise ValueError('At least 1GiB free required')
    if graphics_resources.sample(PREFIX) or helpers.matching_servers(BUILD/'server/wineserver',PREFIX):
        raise ValueError('Owned prefix is not idle')
    lo=isolation.verify_private_loopback()
    pins=json.loads((stage/'calibration-pins.json').read_text())
    expected={str(BUILD/name) for name in CORE}|{str(LAB/'labwc'),str(EDGE)}|set(TOOLS)
    if (set(pins)!={'known_binary_sha256','fixture_sha256'} or set(pins['known_binary_sha256'])!=expected
        or len(expected)!=19 or any(not isinstance(v,str) or not re.fullmatch('[0-9a-f]{64}',v) for v in pins['known_binary_sha256'].values())
        or any(pins['known_binary_sha256'][p]!=v for p,v in TOOLS.items())
        or pins['known_binary_sha256'][str(EDGE)]!=EDGE_HASH): raise ValueError('Exact nineteen-pin inventory required')
    if {p:install.digest(Path(p)) for p in expected}!=pins['known_binary_sha256']: raise ValueError('Binary pin mismatch')
    pe=install.pe_metadata(fixture)
    if pe['sha256']!=pins['fixture_sha256']: raise ValueError('Original fixture pin mismatch')
    return fixture,pins,{'private_loopback':lo,'installed':payload(),'original_pe':pe}


def hash_fixture(fixture,pins):
    value=install.digest(fixture)
    if value!=pins['fixture_sha256']: raise ValueError('Original fixture changed')
    return value


def process_start(child):
    return stat_record((Path('/proc')/str(child.pid)/'stat').read_bytes(),child.pid)['start_ticks']


def kill_reap(child,start,deadline,*,now=time.monotonic):
    # Never signal by basename or a PID recovered from a log/command line.
    if child.poll() is None:
        if process_start(child)!=start: raise ValueError('Owned helper PID reused; refusing signal')
        if os.getpgid(child.pid)!=child.pid: raise ValueError('Owned helper process group changed')
        os.killpg(child.pid,signal.SIGKILL)
    remaining=deadline-now()
    if remaining<=0: raise TimeoutError('No owned reap budget')
    return child.wait(timeout=remaining)


def bounded(argv,env,output,label,seconds,records,*,now=time.monotonic,sleep=time.sleep,monitor=lambda:None):
    """Fixed owned operation; quota/kill/reap included, not appended to seconds."""
    start=now();deadline=start+seconds
    row={'argv':argv,'started_monotonic':start,'maximum_seconds':seconds};records.append(row)
    child=None;stamp=None
    out,err=output/(label+'.stdout.log'),output/(label+'.stderr.log')
    try:
        if seconds<=1: raise TimeoutError('Insufficient operation/reap budget')
        monitor()
        with out.open('wb') as a,err.open('wb') as b:
            child=subprocess.Popen(argv,env=env,stdout=a,stderr=b,start_new_session=True)
            stamp=process_start(child);row.update(linux_pid=child.pid,start_ticks=stamp)
            while child.poll() is None:
                monitor()
                if out.stat().st_size+err.stat().st_size>LOG_LIMIT: raise ValueError('Operation log quota exceeded')
                if now()>=deadline-1: raise TimeoutError('Operation timeout; reserved kill/reap tail')
                sleep(.02)
            row['exit']=child.wait(timeout=max(.001,deadline-now()))
        if out.stat().st_size+err.stat().st_size>LOG_LIMIT: raise ValueError('Operation log quota exceeded')
        monitor()
        if now()>deadline: raise TimeoutError('Operation exceeded total bound')
        if row['exit']: raise RuntimeError(label+' nonzero exit')
        texts=out.read_text(errors='replace'),err.read_text(errors='replace')
        if now()>deadline: raise TimeoutError('Operation readback exceeded total bound')
        return texts
    except Exception as error:
        row['error']=repr(error)
        if child is not None:
            try: row['cleanup_exit']=kill_reap(child,stamp,deadline,now=now)
            except Exception as cleanup: row['cleanup_error']=repr(cleanup)
        raise
    finally:
        row['finished_monotonic']=now();row['within_deadline']=now()<=deadline


def readiness(child,path,nonce,deadline,*,now=time.monotonic,sleep=time.sleep,monitor=lambda:None):
    while True:
        monitor()
        if now()>=deadline: raise TimeoutError('Initial loader/readiness cap (30s)')
        with path.open('rb') as stream: raw=stream.read(4097)
        if len(raw)>4096: raise ValueError('Oversized readiness')
        if b'\n' in raw:
            if raw.count(b'\n')!=1 or not raw.endswith(b'\n'): raise ValueError('Additional/trailing readiness output')
            r=identity(raw.decode())
            if r['nonce']!=nonce: raise ValueError('Readiness nonce mismatch')
            if child.poll() is not None: raise ValueError('Original child died before readiness')
            return r,raw
        if child.poll() is not None: raise ValueError('Original child exited without readiness')
        if now()>=deadline: raise TimeoutError('Initial loader/readiness cap (30s)')
        sleep(.02)


def child_logs(output,expected=None):
    with (output/'child.stdout.log').open('rb') as stream: raw=stream.read(4097)
    if len(raw)>4096: raise ValueError('Original child stdout quota')
    if expected is not None and raw!=expected: raise ValueError('Additional/trailing original child output')
    if (output/'child.stderr.log').stat().st_size>LOG_LIMIT: raise ValueError('Original child stderr quota')


def child_exit(child,monitor,*,now=time.monotonic,sleep=time.sleep):
    deadline=now()+2
    while child.poll() is None:
        monitor()
        if now()>=deadline: raise TimeoutError('Original child release/exit timeout')
        sleep(.02)
    monitor()
    return child.wait(timeout=max(.001,deadline-now()))


def post_budget(received,*,now=time.monotonic):
    left=received+POST_READY-now()
    # Whole debugger10 + after verify3 + release3 + child exit2 + 2s margin.
    if left<20: raise TimeoutError('Insufficient separate native post-ready lifetime; no attach')
    return left


def calibrate(fixture,pins,env,output,result,deadline,*,now=time.monotonic):
    nonce=uuid.uuid4().hex;argv=[str(BUILD/'wine'),str(fixture),'--wait',nonce]
    result.update(nonce=nonce,child_argv=argv,initial_loader_cap_seconds=LOADER,native_post_ready_budget_seconds=POST_READY)
    child=None;stamp=None;record=None
    result['operations']=[]
    raw=None
    def monitor(): child_logs(output,raw)
    def alive():
        if child.poll() is not None: raise ValueError('Original child no longer alive')
        return linux_identity(Path('/proc'),child.pid,stamp,PREFIX,argv)
    def verify(label):
        result.setdefault('fixture_hashes',{})[label]=hash_fixture(fixture,pins)
        args=[str(BUILD/'wine'),str(fixture),'--verify',nonce,str(record['pid']),str(record['creation_time']),str(record['main_tid']),str(record['worker_tid'])]
        out,_=bounded(args,env,output,label,3,result['operations'],monitor=monitor)
        verified=identity(out)
        if verified!=record: raise ValueError('Native before/after identity mismatch')
        return verified
    try:
        result.setdefault('fixture_hashes',{})['before_wait']=hash_fixture(fixture,pins)
        with (output/'child.stdout.log').open('wb') as out,(output/'child.stderr.log').open('wb') as err:
            child=subprocess.Popen(argv,env=env,stdout=out,stderr=err,start_new_session=True)
        stamp=process_start(child);result['child_linux_identity']={'pid':child.pid,'start_ticks':stamp}
        record,raw=readiness(child,output/'child.stdout.log',nonce,min(deadline,now()+LOADER),monitor=monitor)
        received=now();result.update(readiness=record,readiness_received_monotonic=received)
        result['linux_before']=alive();result['native_before']=verify('verify-before')
        result['post_ready_seconds_before_attach']=post_budget(received,now=now)
        text=commands(record);command_file=output/'selected-commands.txt';command_file.write_text(text)
        result['command_sha256']=install.digest(command_file)
        debugger=[str(BUILD/'wine'),str(BUILD/'programs/winedbg/x86_64-windows/winedbg.exe'),'--file',win(command_file)]
        debugger_env=dict(env);debugger_env['WINEDLLOVERRIDES']+=';dbghelp=b'
        debugger_env.update(_NT_SYMBOL_PATH='',_NT_ALT_SYMBOL_PATH='')
        result['debugger_loader_policy']={k:debugger_env[k] for k in ('WINE','WINESERVER','WINEPREFIX','WINEDLLOVERRIDES','_NT_SYMBOL_PATH','_NT_ALT_SYMBOL_PATH')}
        result['tool_attestation']='On-disk hashes + builtin loader policy; actual loaded file identity NOT independently traced'
        result['debugger_failure_warning']='Timeout/quota/forced debugger termination may kill the attached OWNED child; kill-on-exit is not disabled; never harmless'
        if {p:install.digest(Path(p)) for p in TOOLS}!=TOOLS: raise ValueError('Tool changed before debugger')
        stdout,stderr=bounded(debugger,debugger_env,output,'debugger',DEBUGGER,result['operations'],monitor=monitor)
        result['debugger_trace']=parse(stdout+'\n'+stderr,record,win(fixture))
        result['native_after']=verify('verify-after');result['linux_after']=alive()
        if result['linux_before']!=result['linux_after']: raise ValueError('Linux original identity changed')
        if now()+5>received+POST_READY: raise TimeoutError('No native release/exit budget')
        result['fixture_hashes']['before_release']=hash_fixture(fixture,pins)
        bounded([str(BUILD/'wine'),str(fixture),'--release',nonce],env,output,'release',3,result['operations'],monitor=monitor)
        result['release_exit']=0;result['child_exit']=child_exit(child,monitor)
        if result['child_exit']!=0: raise ValueError('Original child did not exit zero after release')
        if (output/'child.stdout.log').read_bytes()!=raw: raise ValueError('Additional original child output')
        if (output/'child.stderr.log').stat().st_size>LOG_LIMIT: raise ValueError('Original child log quota')
        result['fixture_hashes']['after']=hash_fixture(fixture,pins)
        if {p:install.digest(Path(p)) for p in TOOLS}!=TOOLS: raise ValueError('Tool changed after debugger')
        result['functional_detach_proven']=True
    finally:
        if child is not None:
            try: result['owned_child_cleanup_exit']=kill_reap(child,stamp,min(deadline,now()+2),now=now)
            except Exception as error: result.setdefault('cleanup_errors',[]).append('child: '+repr(error))


def run(output,helpers,*,now=time.monotonic):
    start=now();fixture,pins,approved=guards(output,helpers)  # no output/process before guards
    if now()-start>=TOTAL-CLEANUP: raise TimeoutError('Preflight consumed whole run budget')
    output.mkdir(exist_ok=False)
    result={'classification':'TOOL CALIBRATION ONLY; not Edge/Chromium/G4 acceptance','passed':False,'approved':approved,
            'known_binary_sha256':pins['known_binary_sha256'],'maximum_seconds':TOTAL,'cleanup_reserve_seconds':CLEANUP}
    comp=None;attempted=False
    try:
        env=install.environment(output);env.update(WINEPREFIX=str(PREFIX),WINEDEBUG='-all,+timestamp,+pid,+tid,+winediag,+process',_NT_SYMBOL_PATH='',_NT_ALT_SYMBOL_PATH='')
        for key in ('HOME','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','TMPDIR'): Path(env[key]).mkdir()
        helpers.configure_gl_driver(env,'virgl')
        comp=OwnedWexcomp(LAB/'labwc',LAB/'wayland',output,env,'gles2')
        result['wexcomp']=comp.start();result['outer_virgl']=comp.verify_gpu_renderer('virgl')
        result['inner_virgl']=verify_native_gl_driver(env,output,'virgl')
        if now()+LOADER+POST_READY>start+TOTAL-CLEANUP: raise TimeoutError('Insufficient bounded calibration budget')
        attempted=True
        calibrate(fixture,pins,env,output,result,start+TOTAL-CLEANUP,now=now)
        comp.verify();result['collection_complete']=True
    except Exception as error: result['error']=repr(error)
    finally:
        if attempted:
            for label,args in (('kill',['-k']),('wait',['-w'])):
                try:
                    code=helpers.run_logged([str(BUILD/'server/wineserver')]+args,env,output,'cleanup-'+label,min(10,max(.1,start+TOTAL-now()))).returncode
                    result['cleanup_'+label+'_exit']=code
                    if code not in ((0,1) if label=='kill' else (0,)): raise RuntimeError('Owned prefix cleanup nonzero')
                except Exception as error: result.setdefault('cleanup_errors',[]).append(label+': '+repr(error))
        for label,callback in (
            ('compositor_stop',lambda:comp.stop() if comp else None),
            ('installed_after',payload),('private_loopback_after',isolation.verify_private_loopback),
            ('remaining_servers',lambda:helpers.matching_servers(BUILD/'server/wineserver',PREFIX)),
            ('remaining_prefix_processes',lambda:graphics_resources.sample(PREFIX))):
            try: result[label]=callback()
            except Exception as error: result.setdefault('cleanup_errors',[]).append(label+': '+repr(error))
        result['elapsed_seconds']=now()-start
        result['passed']=bool(result.get('collection_complete') and result.get('functional_detach_proven') is True
            and result.get('release_exit')==0 and type(result.get('release_exit')) is int
            and result.get('child_exit')==0 and type(result.get('child_exit')) is int
            and result.get('private_loopback_after')==approved['private_loopback']
            and all(row.get('within_deadline') is True and not row.get('error') and not row.get('cleanup_error') for row in result.get('operations',[]))
            and not result.get('error') and not result.get('cleanup_errors')
            and result.get('installed_after')==approved['installed'] and not result.get('remaining_servers',[1])
            and not result.get('remaining_prefix_processes',{'missing':1}) and result['elapsed_seconds']<=TOTAL)
        (output/'results.json').write_text(json.dumps(result,indent=2)+'\n')
    return 0 if result['passed'] else 1


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output-dir',type=Path,required=True)
    args=parser.parse_args();return run(args.output_dir,load('stack_helpers','check-g3-correctness.py'))


if __name__=='__main__': raise SystemExit(main())
