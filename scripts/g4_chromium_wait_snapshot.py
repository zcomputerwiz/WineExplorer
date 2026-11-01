# SPDX-License-Identifier: GPL-2.0-or-later
"""One bounded read-only Linux thread snapshot of the current private prefix."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time


def observe(output, env, build, prefix, *, maximum_seconds=8, proc_root=Path('/proc'), now=time.monotonic):
    output, build, prefix = Path(output), Path(build), Path(prefix)
    if env.get('WINEPREFIX') != str(prefix) or env.get('WINESERVER') != str(build/'server/wineserver'):
        raise ValueError('Wait snapshot requires exact candidate prefix/server environment')
    if prefix.name not in ('chromium-token-wait-r9-20261031-prefix','chromium-token-wait-r10-20261031-prefix','chromium-token-wait-r13-20261101-prefix'):
        raise ValueError('Wait snapshot requires dedicated fixed diagnostic prefix')
    if not 0 < maximum_seconds <= 8:
        raise ValueError('Wait snapshot exceeds bounded diagnostic budget')
    deadline=now()+maximum_seconds
    record={'maximum_seconds':maximum_seconds,'processes':[],'errors':[],
            'classification':'Read-only private-prefix thread waits; not browser acceptance',
            'candidate_server_sha256':hashlib.sha256((build/'server/wineserver').read_bytes()).hexdigest()}
    def bounded():
        if now()>=deadline:raise TimeoutError('Wait snapshot deadline')
    def read(path):
        bounded()
        try:
            with path.open('rb') as stream:data=stream.read(65537)
            if len(data)>65536:raise ValueError('Wait snapshot field quota')
            return data.decode(errors='replace')
        except (FileNotFoundError,ProcessLookupError,PermissionError) as error:
            return {'error':type(error).__name__}
    marker='WINEPREFIX='+str(prefix)
    for process in proc_root.iterdir():
        if not process.name.isdecimal():continue
        bounded()
        try:
            with (process/'environ').open('rb') as stream:data=stream.read(65537)
        except (FileNotFoundError,ProcessLookupError,PermissionError) as error:
            record['errors'].append({'pid':process.name,'field':'prefix_identity','error':type(error).__name__})
            continue
        if len(data)>65536:raise ValueError('Wait snapshot field quota')
        if marker.encode() not in data.split(b'\0'):continue
        if len(record['processes'])>=64:raise ValueError('Wait snapshot process quota')
        item={'pid':int(process.name),'cmdline':read(process/'cmdline'),'threads':[]}
        try:tasks=list((process/'task').iterdir())
        except (FileNotFoundError,ProcessLookupError,PermissionError) as error:
            item['thread_error']=type(error).__name__;tasks=[]
        if len(tasks)>512:raise ValueError('Wait snapshot thread quota')
        for thread in tasks:
            if not thread.name.isdecimal():continue
            item['threads'].append({'tid':int(thread.name),**{field:read(thread/field) for field in ('comm','wchan','syscall')}})
        record['processes'].append(item)
    (output/'wait-snapshot.json').write_text(json.dumps(record,indent=2)+'\n')
    return record


def observe_root(output, env, build, prefix, *, run=subprocess.run):
    name=Path(output).name
    if name not in ('chromium-token-wait-r13-20261101','chromium-token-wait-r14-20261101'):
        raise ValueError('Root wait snapshot requires fixed r13/r14 paths')
    lab=Path('/home/debian/wex-g4')
    if (Path(output),Path(build),Path(prefix)) != (lab/name,lab/(name+'-wine/build/wow64'),lab/(name+'-prefix')):
        raise ValueError('Root wait snapshot requires fixed r13 paths')
    if env.get('WINEPREFIX')!=str(prefix) or env.get('WINESERVER')!=str(Path(build)/'server/wineserver'):
        raise ValueError('Root wait snapshot requires candidate environment')
    result=run(['sudo','-n','python3','-B',str(Path(__file__).resolve()),'--root-'+name.split('-')[3]],
               capture_output=True,text=True,timeout=10,check=True)
    if len(result.stdout.encode())>1024**2:raise ValueError('Root wait snapshot output quota')
    record=json.loads(result.stdout)
    if record.get('candidate_server_sha256')!='196f24db0bfd402bc5656dc6be09a9d5baf82f863eeedd9596f9f3f78160a800':
        raise ValueError('Root wait snapshot server identity mismatch')
    return record


def browser_mappings(log, journal):
    rows=[json.loads(line) for line in journal.splitlines() if line.strip()]
    bound=[row for row in rows if row.get('event')=='bound']
    if len(bound)!=1:raise ValueError('Exactly one browser binding required')
    wine_pid=bound[0].get('child_pid')
    if type(wine_pid) is not int or wine_pid<=0:raise ValueError('Invalid bound browser PID')
    pattern=r'trace:wex_process:\w+ win_pid=([0-9a-f]+) win_tid=([0-9a-f]+) unix_pid=(\d+) unix_tid=(\d+)'
    mappings={(int(pid),int(tid)) for win,_,pid,tid in re.findall(pattern,log) if int(win,16)==wine_pid}
    if not mappings or len({pid for pid,_ in mappings})!=1:raise ValueError('Unambiguous bound browser Unix mapping required')
    if len(mappings)>512:raise ValueError('Mapped browser thread quota')
    if any(pid<=0 or tid<=0 for pid,tid in mappings):raise ValueError('Invalid browser Unix mapping')
    return wine_pid,sorted(mappings)


def mapped_browser(output, *, proc_root=Path('/proc'), now=time.monotonic):
    deadline=now()+8
    def read(path):
        if now()>=deadline:raise TimeoutError('Mapped browser snapshot deadline')
        try:
            with path.open('rb') as stream:data=stream.read(65537)
        except (FileNotFoundError,ProcessLookupError,PermissionError) as error:
            return {'error':type(error).__name__}
        if len(data)>65536:raise ValueError('Mapped browser field quota')
        return data.decode(errors='replace')
    for path in (output/'helper.stderr.log',output/'native-exit.jsonl'):
        if path.resolve(strict=True)!=path:raise ValueError('Mapped browser noncanonical evidence')
    with (output/'helper.stderr.log').open('rb') as stream:log=stream.read(1024**2+1)
    if len(log)>1024**2:raise ValueError('Mapped browser log quota')
    journal=read(output/'native-exit.jsonl')
    if not isinstance(journal,str):raise ValueError('Mapped browser binding unavailable')
    wine_pid,mappings=browser_mappings(log.decode(errors='replace'),journal)
    record={'wine_pid':wine_pid,'unix_pid':mappings[0][0],'threads':[]}
    image=r'Z:\home\debian\wex-g4\chromium-headful-r1-20261023-stage\vendor\extracted\chrome-win\chrome.exe'
    profile='--user-data-dir=Z:'+str(output/'profile').replace('/','\\')
    for pid,tid in mappings:
        task=proc_root/str(tid)
        command=read(task/'cmdline')
        if isinstance(command,dict):
            record['threads'].append({'tid':tid,'identity_error':command});continue
        args=command.rstrip('\0').split('\0')
        if not args or args[0]!=image or profile not in args or any(arg.startswith('--type=') for arg in args):
            raise ValueError('Mapped browser command identity mismatch')
        status=read(task/'status')
        if not isinstance(status,str) or not re.search(r'^Tgid:\s*'+str(pid)+r'\s*$',status,re.MULTILINE):
            raise ValueError('Mapped browser thread group mismatch')
        record['threads'].append({'tid':tid,'identity_verified':True,
                                  **{field:read(task/field) for field in ('comm','wchan','syscall')}})
    if not any(thread.get('identity_verified') for thread in record['threads']):
        raise ValueError('No live mapped browser thread identity verified')
    return record


def root_snapshot():
    if sys.argv[1:] not in (['--root-r13'],['--root-r14']) or os.geteuid()!=0:
        raise ValueError('Fixed root snapshot only')
    identities={name:os.readlink('/proc/self/ns/'+name) for name in ('mnt','net')}
    if any(identities[name]==os.readlink('/proc/1/ns/'+name) for name in identities):
        raise ValueError('Root snapshot refuses shared namespaces')
    lab=Path('/home/debian/wex-g4');name='chromium-token-wait-'+sys.argv[1].split('-')[-1]+'-20261101'
    output=lab/name;prefix=lab/(name+'-prefix');build=lab/(name+'-wine/build/wow64')
    for path in (output,prefix,build):
        if not path.is_dir() or path.resolve(strict=True)!=path:raise ValueError('Root snapshot noncanonical path')
    if hashlib.sha256((build/'server/wineserver').read_bytes()).hexdigest()!='196f24db0bfd402bc5656dc6be09a9d5baf82f863eeedd9596f9f3f78160a800':
        raise ValueError('Root snapshot server identity mismatch')
    env={'WINEPREFIX':str(prefix),'WINESERVER':str(build/'server/wineserver')}
    if name.endswith('r14-20261101'):
        record={'classification':'Exact bound Wine/Unix mapping and live thread identity; not browser acceptance',
                'candidate_server_sha256':hashlib.sha256((build/'server/wineserver').read_bytes()).hexdigest(),
                'browser':mapped_browser(output)}
        (output/'wait-snapshot.json').write_text(json.dumps(record,indent=2)+'\n')
    else:record=observe(output,env,build,prefix)
    record['root_namespaces']=identities
    encoded=json.dumps(record)
    if len(encoded.encode())>1024**2:raise ValueError('Root wait snapshot output quota')
    print(encoded)


if __name__=='__main__':
    root_snapshot()
