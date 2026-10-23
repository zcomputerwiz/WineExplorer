#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""ONE pinned upstream snapshot cache; data inspection only, never browser execution."""
import argparse
import base64
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import struct
import subprocess
import time
import urllib.request
import zipfile

VERSION='156.0.8078.0'
REVISION='7d9824099030f5c28c939ba08654562faaad7c09'
GENERATION='1790650091108450'
BYTES=358598977
MD5='cdyiojrRPrbsty2zyhfLXg=='
META='https://storage.googleapis.com/storage/v1/b/chromium-browser-snapshots/o/Win_x64%2F1706509%2Fchrome-win.zip'
MEDIA='https://storage.googleapis.com/download/storage/v1/b/chromium-browser-snapshots/o/Win_x64%2F1706509%2Fchrome-win.zip?generation='+GENERATION+'&alt=media'
REVISIONS='https://commondatastorage.googleapis.com/chromium-browser-snapshots/Win_x64/1706509/REVISIONS'
SOURCE_VERSION='https://chromium.googlesource.com/chromium/src/+/'+REVISION+'/chrome/VERSION?format=TEXT'
MAX_MEMBERS, MAX_TOTAL, MAX_MEMBER=5000,3*1024**3,1024**3
REQUIRED=('chrome.exe','chrome.dll','chrome_elf.dll','icudtl.dat','resources.pak','locales/en-US.pak','v8_context_snapshot.bin')


def digest(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream,'sha256').hexdigest()


def save(path,value):
    path.write_text(json.dumps(value,indent=2,allow_nan=False)+'\n')


class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self,*args,**kwargs): raise ValueError('Redirect forbidden for pinned acquisition')


def fetch(url,path,maximum,*,expected=None,md5=None,timeout=300,now=time.monotonic,open_url=None):
    if path.exists() or path.is_symlink(): raise ValueError('Fresh download path required')
    deadline=now()+timeout
    opener=open_url or urllib.request.build_opener(NoRedirect()).open
    count=0; sha=hashlib.sha256(); checksum=hashlib.md5()
    with opener(url,timeout=min(15,timeout)) as response, path.open('xb') as stream:
        if response.geturl()!=url or response.status!=200: raise ValueError('Wrong URL/status')
        header=response.headers.get('Content-Length')
        if header is not None and (not header.isdecimal() or int(header)>maximum or
                (expected is not None and int(header)!=expected)):
            raise ValueError('Unexpected Content-Length')
        while True:
            if now()>=deadline: raise TimeoutError('Acquisition deadline')
            data=response.read(min(1024**2,maximum-count+1))
            if now()>=deadline: raise TimeoutError('Acquisition deadline')
            if not data: break
            count+=len(data)
            if count>maximum: raise ValueError('Oversize response')
            stream.write(data);sha.update(data);checksum.update(data)
    if expected is not None and count!=expected: raise ValueError('Short response')
    measured=base64.b64encode(checksum.digest()).decode()
    if md5 is not None and measured!=md5: raise ValueError('Published MD5 mismatch')
    return dict(url=url,final_url=url,bytes=count,sha256=sha.hexdigest(),md5_base64=measured)


def metadata(meta,revisions,version):
    if (meta.get('bucket')!='chromium-browser-snapshots' or meta.get('name')!='Win_x64/1706509/chrome-win.zip'
            or meta.get('generation')!=GENERATION or meta.get('size')!=str(BYTES)
            or meta.get('md5Hash')!=MD5 or meta.get('mediaLink')!=MEDIA
            or revisions.get('got_revision')!=REVISION or revisions.get('chromium_revision')!='1706509'
            or revisions.get('got_revision_cp')!='refs/heads/main@{#1706509}'
            or base64.b64decode(version,validate=True)!=b'MAJOR=156\nMINOR=0\nBUILD=8078\nPATCH=0\n'):
        raise ValueError('Pinned catalog/generation/revision/version mismatch')


def zip_inventory(archive):
    entries=archive.infolist()
    if not entries or len(entries)>MAX_MEMBERS: raise ValueError('Archive member bound')
    seen=set(); kinds={}; total=0
    for item in entries:
        name=item.filename; stripped=name.rstrip('/')
        parts=stripped.split('/')
        if (not name or item.orig_filename!=name or '\\' in name or '\x00' in name or ':' in name or name.startswith('/')
                or any(p in ('','.','..') or p[-1:] in (' ','.') or
                    re.fullmatch(r'(?i:CON|PRN|AUX|NUL|COM[1-9¹²³]|LPT[1-9¹²³])(?:\..*)?',p) or
                    any(ord(c)<32 or c in '<>"|?*' for c in p) for p in parts)
                or parts[0]!='chrome-win' or PurePosixPath(stripped).as_posix()!=stripped):
            raise ValueError('Noncanonical/unsafe ZIP name')
        folded=stripped.casefold()
        if folded in seen: raise ValueError('Windows casefold collision')
        seen.add(folded)
        mode=item.external_attr>>16
        if stat.S_IFMT(mode) not in (0,stat.S_IFREG,stat.S_IFDIR) or item.flag_bits&1:
            raise ValueError('ZIP link/special/encrypted member')
        if stat.S_ISDIR(mode) and not item.is_dir(): raise ValueError('Directory type mismatch')
        if item.is_dir() and item.file_size: raise ValueError('Directory with data')
        if item.file_size<0 or item.file_size>MAX_MEMBER: raise ValueError('Individual extraction bound')
        total+=item.file_size
        if total>MAX_TOTAL: raise ValueError('Total extraction bound')
        kinds[folded]=item.is_dir()
    for name in kinds:
        for ancestor in PurePosixPath(name).parents:
            if str(ancestor) in kinds and not kinds[str(ancestor)]: raise ValueError('File ancestor')
    for name in REQUIRED:
        if kinds.get('chrome-win/'+name.casefold()) is not False: raise ValueError('Missing required runtime file: '+name)
    return entries,total


