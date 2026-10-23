#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""One installed Edge offline app-window smoke; never full-browser/G4 acceptance."""
import argparse
import importlib.util
import json
import math
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import threading
import time

import graphics_resources
from graphics_wexcomp import OwnedWexcomp, verify_native_gl_driver, capture_command


def load(name):
    spec=importlib.util.spec_from_file_location(name,Path(__file__).with_name(name+'.py'))
    module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    return module


install=load('check-g4-edge-install')
LAB=install.LAB
BUILD=install.BUILD
PREFIX=LAB/'edge-install-r1-v2-20261022/prefix'
EXE=PREFIX/'drive_c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe'
EXE_SHA='bb74db4c753133706f4ab0a483a87aa28a90ddd6eeb305fe001376ba73a40fc3'
TOTAL, RESERVE=285,55  # include bounded server/browser/monitor/compositor cleanup tails
LOG_LIMIT=128*1024**2  # soft stop threshold, never truncate diagnostic logs
VLOG_MODULES=('file_url_loader_factory=2,navigation_request=2,render_process_host_impl=2,'
              'child_process_launcher_helper=2,child_process_launcher_helper_win=2')
RECTS={'marker':dict(x=32,y=32,width=48,height=48),
       'webgl':dict(x=32,y=112,width=96,height=96),
       'webgl2':dict(x=160,y=112,width=96,height=96)}
COLORS={'marker':(21,101,192),'background':(232,245,233),'webgl':(255,0,255),'webgl2':(0,255,255)}


def save(path,value):
    path.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n')


def persistence():
    """Read only; retain actual pending registry evidence without interpreting intent."""
    record={'hives':{},'inventory':[]}
    for name in ('system.reg','user.reg'):
        path=PREFIX/name
        if not path.is_file(): raise ValueError('Missing retained registry hive')
        text=path.read_text(errors='replace')
        blocks=re.findall(r'(?ms)^\[([^\n]+)\][^\n]*\n(.*?)(?=^\[|\Z)',text)
        record['hives'][name]={'sha256':install.digest(path),'pending_blocks':[
            {'key':key,'raw_values':values} for key,values in blocks
            if re.search(r'^"PendingFileRenameOperations(?:2)?"=',values,re.M|re.I)]}
    app=EXE.parent
    # Bounded relevant inventory, not a proprietary payload copy/resource parser.
    for relative in ('msedge.exe', install.VERSION+'/msedge.dll', install.VERSION+'/msedge_elf.dll'):
        p=app/relative
        record['inventory'].append({'relative':relative,'exists':p.is_file(),
            **({'bytes':p.stat().st_size,'sha256':install.digest(p)} if p.is_file() else {})})
    return record


def require_payload():
    if ((PREFIX/'dosdevices/c:').resolve(strict=True)!=(PREFIX/'drive_c').resolve(strict=True)
            or (PREFIX/'dosdevices/z:').resolve(strict=True)!=Path('/')):
        raise ValueError('Retained prefix C:/Z: aliases no longer map to pinned installed file')
    record=install.installed_evidence(PREFIX)
    if record['executable']['path']!=str(EXE.resolve()) or record['executable']['sha256']!=EXE_SHA:
        raise ValueError('Retained installed payload path/hash changed')
    record['owner_aliases']=['C:\\'+str(EXE.relative_to(PREFIX/'drive_c')).replace('/','\\'),
                             r'Z:'+str(EXE).replace('/','\\')]
    record['alias_mapping']={'c:':str((PREFIX/'dosdevices/c:').resolve()),'z:':str((PREFIX/'dosdevices/z:').resolve())}
    return record


def validate(output,fixture):
    if (not output.is_absolute() or output.parent.resolve()!=LAB.resolve()
            or not re.fullmatch(r'edge-headful-[A-Za-z0-9._-]+',output.name)
            or output.exists() or output.is_symlink()):
        raise ValueError('Fresh direct owned edge-headful-* output required')
    expected=LAB/(output.name+'-stage')/'scripts/g4-edge-probe/index.html'
    if fixture.resolve(strict=True)!=expected.resolve(strict=True):
        raise ValueError('Only staged original fixture allowed')
    if (not PREFIX.is_dir() or PREFIX.is_symlink() or EXE.resolve()!=EXE
            or not EXE.is_relative_to(PREFIX)):
        raise ValueError('Wrong retained installed prefix/payload')
    if shutil.disk_usage(LAB).free<1024**3: raise ValueError('At least 1 GiB free; never delete evidence')
    if graphics_resources.sample(PREFIX): raise ValueError('Retained prefix already has processes')
    before=persistence()
    before['installed']=require_payload()
    return before


