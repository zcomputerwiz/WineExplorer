# SPDX-License-Identifier: GPL-2.0-or-later
"""Fixed root-only R3 readonly module view, INSIDE the graphics namespace."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time

from g4_diagnostic_inventory import observe_file,require_regular_canonical
LAB=Path('/home/debian/wex-g4')
BUILD=Path('/var/tmp/wex-g3-repair-20261018/clean-source/build/wow64')
STAGE=LAB/'chromium-userenv-r3-20261024-stage'
OUTPUT=LAB/'chromium-userenv-r3-20261024'
MODE='userenv-r3'

def configure(mode):
    global MODE,STAGE,OUTPUT
    if mode not in ('userenv-r3','userenv-r4'):raise ValueError('Unknown fixed module view')
    MODE=mode
    name='chromium-userenv-r4-20261025' if mode=='userenv-r4' else 'chromium-userenv-r3-20261024'
    STAGE=LAB/(name+'-stage');OUTPUT=LAB/name

def selected():
    return [(STAGE/'modules'/arch/'userenv.dll',BUILD/'dlls/userenv'/(arch+'-windows/userenv.dll'),machine)
            for arch,machine in (('x86_64',0x8664),('i386',0x14c))]

def validate(readlink=os.readlink,uid=os.geteuid,loopback=None):
    if uid()!=0:raise ValueError('Root module view required')
    identities={n:readlink('/proc/self/ns/'+n) for n in ('mnt','net')}
    if any(identities[n]==readlink('/proc/1/ns/'+n) for n in identities):raise ValueError('Refusing init/shared namespace before mounts')
    if BUILD.resolve(strict=True)!=BUILD or not BUILD.is_dir():raise ValueError('Wrong/noncanonical fixed build')
    for source,target,machine in selected():
        require_regular_canonical(source);require_regular_canonical(target)
        with source.open('rb') as stream:
            data=stream.read(64)
            if len(data)!=64 or data[:2]!=b'MZ':raise ValueError('Required PE module')
            off=struct.unpack_from('<I',data,60)[0]
            if not 64<=off<=source.stat().st_size-24:raise ValueError('Invalid module header')
            stream.seek(off);data=stream.read(24)
        if data[:4]!=b'PE\0\0' or struct.unpack_from('<H',data,4)[0]!=machine:raise ValueError('Wrong module architecture')
    if loopback is None:
        spec=importlib.util.spec_from_file_location('view_isolation',Path(__file__).with_name('run-isolated-graphics.py'))
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        loopback=module.verify_private_loopback
    return {'namespaces':identities,'private_loopback':loopback()}

def execute(validate_context=validate,run=subprocess.run,execv=os.execv,emit=print,now=time.monotonic):
    start=now();end=start+12;record={'maximum_seconds':12}
    def left():
        remaining=end-now()
        if remaining<=0:raise TimeoutError('Root module view12s deadline')
        return min(2,remaining)
    try:
        record['context']=validate_context() # ALL validation before first mount
        record['files']=[{'source':observe_file(s,maximum_seconds=2,deadline=end,now=now),'before_target':observe_file(t,maximum_seconds=2,deadline=end,now=now)} for s,t,_ in selected()]
        commands=[['mount','--bind',str(BUILD),str(BUILD)],['mount','-o','remount,bind,ro',str(BUILD)]]
        for source,target,_ in selected():commands.extend([['mount','--bind',str(source),str(target)],['mount','-o','remount,bind,ro',str(target)]])
        record['commands']=commands
        for command in commands:run(command,check=True,timeout=left())
        record['inside']=[observe_file(t,maximum_seconds=2,deadline=end,now=now) for _,t,_ in selected()]
        left();record['elapsed_seconds']=now()-start
        encoded=json.dumps(record,allow_nan=False)
        if len(encoded.encode())>32768:raise ValueError('Module view evidence quota')
        emit('USERENV_MODULE_VIEW '+encoded,flush=True)
        argv=['/usr/sbin/runuser','-u','debian','--','python3','-B',str(STAGE/'scripts/check-g4-chromium-native-exit.py'),'--'+MODE,'--output-dir',str(OUTPUT)]
        execv(argv[0],argv)
    except Exception as error:
        record['error']=repr(error);emit('USERENV_MODULE_VIEW_ERROR '+json.dumps(record,allow_nan=False),flush=True);raise

if __name__=='__main__':
    if sys.argv[1:]==['--userenv-r4']:configure('userenv-r4')
    elif len(sys.argv)!=1:raise SystemExit('No command/path passthrough')
    execute()