def extract(zip_path,target,*,now=time.monotonic,timeout=120,free=None):
    if target.exists() or target.is_symlink(): raise ValueError('Fresh extraction output required')
    deadline=now()+timeout
    with zipfile.ZipFile(zip_path) as archive:
        entries,total=zip_inventory(archive)  # ALL names before any extraction
        available=shutil.disk_usage(target.parent).free if free is None else free
        if available<total+2*1024**3: raise ValueError('Insufficient extraction reserve')
        target.mkdir()
        records={}
        for item in entries:
            if now()>=deadline: raise TimeoutError('Extraction deadline')
            path=target/item.filename
            if item.is_dir(): path.mkdir(parents=True,exist_ok=True);continue
            path.parent.mkdir(parents=True,exist_ok=True)
            count=0; sha=hashlib.sha256()
            with archive.open(item) as source,path.open('xb') as out:
                while data:=source.read(1024**2):
                    if now()>=deadline: raise TimeoutError('Extraction deadline')
                    count+=len(data)
                    if count>item.file_size: raise ValueError('Member exceeded declared size')
                    out.write(data);sha.update(data)
            if count!=item.file_size: raise ValueError('Truncated archive member')
            records[item.filename]={'bytes':count,'sha256':sha.hexdigest()}
    return records


def pe(path):
    with path.open('rb') as stream:
        head=stream.read(64)
        if len(head)!=64 or head[:2]!=b'MZ': raise ValueError('Invalid DOS header')
        off=struct.unpack_from('<I',head,60)[0]
        if not 64<=off<=path.stat().st_size-24: raise ValueError('Invalid PE offset')
        stream.seek(off);header=stream.read(24)
    if header[:4]!=b'PE\0\0' or struct.unpack_from('<H',header,4)[0]!=0x8664:
        raise ValueError('Runtime PE not AMD64')
    return {'machine':0x8664,'sha256':digest(path)}


def version_records(records,expected_paths):
    if (not isinstance(records,list) or len(records)!=3 or len(expected_paths)!=3
            or len(set(expected_paths))!=3): raise ValueError('Version resource result schema')
    for record,path in zip(records,expected_paths):
        if (not isinstance(record,dict) or set(record)!= {'path','file','product'}
                or any(not isinstance(record[k],str) for k in record)
                or record['path']!=path or record['file']!=VERSION
                or record['product'].split(' ')[0]!=VERSION):
            raise ValueError('Actual binary version/path mismatch: '+repr(records))
    return records


def file_versions(root,*,now=time.monotonic):
    # Host read-only version resource API, not browser execution or a PE parser.
    deadline=now()+20
    def bound():
        left=deadline-now()
        if left<=0: raise TimeoutError('Complete version inspection deadline')
        return left
    paths=[str(root/name) for name in ('chrome.exe','chrome.dll','chrome_elf.dll')]
    win=[subprocess.check_output(['wslpath','-w',p],text=True,timeout=bound()).strip() for p in paths]
    if any(not re.fullmatch(r'D:\\[^\x00\r\n]+',p) for p in win) or len(set(win))!=3:
        raise ValueError('Distinct expected D: version-resource paths required')
    quoted=','.join("'"+p.replace("'","''")+"'" for p in win)
    script="@("+quoted+") | ForEach-Object {$v=[Diagnostics.FileVersionInfo]::GetVersionInfo($_); [ordered]@{path=$_;file=$v.FileVersion;product=$v.ProductVersion}} | ConvertTo-Json"
    result=subprocess.run(['pwsh.exe','-NoProfile','-Command',script],capture_output=True,text=True,timeout=bound(),check=True)
    records=json.loads(result.stdout)
    bound()
    return version_records(records,win)


def retained_notices(root):
    records=[str(p.relative_to(root.parent)) for p in root.rglob('*')
             if p.is_file() and re.search('license|credits|notice',p.name,re.I)]
    if not records: raise ValueError('No retained license/notices')
    return records


def acquire(output):
    lab=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')
    if output.parent!=lab or output.exists() or output.is_symlink(): raise ValueError('Fresh direct D: lab cache required')
    if shutil.disk_usage(lab).free<8*1024**3: raise ValueError('Host cache reserve')
    output.mkdir(); record={'status':'FAILED_PRESERVED','version':VERSION,'revision':REVISION,'generation':GENERATION}
    try:
        record['responses']={}
        for name,url in (('metadata.json',META),('REVISIONS',REVISIONS),('VERSION.base64',SOURCE_VERSION)):
            record['responses'][name]=fetch(url,output/name,65536,timeout=30)
        metadata(json.loads((output/'metadata.json').read_text()),json.loads((output/'REVISIONS').read_text()),(output/'VERSION.base64').read_bytes())
        record['payload']=fetch(MEDIA,output/'chrome-win.zip',BYTES,expected=BYTES,md5=MD5)
        record['files']=extract(output/'chrome-win.zip',output/'runtime')
        root=output/'runtime/chrome-win'
        record['pe']={name:pe(root/name) for name in ('chrome.exe','chrome.dll','chrome_elf.dll','D3DCompiler_47.dll','libEGL.dll','libGLESv2.dll')}
        record['versions']=file_versions(root)
        record['notices']=retained_notices(root)
        record['status']='CACHE_VERIFIED_NOT_EXECUTED'
    except Exception as error:
        record['error']=repr(error);raise
    finally: save(output/'acquisition.json',record)
    return record


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output',type=Path)
    print(json.dumps(acquire(parser.parse_args().output),indent=2))
