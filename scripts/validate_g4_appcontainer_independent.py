# SPDX-License-Identifier: GPL-2.0-or-later
"""One fixed independent built-Wine unit matrix; run only in private namespaces.

No build/patch/install/copy/VM operations. Logs and fresh prefixes are private.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import select
import signal
import struct
import subprocess
import time

from g4_prefix_ownership import scan
from graphics_resources import sample

ROOT=Path('/var/tmp/wex-g4-ac-parent-20261026')
BUILD=ROOT/'build'
EVIDENCE=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/appcontainer-independent-r1-20261026')

def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()

def pe_machine(p):
    with p.open('rb') as stream:
        header=stream.read(64)
        if len(header)!=64 or header[:2]!=b'MZ':raise ValueError('Missing PE header')
        offset=struct.unpack_from('<I',header,60)[0]
        if not 64<=offset<=p.stat().st_size-24:raise ValueError('Invalid PE offset')
        stream.seek(offset);header=stream.read(24)
    if header[:4]!=b'PE\0\0':raise ValueError('Invalid PE signature')
    return struct.unpack_from('<H',header,4)[0]

def save(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')

def cell(arch,dll,display):
    label=arch+'-'+dll;folder=ROOT/label;folder.mkdir();prefix=folder/'prefix'
    record={'arch':arch,'dll':dll,'prefix':str(prefix),'limits':{'test':240,'server_cleanup_each':15,'observer_reap':5}}
    executable=BUILD/f'dlls/{dll}/tests/{arch}-windows/{dll}_test.exe'
    env={k:v for k,v in os.environ.items() if not k.startswith(('WINE','WINETEST','WEX_')) and k not in ('WAYLAND_DISPLAY','WAYLAND_SOCKET','DBUS_SESSION_BUS_ADDRESS','LD_LIBRARY_PATH','DISPLAY')}
    for k,n in [('HOME','home'),('XDG_CACHE_HOME','cache'),('XDG_CONFIG_HOME','config'),('XDG_DATA_HOME','data'),('XDG_RUNTIME_DIR','runtime'),('TMPDIR','tmp')]:
        path=folder/n;path.mkdir(mode=0o700);env[k]=str(path)
    env.update(DISPLAY=display,WINEPREFIX=str(prefix),WINESERVER=str(BUILD/'server/wineserver'),
               WINEDEBUG='-all,+timestamp,+pid,+tid,trace+loaddll,+winediag',WINETEST_INTERACTIVE='0')
    argv=[str(BUILD/'wine'),str(executable), 'security' if dll=='kernelbase' else 'userenv']
    record.update(argv=argv,executable_sha256=sha(executable),executable_machine=pe_machine(executable),environment={k:env[k] for k in ('WINEPREFIX','WINESERVER','WINEDEBUG','DISPLAY','HOME')})
    if record['executable_machine']!=({'x86_64':0x8664,'i386':0x14c}[arch]):raise ValueError('Wrong unit architecture')
    child=None;maps=set();start=time.monotonic()
    try:
        record['ownership_before']=scan(str(prefix),secrets.token_hex(16))
        if record['ownership_before']['status']!='complete' or record['ownership_before']['busy']:raise RuntimeError('Prefix ownership not proven empty')
        with (EVIDENCE/(label+'.stdout.log')).open('wb') as out,(EVIDENCE/(label+'.stderr.log')).open('wb') as err:
            child=subprocess.Popen(argv,env=env,stdout=out,stderr=err,start_new_session=True)
            record['observer_pid']=child.pid
            while child.poll() is None:
                for pid in sample(prefix):
                    try:
                        for line in Path('/proc',pid,'maps').read_text().splitlines():
                            if any('/'+n+'.dll' in line.lower() for n in ('kernelbase','userenv','ntdll')):maps.add(line)
                    except OSError:pass # diagnostic sampling only; final ownership scan is strict
                if time.monotonic()-start>240:raise TimeoutError('Owned unit240s deadline')
                time.sleep(.1)
            record['exit']=child.wait(timeout=5);record['observer_reaped']=True
        text=(EVIDENCE/(label+'.stdout.log')).read_text(errors='replace')+(EVIDENCE/(label+'.stderr.log')).read_text(errors='replace')
        record['summaries']=[line for line in text.splitlines() if 'tests executed' in line]
        record['builtin_trace']=[line for line in text.splitlines() if 'loaddll' in line and any(n+'.dll' in line.lower() for n in ('kernelbase','userenv','ntdll'))]
        for name in ('kernelbase','userenv','ntdll'):
            path=prefix/'drive_c/windows'/('syswow64' if arch=='i386' else 'system32')/(name+'.dll')
            module=BUILD/f'dlls/{name}/{arch}-windows/{name}.dll'
            record.setdefault('modules',{})[name]={'prefix_path':str(path),'resolved_path':str(path.resolve()),'prefix_sha256':sha(path),'build_path':str(module),'build_sha256':sha(module)}
        record['normal_builtin_observed']=all(any(name+'.dll' in line.lower() and 'builtin' in line for line in record['builtin_trace']) for name in ('kernelbase','userenv','ntdll'))
        record['unit_pass']=(record['exit']==0 and bool(record['summaries']) and all(re.search(r'0 failures\).*0 skipped',line) for line in record['summaries']) and record['normal_builtin_observed'])
    except Exception as error:record['error']=repr(error)
    finally:
        for flag in ('-k','-w'):
            try:
                with (EVIDENCE/(label+'.cleanup'+flag+'.log')).open('wb') as log:
                    result=subprocess.run([str(BUILD/'server/wineserver'),flag],env=env,stdout=log,stderr=log,timeout=15)
                record.setdefault('cleanup',{})[flag]=result.returncode
            except Exception as error:record.setdefault('cleanup_errors',[]).append(repr(error))
        if child is not None and not record.get('observer_reaped'):
            try:
                if child.poll() is None:os.killpg(child.pid,signal.SIGKILL);record['observer_killed']=True
                record['exit_after_cleanup']=child.wait(timeout=5);record['observer_reaped']=True
            except Exception as error:record.setdefault('cleanup_errors',[]).append(repr(error))
        record['ownership_after']=scan(str(prefix),secrets.token_hex(16))
        record['cleanup_complete']=(not record.get('cleanup_errors') and record.get('cleanup',{}).get('-k') in (0,1)
            and record.get('cleanup',{}).get('-w')==0 and record['ownership_after']['status']=='complete' and record['ownership_after']['busy'] is False)
        record['observed_maps']=sorted(maps);record['elapsed']=time.monotonic()-start
        record['passed']=record.get('unit_pass') is True and record['cleanup_complete'] and 'error' not in record
        save(EVIDENCE/(label+'.json'),record)
    return record

def run():
    if os.geteuid()!=0:raise RuntimeError('Namespace wrapper/root required')
    if os.readlink('/proc/self/ns/mnt')==os.readlink('/proc/1/ns/mnt'):raise RuntimeError('Private mount namespace required')
    spec=importlib.util.spec_from_file_location('isolation',Path(__file__).with_name('run-isolated-graphics.py'))
    isolation=importlib.util.module_from_spec(spec);spec.loader.exec_module(isolation)
    summary={'private_loopback':isolation.verify_private_loopback(),'cells':[]}
    target=EVIDENCE/'units.json'
    if target.exists():raise RuntimeError('Never overwrite previous matrix')
    display=None;read_fd,write_fd=os.pipe();start=time.monotonic()
    try:
        with (EVIDENCE/'xvfb.log').open('xb') as log:
            display=subprocess.Popen(['Xvfb','-displayfd',str(write_fd),'-screen','0','1280x720x24','-nolisten','tcp','-nolisten','local'],pass_fds=(write_fd,),stdout=log,stderr=log)
        os.close(write_fd);write_fd=None
        summary['xvfb_pid']=display.pid
        if not select.select([read_fd],[],[],10)[0]:raise TimeoutError('Private Xvfb10s startup')
        value=os.read(read_fd,64).decode().strip()
        if not value.isdigit() or display.poll() is not None:raise RuntimeError('Invalid owned display')
        summary['display']=':'+value
        for arch in ('x86_64','i386'):
            for dll in ('kernelbase','userenv'):
                record=cell(arch,dll,summary['display']);summary['cells'].append(record)
                print(json.dumps({'arch':arch,'dll':dll,'passed':record['passed'],'summaries':record.get('summaries'),'error':record.get('error')}),flush=True)
                if not record['cleanup_complete']:raise RuntimeError('Stop: owned prefix cleanup incomplete')
    except Exception as error:summary['error']=repr(error)
    finally:
        os.close(read_fd)
        if write_fd is not None:os.close(write_fd)
        if display is not None:
            try:
                if display.poll() is None:display.terminate()
                summary['xvfb_exit']=display.wait(timeout=5)
            except Exception as error:
                summary['display_cleanup_error']=repr(error);display.kill();summary['xvfb_exit_after_kill']=display.wait(timeout=5)
        try:summary['private_loopback_after']=isolation.verify_private_loopback()
        except Exception as error:summary['namespace_error']=repr(error)
        summary['elapsed']=time.monotonic()-start
        summary['passed']=(len(summary['cells'])==4 and all(v['passed'] for v in summary['cells']) and 'error' not in summary
            and 'display_cleanup_error' not in summary and summary.get('private_loopback_after')==summary['private_loopback'])
        save(target,summary)
    return 0 if summary['passed'] else 1

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--run',action='store_true');args=parser.parse_args()
    if not args.run:parser.print_help()
    else:raise SystemExit(run())
