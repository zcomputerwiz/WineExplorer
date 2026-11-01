#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Stage A ONLY: exact owned chain, bounded read-only GPT extraction/preparation.

No create/resize/boot/mount/attach dispatch exists. Stage B requires new authority.
512-byte sectors, GPT1.0/128x128 entries, known Debian root/BIOS/EFI only.
"""
import difflib
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import time
import uuid
import zlib

LAB=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022')
SYSTEM=LAB/'system.qcow2'
BASE=Path('/mnt/d/VirtualBox_VMs/WEX-G4-Acceleration-20261022/image-source/debian-13-generic-amd64.qcow2')
NEW=LAB/'system-capacity-r1-20261023.qcow2'
PREP=LAB/'capacity-r1-20261023.preflight'
REPO=Path('/mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer')
OLD_BYTES,NEW_BYTES=64*1024**3,80*1024**3
ROOT='624cfa91-935d-4cc7-9a3f-f29b41cac2a0'
# Exact architecture-specific root type observed in retained bounded GPT and
# approved by parent; generic Linux filesystem/other architecture NOT accepted.
TYPES={1:'4f68bce3-e8cd-4db1-96e7-fbcaf984b709',14:'21686148-6449-6e6f-744e-656564454649',15:'c12a7328-f81f-11d2-ba4b-00a0c93ec93b'}


def integer(v):return type(v) is int and 0<=v<2**64
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def save(p,v):p.write_text(json.dumps(v,indent=2,allow_nan=False)+'\n')
def file_id(p):
    if p.resolve(strict=True)!=p or not p.is_file():raise ValueError('Noncanonical/symlink image path')
    s=p.stat();return dict(path=str(p),device=s.st_dev,inode=s.st_ino,bytes=s.st_size,mtime_ns=s.st_mtime_ns,ctime_ns=s.st_ctime_ns)


def owners(paths,proc=Path('/proc')):
    targets={str(p) for p in paths};found=[];deadline=time.monotonic()+5
    for entry in proc.iterdir():
        if not entry.name.isdecimal():continue
        if time.monotonic()>deadline:raise TimeoutError('Bounded chain-open inventory')
        try:
            args=(entry/'cmdline').read_bytes().split(b'\0')
            if str(REPO/'build/g4-virgl-20261022/launch.py').encode() in args or (entry/'exe').resolve().name.startswith('qemu-system'):
                found.append({'pid':int(entry.name),'reason':'launcher/QEMU present'})
            for fd in (entry/'fd').iterdir():
                if time.monotonic()>deadline:raise TimeoutError('Bounded chain-open inventory')
                try:
                    if os.readlink(fd) in targets:found.append({'pid':int(entry.name),'fd':fd.name,'reason':'open chain file'})
                except FileNotFoundError:pass
        except (FileNotFoundError,ProcessLookupError):pass
    if found:raise ValueError('Active owner/open chain: '+repr(found))
    return {'no_launcher_qemu_or_open_chain_observed':True,'note':'Snapshot plus normal QEMU input locks; not a permanent ownership lock'}


def validate_chain(rows):
    if type(rows) is not list or len(rows)!=2:raise ValueError('Exact two-layer original chain required')
    for r,p,size in zip(rows,(SYSTEM,BASE),(OLD_BYTES,3*1024**3)):
        if (r.get('filename')!=str(p) or r.get('format')!='qcow2' or type(r.get('virtual-size')) is not int
            or r['virtual-size']!=size or r.get('dirty-flag') is not False
            or r.get('format-specific',{}).get('data',{}).get('corrupt') is not False):raise ValueError('Changed/unsupported chain')
        file_id(p)
    if (rows[0].get('full-backing-filename')!=str(BASE) or rows[0].get('backing-filename')!=str(BASE)
        or rows[0].get('backing-filename-format')!='qcow2' or rows[1].get('backing-filename')):raise ValueError('Unexpected backing path/format')
    return rows


def layout(primary,backup,capacity=OLD_BYTES,new_capacity=NEW_BYTES):
    if (not integer(capacity) or not integer(new_capacity) or capacity%512 or new_capacity%512
        or capacity<34*512 or new_capacity<=capacity or len(primary)!=34*512 or len(backup)!=33*512):raise ValueError('Sector/capacity/extraction bounds')
    sectors=capacity//512
    if primary[510:512]!=b'\x55\xaa':raise ValueError('Protective MBR signature')
    mbr=[primary[446+i*16:462+i*16] for i in range(4)]
    if (mbr[0][4]!=0xee or struct.unpack_from('<II',mbr[0],8)!=(1,min(sectors-1,0xffffffff))
        or any(any(p) for p in mbr[1:])):raise ValueError('Protective MBR layout/hybrid unsupported')
    def header(raw,current,other):
        sig,rev,size,crc,res,at,alt,first,last,guid,entries,count,width,array_crc=struct.unpack_from('<8sIIIIQQQQ16sQIII',raw)
        check=bytearray(raw[:92]);check[16:20]=b'\0'*4
        if (sig!=b'EFI PART' or rev!=0x10000 or size!=92 or res or zlib.crc32(check)!=crc
            or at!=current or alt!=other or not 34<=first<=last<=sectors-34
            or guid==b'\0'*16 or count!=128 or width!=128):raise ValueError('GPT header/signature/CRC/reciprocal/bounds/schema')
        return dict(first=first,last=last,guid=guid,entries=entries,array_crc=array_crc)
    a=header(primary[512:1024],1,sectors-1);b=header(backup[-512:],sectors-1,1)
    if a['entries']!=2 or b['entries']!=sectors-33 or any(a[k]!=b[k] for k in ('first','last','guid','array_crc')):raise ValueError('Primary/backup layout disagreement')
    array=primary[1024:]
    if array!=backup[:-512] or zlib.crc32(array)!=a['array_crc']:raise ValueError('Partition-array CRC/backup mismatch')
    parts=[];guids=set()
    for i in range(128):
        entry=array[i*128:(i+1)*128]
        if entry[:16]==b'\0'*16:continue  # unused entries do NOT duplicate GUID0
        kind,uid,start,end,flags,name=struct.unpack('<16s16sQQQ72s',entry)
        if uid==b'\0'*16 or uid in guids or not a['first']<=start<=end<=a['last']:raise ValueError('Partition GUID/extent bounds')
        guids.add(uid)
        parts.append(dict(number=i+1,type=str(uuid.UUID(bytes_le=kind)),guid=str(uuid.UUID(bytes_le=uid)),
            start_lba=start,end_lba=end,sectors=end-start+1,attributes=flags,name=name.decode('utf-16-le').rstrip('\0')))
    ordered=sorted(parts,key=lambda p:p['start_lba'])
    if any(x['end_lba']>=y['start_lba'] for x,y in zip(ordered,ordered[1:])):raise ValueError('Overlapping partitions')
    if {p['number']:p['type'] for p in parts}!=TYPES:raise ValueError('Unknown/ambiguous Debian partition layout')
    root=next(p for p in parts if p['number']==1)
    if root['guid']!=ROOT or ordered[-1]!=root:raise ValueError('Root identity/not physically last')
    new_last=new_capacity//512-34
    if new_last<=root['end_lba']:raise ValueError('No expandable trailing extent')
    return dict(sector_bytes=512,virtual_bytes=capacity,new_virtual_bytes=new_capacity,disk_guid=str(uuid.UUID(bytes_le=a['guid'])),
        first_usable_lba=a['first'],last_usable_lba=a['last'],partitions=parts,root=root,
        projected_new_last_usable_lba=new_last,additional_root_sectors=new_last-root['end_lba'],
        gate='GPT layout proof ONLY; ext4 identity from prior serial; Stage B automatic growth NOT executed')


def extraction_commands(out):
    # Installed dd has NO skip. Raw offset filter reads ONLY last33 virtual
    # sectors through a read-only qcow2 input, normal locks, no -U/force-share.
    return [
        ['qemu-img','dd','-f','qcow2','-O','raw','bs=512','count=34','if='+str(SYSTEM),'of='+str(out/'primary.bin')],
        ['qemu-img','dd','--image-opts','-O','raw','bs=512','count=33',
         'if=driver=raw,offset='+str(OLD_BYTES-33*512)+',file.driver=qcow2,file.file.filename='+str(SYSTEM),'of='+str(out/'backup.bin')]]


def creation_result(exit_code,exists,info):
    """Future Stage B outcome gate, tested with fakes; NEVER dispatches create."""
    if type(exit_code) is not int or exit_code or not exists:raise ValueError('Creation failed/partial; preserve output, no retry/delete')
    if (info.get('filename')!=str(NEW) or info.get('format')!='qcow2' or type(info.get('virtual-size')) is not int
        or info['virtual-size']!=NEW_BYTES or info.get('full-backing-filename')!=str(SYSTEM)
        or info.get('backing-filename-format')!='qcow2'):raise ValueError('Created overlay identity/size/backing mismatch')
    return True


def prepare():
    if PREP.exists() or PREP.is_symlink() or NEW.exists() or NEW.is_symlink():raise ValueError('Refuse existing prep/new overlay')
    if PREP.parent!=LAB or NEW.parent!=LAB or LAB.resolve()!=LAB:raise ValueError('Exact owned D paths required')
    for name in ('qemu-img','qemu-io'):
        if not shutil.which(name):raise ValueError('Missing required host tool '+name)
    if shutil.disk_usage(LAB).free<NEW_BYTES:raise ValueError('Host reserve must cover full80GiB allocation')
    before=[file_id(p) for p in (SYSTEM,BASE)];idle=owners((SYSTEM,BASE))
    PREP.mkdir(exist_ok=False);record={'status':'STAGE_A_IN_PROGRESS','commands':[],'original_file_identities':before,'owner_snapshot':idle}
    def read_command(argv,label):
        record['commands'].append({'argv':argv,'timeout_seconds':30})
        with (PREP/(label+'.stdout.log')).open('wb') as out,(PREP/(label+'.stderr.log')).open('wb') as err:
            done=subprocess.run(argv,stdout=out,stderr=err,timeout=30)
        record['commands'][-1]['exit']=done.returncode
        if done.returncode:raise RuntimeError('Read-only metadata operation failed: '+label)
        return (PREP/(label+'.stdout.log')).read_text()
    try:
        read_command(['qemu-img','dd','--help'],'dd-help');read_command(['qemu-io','--help'],'io-help')
        info=validate_chain(json.loads(read_command(['qemu-img','info','--backing-chain','--output=json',str(SYSTEM)],'chain-before')))
        for argv,label in zip(extraction_commands(PREP),('primary','backup')):read_command(argv,label)
        for name,size in (('primary.bin',34*512),('backup.bin',33*512)):
            if (PREP/name).stat().st_size!=size:raise ValueError('Unexpected metadata extraction size')
        record['layout']=layout((PREP/'primary.bin').read_bytes(),(PREP/'backup.bin').read_bytes())
        after=validate_chain(json.loads(read_command(['qemu-img','info','--backing-chain','--output=json',str(SYSTEM)],'chain-after')))
        if info!=after or before!=[file_id(p) for p in (SYSTEM,BASE)]:raise ValueError('Chain changed during reads')
        owners((SYSTEM,BASE));record['chain']=info
        launcher=REPO/'build/g4-virgl-20261022/launch.py';old=launcher.read_text()
        needle="str(root / 'system.qcow2')"
        if old.count(needle)!=1:raise ValueError('Exact launcher target ambiguous')
        proposed=old.replace(needle,"str(root / 'system-capacity-r1-20261023.qcow2')")
        record.update(status='NOT_READY_FOR_STAGE_B',host_free_bytes=shutil.disk_usage(LAB).free,
            incomplete_gates=['Exact19 binary expected values/remote hash argv not yet inventoried','Windows boot command conversion not yet implemented; command below is TEMPLATE ONLY','Full original-chain pre/post content hashes not measured'],
            original_content_sha256='NOT_MEASURED; MANDATORY full-chain pre/post content hash gate BEFORE/AFTER Stage B mutation',
            metadata_sha256={n:sha(PREP/n) for n in ('primary.bin','backup.bin')},
            create_argv=['qemu-img','create','-f','qcow2','-F','qcow2','-b',str(SYSTEM),str(NEW),str(NEW_BYTES)],
            inspect_new_argv=['qemu-img','info','--backing-chain','--output=json',str(NEW)],
            launcher={'path':str(launcher),'before_sha256':sha(launcher),'proposed_sha256':hashlib.sha256(proposed.encode()).hexdigest(),
                'proposed_diff':''.join(difflib.unified_diff(old.splitlines(True),proposed.splitlines(True))), 'mutated':False},
            boot_command='pwsh -NoProfile -File '+str(REPO/'scripts/g4-virgl-launcher-session.ps1')+' -RunName capacity-maintenance-r1-20261023',
            stop_command=['python3','-B',str(REPO/'scripts/stop_g4_virgl_launcher.py'),'--identity',str(LAB/'capacity-maintenance-r1-20261023/launcher-identity.json'),'--timeout','60'],
            future_checks={'before_remote':'Exact recorded QEMU -drive must name NEW; rawUTC650/435 guards; source/chain hashes; no Wine/browser',
                'remote_argvs':[['lsblk','--json','-b','-o','NAME,TYPE,SIZE,FSTYPE,UUID,PARTUUID,START'],['findmnt','--json','/'],['sudo','-n','sfdisk','--json','/dev/vda'],['sudo','-n','sfdisk','--dump','/dev/vda'],['df','-B1','/'],['sudo','-n','dumpe2fs','-h','/dev/vda1'],['cloud-init','status','--long']],
                'after':'Root ext4/vda1 UUID/start unchanged; other extents unchanged; growth+>=8GiB free;19 pinned binaries/compositor; full original-chain content hashes unchanged; graceful QMP/QEMU/Xvfb/Windows0',
                'automatic_growth':'FIRST writable overlay boot is expansion; no manual repair/resize/fallback',
                'bounds':'One900s launcher,90s shutdown reserve; remote total<=120s, each<=15s; stop on mismatch'},
            rollback='Only while all off: select old base loses all overlay-only changes; NEVER commit/rebase/downward merge')
    except Exception as error:record.update(status='FAILED_PRESERVED',error=repr(error));raise
    finally:save(PREP/'preflight.json',record)
    (PREP/'NOT_READY_FOR_STAGE_B').write_text('Stage A ONLY; execution manifest incomplete. No disk created/resized/booted/launcher changed. Parent review required.\n')
    print(json.dumps(record['layout'],indent=2))


if __name__=='__main__':prepare()
