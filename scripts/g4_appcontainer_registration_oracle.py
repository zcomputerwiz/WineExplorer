# SPDX-License-Identifier: GPL-2.0-or-later
"""Original Windows registration contract probe. Default help never loads APIs.

Only --run mutates registrations, for two internally generated identities.
--child-read accepts only these prefixed names on stdin and NEVER registers.
No profile, token, ACL, privilege, browser or VM operations.
"""
import argparse
import base64
import ctypes as C
import datetime
import json
import os
from pathlib import Path
import re
import secrets
import subprocess
import sys
import time

PREFIX='WexGraphicsOracle.'
NAME=re.compile(r'WexGraphicsOracle\.[0-9a-f]{32}\Z')
ABSENT={0x80070002,0x80070003,0x80070490} # Win32 FILE/PATH/NOT_FOUND, not permission failures
HRESULT=C.c_int32
PTR=C.c_void_p
SIGNATURES={
    'AppContainerRegisterSid':([PTR,C.c_wchar_p,C.c_wchar_p],HRESULT),
    'AppContainerUnregisterSid':([PTR],HRESULT),
    'AppContainerLookupMoniker':([PTR,C.POINTER(PTR)],HRESULT),
    'AppContainerFreeMemory':([PTR],None),
    'DeriveAppContainerSidFromAppContainerName':([C.c_wchar_p,C.POINTER(PTR)],HRESULT),
    'FreeSid':([PTR],PTR),
    'ConvertSidToStringSidW':([PTR,C.POINTER(PTR)],C.c_int32),
    'LocalFree':([PTR],PTR),
}

def success(record):return record['hresult']<0x80000000
def absent(record):return record['hresult'] in ABSENT and record['output_nonnull'] is False

class ApiUnavailable(RuntimeError):
    def __init__(self,exports):super().__init__('Unavailable required exports');self.exports=exports

class NativeApi:
    def __init__(self,loader=None,set_error=None,get_error=None):
        if loader is None:
            if os.name!='nt':raise RuntimeError('Native Windows only; never invoke Wine')
            loader=lambda n:C.WinDLL(n,use_last_error=True)
        self.set_error=set_error or C.set_last_error
        self.get_error=get_error or C.get_last_error
        self.events=[];self.sids=[];self.functions={};self.exports={}
        libraries={n:loader(n+'.dll') for n in ('kernelbase','kernel32','userenv','advapi32')}
        for name,(args,restype) in SIGNATURES.items():
            library=('kernelbase' if name.startswith('AppContainer') else 'userenv' if name.startswith('Derive') else 'kernel32' if name=='LocalFree' else 'advapi32')
            fn=getattr(libraries[library],name,None);self.exports[name]=fn is not None
            if fn is not None:fn.argtypes=args;fn.restype=restype;self.functions[name]=fn
        if not all(self.exports.values()):raise ApiUnavailable(self.exports)

    def call(self,name,*args):
        self.set_error(0)
        value=self.functions[name](*args);last=self.get_error()
        record={'operation':name,'sequence':len(self.events),'monotonic_ns':time.monotonic_ns(),
                'last_error':last,'last_error_semantics':'separate observation; not HRESULT interpretation'}
        if SIGNATURES[name][1] is HRESULT:
            record.update(hresult=int(value)&0xffffffff,hresult_hex=f'0x{int(value)&0xffffffff:08x}')
        elif SIGNATURES[name][1] is None:record['return_kind']='void'
        else:record['return_null']=value is None or value==0
        self.events.append(record);return value,record

    def derive(self,name):
        out=PTR();_,record=self.call('DeriveAppContainerSidFromAppContainerName',name,C.byref(out))
        record.update(moniker=name,output_nonnull=bool(out.value),deallocator='FreeSid')
        if not success(record) or not out.value:raise RuntimeError('Derive failed: '+record['hresult_hex'])
        self.sids.append(out.value) # tracked BEFORE conversion, including conversion-error cleanup
        string=PTR();value,conversion=self.call('ConvertSidToStringSidW',out,C.byref(string))
        if not value or not string.value:raise RuntimeError('SID string conversion failed')
        try:record['sid']=C.wstring_at(string.value)
        finally:
            result,_=self.call('LocalFree',string)
            if result:raise RuntimeError('SID string LocalFree failed')
        return out.value

    def lookup(self,sid):
        out=PTR();_,record=self.call('AppContainerLookupMoniker',sid,C.byref(out))
        record.update(output_nonnull=bool(out.value),text=None,deallocator=None)
        if success(record):
            if not out.value:raise RuntimeError('Successful lookup returned NULL')
            try:
                record['text']=C.wstring_at(out.value)
                if len(record['text'])>1024:raise ValueError('Lookup string quota')
            finally:
                self.call('AppContainerFreeMemory',out)
                record.update(deallocator='AppContainerFreeMemory',freed=True)
        elif out.value:
            # Failure outputs have no established ownership contract; never free arbitrary pointers.
            raise RuntimeError('Failed lookup returned unexpected non-NULL output')
        return record

    def register(self,sid,name,display):return self.call('AppContainerRegisterSid',sid,name,display)[1]
    def unregister(self,sid):return self.call('AppContainerUnregisterSid',sid)[1]
    def close(self):
        errors=[]
        for sid in self.sids:
            try:
                value,_=self.call('FreeSid',sid)
                if value:raise RuntimeError('FreeSid failed')
            except Exception as error:errors.append(repr(error))
        self.sids.clear()
        if errors:raise RuntimeError(';'.join(errors))

