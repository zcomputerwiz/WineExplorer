# SPDX-License-Identifier: GPL-2.0-or-later
"""One bounded read-only Linux thread snapshot of the current private prefix."""
import hashlib
import json
import os
from pathlib import Path
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
    name='chromium-token-wait-r13-20261101'
    lab=Path('/home/debian/wex-g4')
    if (Path(output),Path(build),Path(prefix)) != (lab/name,lab/(name+'-wine/build/wow64'),lab/(name+'-prefix')):
        raise ValueError('Root wait snapshot requires fixed r13 paths')
    if env.get('WINEPREFIX')!=str(prefix) or env.get('WINESERVER')!=str(Path(build)/'server/wineserver'):
        raise ValueError('Root wait snapshot requires candidate environment')
    result=run(['sudo','-n','python3','-B',str(Path(__file__).resolve()),'--root-r13'],
               capture_output=True,text=True,timeout=10,check=True)
    if len(result.stdout.encode())>1024**2:raise ValueError('Root wait snapshot output quota')
    record=json.loads(result.stdout)
    if record.get('candidate_server_sha256')!='196f24db0bfd402bc5656dc6be09a9d5baf82f863eeedd9596f9f3f78160a800':
        raise ValueError('Root wait snapshot server identity mismatch')
    return record


def root_snapshot():
    if sys.argv[1:]!=['--root-r13'] or os.geteuid()!=0:
        raise ValueError('Fixed root snapshot only')
    identities={name:os.readlink('/proc/self/ns/'+name) for name in ('mnt','net')}
    if any(identities[name]==os.readlink('/proc/1/ns/'+name) for name in identities):
        raise ValueError('Root snapshot refuses shared namespaces')
    lab=Path('/home/debian/wex-g4');name='chromium-token-wait-r13-20261101'
    output=lab/name;prefix=lab/(name+'-prefix');build=lab/(name+'-wine/build/wow64')
    for path in (output,prefix,build):
        if not path.is_dir() or path.resolve(strict=True)!=path:raise ValueError('Root snapshot noncanonical path')
    if hashlib.sha256((build/'server/wineserver').read_bytes()).hexdigest()!='196f24db0bfd402bc5656dc6be09a9d5baf82f863eeedd9596f9f3f78160a800':
        raise ValueError('Root snapshot server identity mismatch')
    env={'WINEPREFIX':str(prefix),'WINESERVER':str(build/'server/wineserver')}
    record=observe(output,env,build,prefix)
    record['root_namespaces']=identities
    encoded=json.dumps(record)
    if len(encoded.encode())>1024**2:raise ValueError('Root wait snapshot output quota')
    print(encoded)


if __name__=='__main__':
    root_snapshot()