def command(fixture,output,nonce,startup_diagnostic=False):
    win=lambda p:'Z:'+str(p).replace('/','\\')
    # file URL is interpreted by Windows Edge, not by the Linux host.
    url='file:///Z:'+str(fixture).replace(' ','%20')+'#nonce='+nonce
    argv=[str(BUILD/'wine'),str(EXE),'--app='+url,
            '--user-data-dir='+win(output/'profile'),'--no-first-run','--no-default-browser-check',
            '--enable-logging','--log-file='+win(output/'chromium.log'),'--v=1']
    if startup_diagnostic: argv.append('--vmodule='+VLOG_MODULES)
    return argv


def browser_environment(env,startup_diagnostic):
    private=dict(env)
    if startup_diagnostic: private['WINEDEBUG']+=',+file,+seh'
    return private


def diagnostic_log_usage(output):
    files={name:(output/name).stat().st_size if (output/name).exists() else 0
           for name in ('browser.stdout.log','browser.stderr.log','chromium.log')}
    total=sum(files.values())
    return {'files':files,'total_bytes':total,'limit_bytes':LOG_LIMIT,'passed':total<=LOG_LIMIT}


def fixture_probe_record(text,fixture):
    value,end=strict_json(text.strip())
    if text.strip()[end:] or not isinstance(value,dict): raise ValueError('Invalid fixture file probe JSON')
    with fixture.open('rb') as stream: expected=stream.read(64).hex()
    if (value.get('ok') is not True or type(value.get('size')) is not int or value['size']!=fixture.stat().st_size
            or type(value.get('error')) is not int or value['error']!=0
            or type(value.get('read')) is not int or value['read']!=len(expected)//2 or value.get('prefix_hex')!=expected):
        raise ValueError('Original fixture Win32 file probe failed/mismatched')
    return value


def terminal_failure_front(result,output,comp,helpers,env,bound,now):
    """One best-effort whole front before cleanup; never replace the primary error."""
    record={'classification':'FAILURE_ONLY_DIAGNOSTIC_NOT_PIXEL_ACCEPTANCE','started_monotonic':now()}
    result['failure_front']=record
    try:
        comp.verify()
        timeout=bound(3)  # preserve the entire cleanup reserve
        record['timeout_seconds']=timeout
        path=output/'failure-front.png'
        record['path']=str(path)
        captured=helpers.run_logged(capture_command(path),env,output,'failure-front-capture',timeout)
        record['capture_exit']=captured.returncode
        if captured.returncode: raise RuntimeError('Terminal front capture failed')
        from PIL import Image
        with Image.open(path) as image:
            image.load();record['dimensions']=list(image.size)
        record['sha256']=install.digest(path)
        record['captured']=True
    except Exception as error:
        record['error']=repr(error)
    finally:
        record['finished_monotonic']=now()


def strict_json(text):
    def pairs(values):
        result={}
        for key,value in values:
            if key in result: raise ValueError('Duplicate JSON key')
            result[key]=value
        return result
    def number(s):
        value=float(s)
        if not math.isfinite(value): raise ValueError('Nonfinite JSON number')
        return value
    decoder=json.JSONDecoder(object_pairs_hook=pairs,parse_float=number,
                             parse_constant=lambda s: (_ for _ in ()).throw(ValueError(s)))
    return decoder.raw_decode(text)


def page_record(title,nonce):
    if not isinstance(title,str) or not title.startswith('WEXEDGE '): raise ValueError('Missing readiness title')
    data,end=strict_json(title[8:])
    if title[8+end:] not in ('',' - Microsoft Edge'): raise ValueError('Unexpected title suffix')
    if (not isinstance(data,dict) or type(data.get('v')) is not int or data['v']!=1 or data.get('nonce')!=nonce
            or data.get('kind')!='ready' or data.get('passed') is not True):
        raise ValueError('Stale/failed readiness')
    def finite(v): return type(v) in (int,float) and math.isfinite(v)
    viewport=data.get('viewport',{})
    if (not isinstance(viewport,dict) or not all(finite(viewport.get(k)) for k in ('width','height','dpr'))
            or not 300<=viewport['width']<=4096 or not 240<=viewport['height']<=4096
            or not .5<=viewport['dpr']<=4 or data.get('marker')!=RECTS['marker']
            or data.get('background')!='rgb(232, 245, 233)'
            or not finite(data.get('frozenAtMs')) or data['frozenAtMs']<0):
        raise ValueError('Invalid frozen CSS/viewport geometry')
    apis=data.get('apis')
    if not isinstance(apis,dict) or set(apis)!= {'webgl','webgl2'}: raise ValueError('Missing APIs')
    for api,item in apis.items():
        def rgba(value): return isinstance(value,list) and len(value)==4 and all(type(v) is int and 0<=v<=255 for v in value)
        if (not isinstance(item,dict) or item.get('rect')!=RECTS[api]
                or not all(rgba(item.get(k)) for k in ('defaultPixel','fboPixel','finalPixel'))
                or item.get('defaultPixel')!=[255,0,0,255] or item.get('fboPixel')!=[0,255,0,255]
                or item.get('finalPixel')!=list(COLORS[api]) + [255]
                or item.get('fboComplete') is not True or type(item.get('error')) is not int or item['error']!=0
                or item.get('lost') is not False or not isinstance(item.get('renderer'),str)
                or not item['renderer'].strip() or item['renderer'] in ('null','undefined')
                or re.search(r'llvmpipe|softpipe|swiftshader|Microsoft Basic|WARP',item['renderer'],re.I)):
            raise ValueError('Invalid '+api+' readbacks/renderer')
    return data


def window_record(rows,nonce):
    if not isinstance(rows,list) or len(rows)>4096 or any(not isinstance(w,dict) or not isinstance(w.get('title'),str) for w in rows):
        raise ValueError('Malformed observer inventory')
    candidates=[w for w in rows if w['title'].startswith('WEXEDGE ') and w['title']!='WEXEDGE loading']
    if not candidates: return None
    if len(candidates)!=1: raise ValueError('Ambiguous readiness windows')
    w=candidates[0]
    data=page_record(w['title'],nonce)
    aliases=['C:\\'+str(EXE.relative_to(PREFIX/'drive_c')).replace('/','\\'),r'Z:'+str(EXE).replace('/','\\')]
    if (not isinstance(w.get('image'),str) or w['image'].casefold() not in [p.casefold() for p in aliases] or w.get('visible') is not True
            or w.get('geometry_valid') is not True or type(w.get('pid')) is not int or not 0<w['pid']<=0xffffffff
            or type(w.get('hwnd')) is not int or not 0<w['hwnd']<=0xffffffffffffffff
            or not isinstance(w.get('class'),str) or not w['class']
            or type(w.get('fixed')) is not int or not 32768<=w['fixed']<=262144):
        raise ValueError('Missing/wrong Win32 owner, visibility or scale')
    scale=w['fixed']/65536
    for key in ('window','client'):
        r=w.get(key)
        if (not isinstance(r,dict) or any(type(r.get(k)) is not int for k in ('x','y','width','height'))
                or min(r['x'],r['y'])<0 or min(r['width'],r['height'])<=0
                or max(r.values())>8192): raise ValueError('Invalid native geometry')
    c,b=w['client'],w['window']
    if (c['x']<b['x'] or c['y']<b['y'] or c['x']+c['width']>b['x']+b['width']
            or c['y']+c['height']>b['y']+b['height']
            or abs(data['viewport']['dpr']-scale)>1e-6
            or any(abs(data['viewport'][k]-c[k])>.01 for k in ('width','height'))):
        raise ValueError('Viewport/client/scale mismatch; never guess browser chrome offsets')
    return {'native':w,'page':data,'scale':scale}


def front_pixels(path,state):
    from PIL import Image
    with Image.open(path) as source:
        source.load(); image=source.convert('RGB')
    w=state['native']['window']; c=state['native']['client']; scale=state['scale']
    if round((w['x']+w['width'])*scale)>image.width or round((w['y']+w['height'])*scale)>image.height:
        raise ValueError('Observed window clipped by output')
    v=state['page']['viewport']
    regions={**RECTS,'background':dict(x=v['width']-48,y=v['height']-48,width=32,height=32)}
    samples={}
    for name,r in regions.items():
        box=[round((c['x']+r['x']+r['width']*.25)*scale),round((c['y']+r['y']+r['height']*.25)*scale),
             round((c['x']+r['x']+r['width']*.75)*scale),round((c['y']+r['y']+r['height']*.75)*scale)]
        left,top,right,bottom=box
        if min(left,top)<0 or right>image.width or bottom>image.height or min(right-left,bottom-top)<8:
            raise ValueError('Invalid mapped pixel region')
        pixels=list(image.crop(box).getdata()); matched=sum(p==COLORS[name] for p in pixels)
        samples[name]={'box':box,'expected':COLORS[name],'matched':matched,'count':len(pixels),'passed':matched==len(pixels)}
    return {'passed':all(v['passed'] for v in samples.values()),'samples':samples,'sha256':install.digest(path)}


def collect_fronts(observe,capture,alive,output,initial,records,*,now=time.monotonic,sleep=time.sleep):
    origin=now()
    def unchanged(state):
        if state is None or state['page']!=initial['page'] or any(state['native'][k]!=initial['native'][k]
                for k in ('hwnd','pid','image','class','window','client','fixed','visible')):
            raise ValueError('Frozen readiness/window identity changed')
    for offset in (0,1,3):
        target=origin+offset
        if now()<target: sleep(target-now())
        record={'offset_seconds':offset,'request_elapsed':now()-origin}; records.append(record)
        alive()
        state=observe()
        record['state']=state
        # Ignore title suffix only; page protocol and all native identity/geometry must remain frozen.
        unchanged(state)
        path=output/('front-'+str(offset)+'.png')
        record['capture_started_elapsed']=now()-origin
        record['lateness_seconds']=max(0,now()-target)
        capture(path)
        record['capture_finished_elapsed']=now()-origin
        record['pixels']=front_pixels(path,state)
        alive()
        record['after_capture_state']=observe()
        unchanged(record['after_capture_state'])
    # Early stale pixels remain evidence, never drive scheduling/retries.
    return all(r['pixels']['passed'] for r in records[-2:])


def log_health(output):
    # Reuse established graphics error signatures, not animation gates.
    text='\n'.join(p.read_text(errors='replace') for p in (output/'browser.stderr.log',output/'chromium.log') if p.exists())
    # Analyzer reads chromium.log; do not rewrite vendor logs just to reuse it.
    patterns={'fatal':r'\bFATAL:|FATAL ERROR|Unhandled exception',
        'gpu_exit':r'GPU process exited|GPU process crashed|GPU process isn.t usable',
        'renderer_exit':r'Renderer process.*(?:crash|exit)|render process gone',
        'graphics':r'Requested GL implementation.*not found|eglInitialize D3D11Warp failed|D3D11CreateDevice failed|DCompositionCreateDevice failed|Unable to initialize SkSurface|Failed to create shared context for virtualization'}
    return {key:len(re.findall(pattern,text,re.I)) for key,pattern in patterns.items()}


def process_roles(snapshot):
    roles={}
    for pid,item in snapshot.items():
        match=re.search(r'(?:^|\s)--type=(gpu-process|renderer)(?:\s|$)',item.get('cmdline',''))
        if match:
            roles[str(pid)+':'+str(item['start_ticks'])]={'linux_pid':str(pid),'role':match[1],**item}
    return roles


def record_roles(result,previous,snapshot,cleanup):
    roles=process_roles(snapshot)
    result['observed_process_roles'].update(roles)
    if not cleanup:
        result['role_exits_before_cleanup'].extend(previous[key] for key in previous.keys()-roles.keys())
    return roles


def run(output,fixture,helpers,*,startup_diagnostic=False,private_loopback_control=False,
        now=time.monotonic,sleep=time.sleep,popen=subprocess.Popen):
    start=now()
    if private_loopback_control and not startup_diagnostic:
        raise ValueError('Private loopback control requires startup diagnostic mode')
    before=validate(output,fixture)  # never cleanup on a rejected guard
    loopback=load('run-isolated-graphics').verify_private_loopback() if private_loopback_control else None
    output.mkdir(); save(output/'persistence-before.json',before)
    env=install.environment(output); env['WINEPREFIX']=str(PREFIX)
    env['WINEDEBUG']='-all,+timestamp,+pid,+tid,+winediag,+process'
    for key in ('HOME','XDG_CACHE_HOME','XDG_CONFIG_HOME','XDG_DATA_HOME','TMPDIR'): Path(env[key]).mkdir()
    helpers.configure_gl_driver(env,'virgl')
    comp=OwnedWexcomp(LAB/'labwc',LAB/'wayland',output,env,'gles2')
    nonce=secrets.token_hex(16)
    result={'passed':False,'classification':'Scoped headful clear/readback/front smoke only; GPU compositing NOT_PROVEN',
            'nonce':nonce,'argv':command(fixture,output,nonce,startup_diagnostic),'front_samples':[],
            'startup_diagnostic':startup_diagnostic,'win32_pid_is_not_linux_pid':True}
    if startup_diagnostic:
        result['classification']='Startup diagnostic ONLY; rendering acceptance NOT_EVALUATED'
    if private_loopback_control: result['private_loopback_before']=loopback
    deadline=start+TOTAL; browser=None; files=[]; stop=threading.Event(); cleanup_started=threading.Event(); monitor=None
    result.update(observed_process_roles={},role_exits_before_cleanup=[])
    def bound(cap,cleanup=False):
        remaining=deadline-now()-(0 if cleanup else RESERVE)
        if remaining<=0: raise TimeoutError('Complete headful budget exhausted')
        return min(cap,remaining)
    def sample():
        try:
            previous={}
            with (output/'processes.jsonl').open('a') as stream:
                while not stop.is_set():
                    snapshot=graphics_resources.sample(PREFIX)
                    for pid,item in snapshot.items():
                        try: item['cmdline']=(Path('/proc')/pid/'cmdline').read_bytes().replace(b'\0',b' ').decode(errors='replace')
                        except OSError: pass
                    previous=record_roles(result,previous,snapshot,cleanup_started.is_set())
                    stream.write(json.dumps({'elapsed':now()-start,'processes':snapshot})+'\n');stream.flush()
                    stop.wait(.25)
        except Exception as error: result['monitor_error']=repr(error)
    try:
        monitor=threading.Thread(target=sample,daemon=True);monitor.start()
        if bound(55)<55: raise TimeoutError('Insufficient compositor/EGL budget')
        result['compositor']=comp.start(); result['outer']=comp.verify_gpu_renderer('virgl')
        result['inner']=verify_native_gl_driver(env,output,'virgl')
        observer=output/'window-observer.exe'
        built=helpers.run_logged(['x86_64-w64-mingw32-g++','-O2','-static','-o',str(observer),
            str(Path(__file__).with_name('g4_edge_window_observer.cpp')),'-luser32'],env,output,'observer-build',bound(20))
        if built.returncode: raise RuntimeError('Observer compilation failed')
        result['observer_sha256']=install.digest(observer)
        boot=helpers.run_logged([str(BUILD/'wine'),'wineboot','-u'],env,output,'wineboot',bound(60))
        result['wineboot_exit']=boot.returncode
        # Record actual postboot state even when boot itself failed.
        save(output/'persistence-after.json',persistence())
        if boot.returncode: raise RuntimeError('Retained prefix Wineboot failed')
        result['installed_after_boot']=require_payload()
        if startup_diagnostic:
            probe=helpers.run_logged([str(BUILD/'wine'),str(observer),'--fixture-file',
                'Z:'+str(fixture).replace('/','\\')],env,output,'fixture-file-probe',bound(5))
            result['fixture_file_probe_exit']=probe.returncode
            result['fixture_file_probe_stdout']=probe.stdout
            if probe.returncode: raise RuntimeError('Original fixture Win32 file probe failed')
            result['fixture_file_probe']=fixture_probe_record(probe.stdout,fixture)
        def observe():
            n=len(result.setdefault('observations',[])); label='native-'+str(n)
            completed=helpers.run_logged([str(BUILD/'wine'),str(observer)],env,output,label,bound(5))
            if completed.returncode: raise RuntimeError('Read-only observer failed')
            rows,end=strict_json(completed.stdout.strip())
            if completed.stdout.strip()[end:]: raise ValueError('Trailing observer output')
            result['observations'].append({'elapsed':now()-start,'windows':rows})
            return window_record(rows,nonce)
        def alive():
            if startup_diagnostic:
                result['log_quota']=diagnostic_log_usage(output)
                result['log_quota_checks']=result.get('log_quota_checks',0)+1
                if not result['log_quota']['passed']: raise RuntimeError('Startup diagnostic log quota exceeded')
            comp.verify()
            if browser.poll() is not None: raise RuntimeError('Browser launcher exited before collection completed')
            if result['role_exits_before_cleanup']: raise RuntimeError('Observed GPU/renderer process exited before cleanup')
            if any(log_health(output).values()): raise RuntimeError('Browser fatal/renderer/GPU/graphics log failure')
        readiness_seconds=bound(45)  # refuse an exhausted budget BEFORE browser creation
        files=[(output/'browser.stdout.log').open('wb'),(output/'browser.stderr.log').open('wb')]
        browser=popen(result['argv'],env=browser_environment(env,startup_diagnostic),stdout=files[0],stderr=files[1],start_new_session=True)
        result['browser_launcher_linux_pid']=browser.pid
        result['browser_launcher_prefix_sample']=graphics_resources.sample(PREFIX).get(str(browser.pid))
        readiness_deadline=now()+readiness_seconds
        initial=None
        while now()<readiness_deadline:
            alive();initial=observe()
            if initial: break
            sleep(min(.25,max(0,readiness_deadline-now())))
        if initial is None: raise TimeoutError('No nonce-bound ready browser window')
        result['ready_elapsed']=now()-start
        def capture(path):
            code=helpers.run_logged(capture_command(path),env,output,path.stem+'-capture',bound(5)).returncode
            if code: raise RuntimeError('Front capture failed')
        result['settled_fronts_passed']=collect_fronts(observe,capture,alive,output,initial,result['front_samples'],now=now,sleep=sleep)
        result['completed']=True
    except Exception as error:
        result['error']=repr(error)
        if browser is not None:
            terminal_failure_front(result,output,comp,helpers,env,bound,now)
    finally:
        cleanup_started.set()
        for label,args in (('kill',['-k']),('wait',['-w'])):
            try:
                result['cleanup_'+label]=helpers.run_logged([str(BUILD/'server/wineserver')]+args,env,output,'cleanup-'+label,bound(10,True)).returncode
            except Exception as error: result.setdefault('cleanup_errors',[]).append(repr(error))
        try:
            if browser is not None:
                try: result['browser_exit_after_cleanup']=browser.wait(timeout=bound(5,True))
                except subprocess.TimeoutExpired:
                    browser.terminate(); result['browser_exit_after_cleanup']=browser.wait(timeout=bound(5,True))
                    result.setdefault('cleanup_errors',[]).append('Browser required termination after prefix cleanup')
        except Exception as error: result.setdefault('cleanup_errors',[]).append(repr(error))
        stop.set()
        if monitor is not None and monitor.ident is not None: monitor.join(timeout=1)
        for stream in files:
            try: stream.close()
            except Exception as error: result.setdefault('cleanup_errors',[]).append(repr(error))
        try:
            comp.stop()
            result['remaining_servers']=helpers.matching_servers(BUILD/'server/wineserver',PREFIX)
            result['remaining_prefix_processes']=graphics_resources.sample(PREFIX)
            if private_loopback_control:
                result['private_loopback_after']=load('run-isolated-graphics').verify_private_loopback()
            if startup_diagnostic:
                result['log_quota_after_cleanup']=diagnostic_log_usage(output)
            result['log_errors']=({'log_quota':1} if startup_diagnostic and
                not result['log_quota_after_cleanup']['passed'] else log_health(output))
            result['chromium_log_present']=((output/'chromium.log').is_file() and (output/'chromium.log').stat().st_size>0)
        except Exception as error: result.setdefault('cleanup_errors',[]).append(repr(error))
        finally:
            result['elapsed_seconds']=now()-start
            result['passed']=(not startup_diagnostic and result.get('completed') is True and result.get('settled_fronts_passed') is True
                and not result.get('error') and not result.get('monitor_error') and not result.get('cleanup_errors')
                and not result['role_exits_before_cleanup']
                and result.get('chromium_log_present') is True
                and result.get('cleanup_kill') in (0,1) and result.get('cleanup_wait')==0
                and result.get('remaining_servers')==[] and result.get('remaining_prefix_processes')=={}
                and not any(result.get('log_errors',{'missing':1}).values())
                and (monitor is None or not monitor.is_alive()) and result['elapsed_seconds']<=TOTAL)
            save(output/'results.json',result)
    return 0 if result['passed'] else 1


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-dir',type=Path,required=True)
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--startup-diagnostic',action='store_true',help='Bounded logging/file probe only; can never pass rendering acceptance')
    parser.add_argument('--private-loopback-control',action='store_true',help='Same FILE URL diagnostic; require inherited proof of private lo UP')
    args=parser.parse_args()
    return run(args.output_dir,args.fixture,load('check-g3-correctness'),startup_diagnostic=args.startup_diagnostic,
               private_loopback_control=args.private_loopback_control)


if __name__=='__main__': raise SystemExit(main())