def child_main(api_factory=NativeApi):
    raw=sys.stdin.buffer.read(1025)
    if len(raw)>1024:raise ValueError('Child request quota')
    request=json.loads(raw)
    names=request.get('monikers') if type(request) is dict and set(request)=={'monikers'} else None
    if (type(names) is not list or len(names)!=2 or any(type(n) is not str or not NAME.fullmatch(n) for n in names) or len(set(names))!=2):
        raise ValueError('Two distinct original oracle names only')
    api=api_factory();value={'v':1,'pid':os.getpid(),'read_only':True,'observations':[],'events':api.events}
    try:
        for name in names:value['observations'].append({'moniker':name,'lookup':api.lookup(api.derive(name))})
    except Exception as error:value['error']=repr(error)
    finally:
        try:api.close()
        except Exception as error:value['cleanup_error']=repr(error)
    print(json.dumps(value,allow_nan=False));return 1 if 'error' in value or 'cleanup_error' in value else 0

class ChildError(RuntimeError):
    def __init__(self,message,record):super().__init__(message);self.record=record

def child_read(names,deadline,*,popen=subprocess.Popen,now=time.monotonic):
    record={'argv':[sys.executable,'-B',str(Path(__file__).resolve()),'--child-read'],'reaped':False}
    child=None
    try:
        timeout=min(10,deadline-now()-5)
        if timeout<=0:raise TimeoutError('No child/reap budget')
        child=popen(record['argv'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        record['pid']=child.pid
        timeout=min(10,deadline-now()-5)
        if timeout<=0:raise TimeoutError('No child/reap budget after process creation')
        stdout,stderr=child.communicate(json.dumps({'monikers':names}).encode(),timeout=timeout)
        record.update(exit=child.returncode,reaped=True)
        record.update(stdout_base64=base64.b64encode(stdout[:65536]).decode(),stderr_base64=base64.b64encode(stderr[:8192]).decode(),
                      stdout_bytes=len(stdout),stderr_bytes=len(stderr))
        if len(stdout)>65536 or len(stderr)>8192:raise ValueError('Child output quota')
        if child.returncode:raise RuntimeError('Read-only child failed')
        def unique(pairs):
            v={}
            for k,x in pairs:
                if k in v:raise ValueError('Duplicate child JSON key')
                v[k]=x
            return v
        record['result']=json.loads(stdout,object_pairs_hook=unique)
        return record
    except Exception as error:
        record['error']=repr(error)
        if child is not None and not record['reaped']:
            try:
                if child.poll() is None:child.kill();record['killed']=True
                stdout,stderr=child.communicate(timeout=5)
                record.update(reaped=True,exit=child.returncode,stdout_base64=base64.b64encode(stdout[:65536]).decode(),stderr_base64=base64.b64encode(stderr[:8192]).decode())
            except Exception as secondary:record['cleanup_error']=repr(secondary)
        raise ChildError(str(error),record) from error

def experiment(api,result,*,reader=child_read,now=time.monotonic,name_factory=lambda:PREFIX+secrets.token_hex(16)):
    deadline=now()+90;owned={};sids={};names=[]
    result.update(events=api.events,identities=names,cleanup=[],exports=api.exports)
    def step(label,fn):
        if now()>=deadline:raise TimeoutError('Oracle90s deadline')
        value=fn();result.setdefault('order',[]).append({'step':label,'record':value});return value
    def missing(label,sid):
        value=step(label,lambda:api.lookup(sid))
        if not absent(value):raise RuntimeError('Collision or unknown lookup failure: '+label)
    def lookup(label,name):
        value=step(label,lambda:api.lookup(sids[name]))
        if not success(value) or value['text']!=name:raise RuntimeError('Lookup did not return owned moniker: '+label)
    try:
        names.extend([name_factory(),name_factory()])
        if len(set(names))!=2 or any(not NAME.fullmatch(n) for n in names):raise ValueError('Fresh unique fixed-prefix identities required')
        for name in names:sids[name]=step('derive',lambda n=name:api.derive(n))
        for name in names:missing('initial_missing',sids[name]) # BOTH checked before mutation
        a,b=names
        value=step('register_a',lambda:api.register(sids[a],a,a+' display'))
        if not success(value):raise RuntimeError('Fresh registration A failed: '+value['hresult_hex'])
        owned[a]=sids[a]
        lookup('lookup_a',a)
        step('duplicate_a',lambda:api.register(sids[a],a,a+' changed display'))
        lookup('lookup_a_after_duplicate',a);missing('b_still_missing',sids[b])
        value=step('register_b',lambda:api.register(sids[b],b,b+' display'))
        if not success(value):raise RuntimeError('Fresh registration B failed: '+value['hresult_hex'])
        owned[b]=sids[b];lookup('lookup_b',b);lookup('a_isolated',a)
        result['child']=reader(names,deadline)
        value=result['child']['result']
        if (result['child'].get('reaped') is not True or result['child'].get('exit')!=0
                or value.get('read_only') is not True or type(value.get('pid')) is not int
                or value['pid']!=result['child'].get('pid') or value['pid'] in (0,os.getpid())
                or value.get('error') or value.get('cleanup_error') or len(value.get('observations',[]))!=2):raise RuntimeError('Invalid independent child observation')
        for name,item in zip(names,value['observations']):
            if item.get('moniker')!=name or not success(item['lookup']) or item['lookup']['text']!=name:raise RuntimeError('Child persistence mismatch')
        value=step('unregister_a',lambda:api.unregister(sids[a]))
        if not success(value):raise RuntimeError('Unregister A failed')
        missing('a_missing_after_unregister',sids[a]);del owned[a]
        step('second_unregister_a',lambda:api.unregister(sids[a]));lookup('b_survives_a_unregister',b)
        result['sequence_complete']=True
    except Exception as error:
        result['error']=repr(error)
        if isinstance(error,ChildError):result['child']=error.record
    finally:
        for name,sid in owned.items():
            record={'moniker':name}
            try:
                record['unregister']=api.unregister(sid)
                record['lookup_after']=api.lookup(sid)
                record['absent']=success(record['unregister']) and absent(record['lookup_after'])
                if not record['absent']:raise RuntimeError('Owned registration cleanup not confirmed')
            except Exception as error:record['error']=repr(error)
            result['cleanup'].append(record)
        try:api.close()
        except Exception as error:result['allocator_cleanup_error']=repr(error)
        result['cleanup_complete']=all(v.get('absent') is True for v in result['cleanup']) and 'allocator_cleanup_error' not in result
        result['completed']=result.get('sequence_complete') is True and result['cleanup_complete'] and 'error' not in result
    return result

def output_path(value):
    path=Path(value)
    if os.name!='nt' or not path.is_absolute() or path.drive.upper()!='D:' or path.suffix.lower()!='.json':raise ValueError('Fresh absolute D: JSON path required on native Windows')
    if path.parent.resolve(strict=True)!=path.parent or path.is_symlink() or os.path.lexists(path):raise ValueError('Canonical fresh output required')
    return path

def main(argv=None,*,api_factory=NativeApi):
    parser=argparse.ArgumentParser(description=__doc__)
    modes=parser.add_mutually_exclusive_group();modes.add_argument('--run',action='store_true');modes.add_argument('--child-read',action='store_true')
    parser.add_argument('--output')
    args=parser.parse_args(argv)
    if args.child_read:
        if args.output:parser.error('Child is read-only stdout only')
        return child_main(api_factory)
    if not args.run:
        parser.print_help();return 0
    if not args.output:parser.error('--run requires fresh --output D: JSON')
    path=output_path(args.output)
    # Lease output BEFORE DLL loading/mutation; never replace historical evidence.
    with path.open('x',encoding='utf-8') as stream:
        result={'v':1,'pid':os.getpid(),'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),
            'windows_version':list(sys.getwindowsversion()),'pointer_bits':C.sizeof(PTR)*8,
            'classification':'Native registration metadata ONLY; no token/ACL/sandbox/browser proof'}
        stream.write(json.dumps(result)+'\n');stream.flush()
        try:experiment(api_factory(),result)
        except Exception as error:
            result['error']=repr(error);result['completed']=False
            if isinstance(error,ApiUnavailable):result['exports']=error.exports
        finally:
            stream.seek(0);stream.truncate();json.dump(result,stream,indent=2,allow_nan=False);stream.write('\n');stream.flush();os.fsync(stream.fileno())
    print(json.dumps({'output':str(path),'completed':result.get('completed',False),'cleanup_complete':result.get('cleanup_complete'),'error':result.get('error')}))
    return 0 if result.get('completed') else 1

if __name__=='__main__':raise SystemExit(main())
