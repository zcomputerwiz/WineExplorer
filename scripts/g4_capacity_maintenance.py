#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact OFFLINE preparation/read-only gates. Never creates images or boots.

Stage B commands are recorded, not dispatched. No arbitrary image/VM selection.
"""
import argparse
import difflib
import hashlib
import json
import math
from pathlib import Path
import shutil
import subprocess
import time

import g4_capacity as c
import g4_capacity_guest as guest

PREP=c.LAB/'capacity-maintenance-r1-20261023.preflight'
RUN=c.LAB/'capacity-maintenance-r1-20261023'
ACCEPTED=c.LAB/'edge-headful-selected-stacks-r1-20261023.preflight/preflight.json'
ACCEPTED_SHA='437f88ed1f908081ba893d436cc7d9770650c5a37ee32d88cf20e136ebfc80db'
GPT_SHA={'primary.bin':'0e9d0adfe3e2f5d8aa168062b3f7fa1a258d776e426376b150d7ba29b7612f9f',
         'backup.bin':'15966b074969c8afd128357c8f277f6dc47a77106d791ae99714daed045e7f6e'}
FS_UUID='fe2c0409-10e4-4cd2-949b-a61b19c5a757'
LAUNCHER=c.REPO/'build/g4-virgl-20261022/launch.py'
INVENTORY=['scripts/g4_capacity.py','scripts/g4_capacity_maintenance.py','scripts/g4_capacity_guest.py',
    'scripts/g4-virgl-launcher-session.ps1','scripts/stop_g4_virgl_launcher.py','scripts/g4_virgl_budget.py',
    'scripts/check-g3-correctness.py','build/g4-virgl-20261022/launch.py']


def windows(path):
    prefix='/mnt/c/'
    if not str(path).startswith(prefix):raise ValueError('Exact C: source required')
    return 'C:\\'+str(path)[len(prefix):].replace('/','\\')


def hash_chain(expected=None,*,seconds=600,now=time.monotonic):
    if type(seconds) not in (int,float) or not math.isfinite(seconds) or not 0<seconds<=600:raise ValueError('Hash bound')
    start=now();deadline=start+seconds;c.owners((c.SYSTEM,c.BASE));before=[c.file_id(p) for p in (c.SYSTEM,c.BASE)];rows=[]
    for path,identity in zip((c.SYSTEM,c.BASE),before):
        h=hashlib.sha256();count=0
        with path.open('rb') as stream:
            while True:
                if now()>=deadline:raise TimeoutError('Whole original-chain hash deadline')
                data=stream.read(8*1024**2)
                if not data:break
                h.update(data);count+=len(data)
        if c.file_id(path)!=identity or count!=identity['bytes']:raise ValueError('Original changed while hashing')
        rows.append({'identity':identity,'sha256':h.hexdigest()})
    c.owners((c.SYSTEM,c.BASE))
    if before!=[c.file_id(p) for p in (c.SYSTEM,c.BASE)] or now()>deadline:raise ValueError('Original/hash deadline changed')
    if expected is not None and rows!=expected:raise ValueError('Original-chain content/identity mismatch')
    return {'files':rows,'elapsed_seconds':now()-start,'maximum_seconds':seconds,'note':'Offline full physical-file SHA256; owner snapshots/identity continuity, not an atomic cross-file snapshot'}


def proposal(old):
    needle=b"str(root / 'system.qcow2')"
    if old.count(needle)!=1:raise ValueError('Ambiguous original launcher')
    new=old.replace(needle,b"str(root / 'system-capacity-r1-20261023.qcow2')")
    return new,''.join(difflib.unified_diff(old.decode().splitlines(True),new.decode().splitlines(True),fromfile=str(LAUNCHER),tofile=str(LAUNCHER)))


def binary_pins():
    if c.sha(ACCEPTED)!=ACCEPTED_SHA:raise ValueError('Accepted19 provenance changed')
    record=json.loads(ACCEPTED.read_text());pins=record['known_binary_sha256']
    if set(pins)!=set(guest.BINARIES) or len(pins)!=19:raise ValueError('Exactly19 including Edge/labwc/tools required')
    return pins


def validate_new_chain(rows,original):
    if type(rows) is not list or len(rows)!=3:raise ValueError('Exact new3-layer chain required')
    c.creation_result(0,True,rows[0])
    if rows[0].get('backing-filename')!=str(c.SYSTEM) or rows[0].get('dirty-flag') is not False or rows[0].get('format-specific',{}).get('data',{}).get('corrupt') is not False:
        raise ValueError('New overlay backing/health mismatch')
    c.validate_chain(rows[1:])
    if rows[1:]!=original:raise ValueError('Original backing-chain metadata changed')
    return True


def validate_drive(argv):
    disks=[argv[i+1] for i,arg in enumerate(argv[:-1]) if arg=='-drive']
    expected=['file='+str(c.NEW)+',format=qcow2,if=virtio','file='+str(c.LAB/'seed.iso')+',format=raw,media=cdrom,readonly=on']
    if disks!=expected or any(arg in argv for arg in ('-hda','-hdb','-blockdev')):raise ValueError('Actual QEMU drive is not exact new overlay + read-only seed')
    return True


def validate_guest(r,manifest):
    rows=r.get('records')
    if (r.get('error') or type(rows) is not list or len(rows)!=len(guest.COMMANDS)
        or type(r.get('free_bytes')) is not int or r['free_bytes']<8*1024**3
        or type(r.get('elapsed_seconds')) not in (int,float) or not math.isfinite(r['elapsed_seconds']) or not 0<=r['elapsed_seconds']<=120):raise ValueError('Incomplete/unbounded guest probe/free space')
    text={}
    for row,(label,argv) in zip(rows,guest.COMMANDS):
        if row.get('label')!=label or row.get('argv')!=argv or type(row.get('exit')) is not int or row['exit'] or row.get('error') or row.get('cleanup_error') or row.get('reaped') is not True or row.get('within_deadline') is not True:
            raise ValueError('Read-only command identity/exit/deadline: '+label)
        if not isinstance(row.get('stdout'),str) or not isinstance(row.get('stderr'),str) or len((row['stdout']+row['stderr']).encode())>1024**2:raise ValueError('Guest evidence quota/schema')
        text[label]=row['stdout']
    table=json.loads(text['partition'])['partitiontable'];expected=manifest['layout']
    if table.get('device')!='/dev/vda' or table.get('label')!='gpt' or table.get('unit')!='sectors' or type(table.get('sectorsize')) is not int or table['sectorsize']!=512 or table.get('id','').lower()!=expected['disk_guid']:
        raise ValueError('Unexpected root disk/table')
    parts=table.get('partitions')
    if type(parts) is not list or len(parts)!=3:raise ValueError('Partition count')
    for old in expected['partitions']:
        found=[p for p in parts if p.get('node')=='/dev/vda'+str(old['number'])]
        if len(found)!=1:raise ValueError('Partition identity missing/ambiguous')
        p=found[0];end=expected['projected_new_last_usable_lba'] if old['number']==1 else old['end_lba']
        if (type(p.get('start')) is not int or type(p.get('size')) is not int or p['start']!=old['start_lba'] or p['size']!=end-old['start_lba']+1
            or p.get('type','').lower()!=old['type'] or p.get('uuid','').lower()!=old['guid'] or p.get('attrs','')!=''):raise ValueError('Partition extent/GUID/attributes changed or root not fully grown')
    mounts=json.loads(text['root']).get('filesystems')
    if type(mounts) is not list or len(mounts)!=1 or any(mounts[0].get(k)!=v for k,v in dict(source='/dev/vda1',fstype='ext4',uuid=FS_UUID,target='/').items()):raise ValueError('Root mount/FS UUID mismatch')
    disks=json.loads(text['lsblk'])['blockdevices'];disk=[d for d in disks if d.get('path')=='/dev/vda']
    if len(disk)!=1 or type(disk[0].get('size')) is not int or disk[0]['size']!=c.NEW_BYTES or disk[0].get('type')!='disk' or type(disk[0].get('log-sec')) is not int or disk[0]['log-sec']!=512:raise ValueError('Guest disk capacity/sector size')
    root=[d for d in disk[0].get('children',[]) if d.get('path')=='/dev/vda1']
    if len(root)!=1 or root[0].get('fstype')!='ext4' or root[0].get('uuid')!=FS_UUID or root[0].get('partuuid','').lower()!=c.ROOT:raise ValueError('Root filesystem/partition aliases')
    fields=dict(line.split(':',1) for line in text['filesystem'].splitlines() if ':' in line)
    if fields.get('Filesystem UUID','').strip()!=FS_UUID or fields.get('Filesystem state','').strip()!='clean':raise ValueError('Filesystem UUID/state')
    block=int(fields.get('Block size','0'));count=int(fields.get('Block count','0'))
    root_bytes=(expected['projected_new_last_usable_lba']-expected['root']['start_lba']+1)*512
    if block!=4096 or count<=16744443 or not 0<=root_bytes-block*count<16*1024**2:raise ValueError('Root filesystem has not grown to partition')
    cloud=json.loads(text['cloud_init'])
    if cloud.get('status')!='done' or cloud.get('errors')!=[] or cloud.get('recoverable_errors') not in ({},[]):raise ValueError('Cloud-init incomplete/error')
    if text['services'].strip()!='running':raise ValueError('Guest services degraded')
    observed={}
    for line in text['binaries'].splitlines():
        if len(line)<67 or line[64:66]!='  ' or line[66:] in observed:raise ValueError('Malformed/duplicate binary hash')
        observed[line[66:]]=line[:64]
    if observed!=manifest['known_binary_sha256']:raise ValueError('Accepted19 binary/compositor/Edge hash mismatch')
    return {'capacity_growth_verified':True,'free_bytes':r['free_bytes'],'root_fs_uuid':FS_UUID,'binary_count':19,'classification':'MAINTENANCE ONLY; no graphics/browser acceptance'}


def validate_shutdown(result,session,identity):
    if (result.get('stop_reason')!='signal' or result.get('graceful_powerdown') is not True
        or type(result.get('qemu_exit_status')) is not int or result['qemu_exit_status']!=0
        or result.get('cleanup')!={'qemu_stopped':True,'xvfb_stopped':True}
        or session.get('runName')!=RUN.name or session.get('state')!='exited'
        or type(session.get('windowsExitCode')) is not int or session['windowsExitCode']!=0):raise ValueError('QMP/QEMU/Windows shutdown not clean0')
    if any(result.get('launcher_identity',{}).get(k)!=identity.get(k) for k in ('pid','start_ticks','run_dir','identity_record')):raise ValueError('Final identity mismatch')
    return True


def validate_creation_record(record,r):
    if (type(record) is not dict or record.get('argv')!=r['create_argv'] or type(record.get('exit')) is not int or record['exit']!=0):
        raise ValueError('Creation exit/argv missing or failed; preserve partial output, no retry/delete/boot')


def stopped_processes(identity,query=None):
    if query is None:
        from stop_g4_virgl_launcher import proc_identity
        query=proc_identity
    rows={}
    for name,pid_key,ticks_key in (('launcher','pid','start_ticks'),('qemu','qemu_pid','qemu_start_ticks'),('xvfb','xvfb_pid','xvfb_start_ticks')):
        pid,ticks=identity.get(pid_key),identity.get(ticks_key)
        if type(pid) is not int or pid<=0 or type(ticks) is not int or ticks<=0:raise ValueError('Missing stopped process identity')
        try:actual=query(pid)
        except FileNotFoundError:rows[name]='absent';continue
        if actual['start_ticks']==ticks:raise ValueError('Owned '+name+' still present')
        rows[name]='recorded identity absent; PID reused'
    return rows


def prepare():
    if PREP.exists() or PREP.is_symlink() or RUN.exists() or c.NEW.exists() or c.NEW.is_symlink():raise ValueError('Existing prep/run/new overlay; preserve, no retry')
    if PREP.parent!=c.LAB or c.LAB.resolve()!=c.LAB or shutil.disk_usage(c.LAB).free<c.NEW_BYTES:raise ValueError('Exact D storage/80GiB host reserve required')
    for tool in ('qemu-img','qemu-io'):
        if shutil.which(tool)!='/usr/bin/'+tool:raise ValueError('Missing/nonowned installed tool')
    pins=binary_pins();retained={name:c.sha(c.PREP/name) for name in GPT_SHA}
    if retained!=GPT_SHA or c.sha(c.PREP/'preflight.json')!='c01f9f22f701d1e0fc348da4db2f553629310a4d1f1bdf6fe02e98003c6a6f59':raise ValueError('Retained GPT/failed provenance changed')
    proof=c.layout((c.PREP/'primary.bin').read_bytes(),(c.PREP/'backup.bin').read_bytes())
    c.owners((c.SYSTEM,c.BASE));old=LAUNCHER.read_bytes();proposed,diff=proposal(old)
    PREP.mkdir(exist_ok=False);r={'status':'PREPARING_OFFLINE','commands_executed':[]}
    try:
        argv=['/usr/bin/qemu-img','info','--backing-chain','--output=json',str(c.SYSTEM)]
        completed=subprocess.run(argv,capture_output=True,text=True,timeout=30)
        r['commands_executed'].append({'argv':argv,'exit':completed.returncode})
        (PREP/'chain.stdout.log').write_text(completed.stdout);(PREP/'chain.stderr.log').write_text(completed.stderr)
        if completed.returncode:raise ValueError('Cannot obtain normal-lock original chain')
        chain=c.validate_chain(json.loads(completed.stdout));hashes=hash_chain()
        check=subprocess.run(argv,capture_output=True,text=True,timeout=30)
        r['commands_executed'].append({'argv':argv,'exit':check.returncode})
        (PREP/'chain-after.stdout.log').write_text(check.stdout);(PREP/'chain-after.stderr.log').write_text(check.stderr)
        if check.returncode or c.validate_chain(json.loads(check.stdout))!=chain:raise ValueError('Original chain changed during full hashing')
        if [v['identity'] for v in hashes['files']]!=[c.file_id(p) for p in (c.SYSTEM,c.BASE)]:raise ValueError('Originals changed')
        (PREP/'launcher.original.txt').write_bytes(old);(PREP/'launcher.proposed.txt').write_bytes(proposed);(PREP/'launcher.diff').write_text(diff)
        ssh=['ssh','-i','/tmp/wex-visible-recovery-r1/vm_key','-o','BatchMode=yes','-o','ConnectTimeout=5','-o','StrictHostKeyChecking=yes',
            '-o','UserKnownHostsFile='+str(c.LAB/'known_hosts'),'-p','2235','debian@127.0.0.1']
        prefix=['wsl','-d','Debian','--exec','python3','-B',str(c.REPO/'scripts/g4_capacity_maintenance.py')]
        r.update(status='PREPARED_NOT_RUN_PARENT_REVIEW_REQUIRED',layout=proof,root_fs_uuid_historical=FS_UUID,
            new_overlay=str(c.NEW),run_dir=str(RUN),manifest_path=str(PREP/'preflight.json'),
            retained_GPT_sha256=retained,original_chain=chain,original_hashes=hashes,known_binary_sha256=pins,
            accepted_pin_reference={'path':str(ACCEPTED),'sha256':ACCEPTED_SHA},host_free_bytes=shutil.disk_usage(c.LAB).free,
            inventory={name:{'path':str(c.REPO/name),'sha256':c.sha(c.REPO/name),'bytes':(c.REPO/name).stat().st_size} for name in INVENTORY},
            launcher={'path':str(LAUNCHER),'before_sha256':hashlib.sha256(old).hexdigest(),'proposed_sha256':hashlib.sha256(proposed).hexdigest(),'diff':diff,'changed':False},
            create_argv=['/usr/bin/qemu-img','create','-f','qcow2','-F','qcow2','-b',str(c.SYSTEM),str(c.NEW),str(c.NEW_BYTES)],
            inspect_argv=['/usr/bin/qemu-img','info','--backing-chain','--output=json',str(c.NEW)],
            creation_exit_record=str(c.LAB/(RUN.name+'.create-exit.json')),
            precreate_guard_argv=prefix+['verify-originals'],overlay_guard_argv=prefix+['verify-overlay'],
            boot_argv=['pwsh','-NoProfile','-File',windows(c.REPO/'scripts/g4-virgl-launcher-session.ps1'),'-RunName',RUN.name],
            live_guards_argv=[prefix+['live-guard','--minimum',str(n)] for n in (650,435)],
            guest_ssh_argv=ssh+['python3 -B -'],guest_stdin_source=str(c.REPO/'scripts/g4_capacity_guest.py'),
            guest_commands=guest.COMMANDS,guest_ssh_timeout_seconds=150,guest_whole_seconds=120,guest_each_seconds=15,
            stop_argv=['wsl','-d','Debian','--exec','python3','-B',str(c.REPO/'scripts/stop_g4_virgl_launcher.py'),'--identity',str(RUN/'launcher-identity.json'),'--timeout','60'],
            poststop_guard_argv=prefix+['verify-finished'],
            procedure=['Revalidate inventory + original full hashes/identities/no owners before create; new overlay/run absent',
                'Execute ONE exact create argv; retain actual argv/exit in creation_exit_record plus stdout/stderr; nonzero/partial output STOP/PRESERVE, no retry/delete',
                'Inspect3-layer chain and verify-overlay; apply ONLY reviewed launcher.diff via apply_patch, verify proposed SHA before boot; retain original text',
                'Launch persistent controller once;650 rawUTC + actual QEMU new-drive/identity check before ANY SSH',
                '435 rawUTC + actual identity check immediately before readonly stdin probe; capture stdout/stderr/exit; validate-guest',
                'Always exact identity SIGINT/QMP stop, no Ctrl-C; verify-finished demands QEMU0 + Windows0, original full hashes after shutdown; capacity success requires BOTH validate-guest AND verify-finished exit0',
                'Growth/layout/repair/service/hash failure: preserve overlay/logs, stop/review; NO manual resize/repair/extra boot'],
            guest_validate_argv=prefix+['validate-guest'],limits={'launcher':900,'cleanup_reserve':90,'hash_whole_outside_boot':600},
            rollback='Only with all processes off: selecting old base loses overlay-only changes; never downward commit/rebase/compact/delete',
            seeds={n:{'path':str(c.REPO/'build/g4-virgl-20261022'/n),'sha256':c.sha(c.REPO/'build/g4-virgl-20261022'/n)} for n in ('user-data','meta-data')},
            seed_iso={'path':str(c.LAB/'seed.iso'),'identity':c.file_id(c.LAB/'seed.iso'),'sha256':c.sha(c.LAB/'seed.iso')},
            qemu_img={'path':'/usr/bin/qemu-img','sha256':c.sha(Path('/usr/bin/qemu-img'))},
            stage_b_executed=False,launcher_mutated=False)
    except Exception as error:r.update(status='FAILED_PRESERVED',error=repr(error));raise
    finally:c.save(PREP/'preflight.json',r)
    (PREP/'PREPARED_NOT_RUN').write_text('Offline only. No create/resize/boot/launcher mutation; parent review before Stage B.\n')
    print(json.dumps({'status':r['status'],'preflight_sha256':c.sha(PREP/'preflight.json'),'original_hashes':hashes},indent=2))


def inventory_guard(r,*,proposed=False):
    for name,row in r['inventory'].items():
        expected=r['launcher']['proposed_sha256'] if proposed and row['path']==str(LAUNCHER) else row['sha256']
        if c.sha(Path(row['path']))!=expected:raise ValueError('Changed maintenance inventory: '+name)
    for row in list(r['seeds'].values())+[r['seed_iso'],r['qemu_img']]:
        if c.sha(Path(row['path']))!=row['sha256']:raise ValueError('Changed seed/tool inventory')


def live_guard(r,minimum):
    from g4_virgl_budget import check_budget
    from stop_g4_virgl_launcher import validate_identity,proc_identity
    inventory_guard(r,proposed=True)
    budget=check_budget((c.LAB/(RUN.name+'.wsl-session.json')).read_bytes(),RUN.name,minimum)
    if not budget['passed']:raise ValueError('RawUTC guard failed: '+repr(budget))
    identity=json.loads((RUN/'launcher-identity.json').read_text());validate_identity(identity,RUN/'launcher-identity.json')
    qemu=proc_identity(identity['qemu_pid']);xvfb=proc_identity(identity['xvfb_pid'])
    if qemu['start_ticks']!=identity['qemu_start_ticks'] or xvfb['start_ticks']!=identity['xvfb_start_ticks']:raise ValueError('Owned child reused')
    validate_drive(qemu['cmdline'])
    return {'budget':budget,'actual_qemu':qemu,'actual_xvfb':xvfb,'launcher':identity}


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('mode',choices=['prepare','verify-originals','verify-overlay','live-guard','validate-guest','verify-finished']);p.add_argument('--minimum',type=int,choices=[650,435],default=650);a=p.parse_args()
    if a.mode=='prepare':prepare();return
    r=json.loads((PREP/'preflight.json').read_text())
    if r.get('status')!='PREPARED_NOT_RUN_PARENT_REVIEW_REQUIRED':raise ValueError('Incomplete preparation')
    if a.mode=='verify-originals':
        inventory_guard(r)
        if c.NEW.exists() or c.NEW.is_symlink() or RUN.exists():raise ValueError('Existing overlay/run; no recreate')
        result=hash_chain(r['original_hashes']['files'])
    elif a.mode=='verify-overlay':
        inventory_guard(r);c.owners((c.SYSTEM,c.BASE,c.NEW))
        validate_creation_record(json.loads(Path(r['creation_exit_record']).read_text()),r)
        c.file_id(c.NEW)
        done=subprocess.run(r['inspect_argv'],capture_output=True,text=True,timeout=30)
        if done.returncode:raise ValueError('Overlay inspection failed')
        result={'overlay_valid':validate_new_chain(json.loads(done.stdout),r['original_chain'])}
    elif a.mode=='live-guard':result=live_guard(r,a.minimum)
    elif a.mode=='validate-guest':result=validate_guest(json.loads((RUN/'maintenance-guest.stdout.log').read_text()),r)
    else:
        inventory_guard(r,proposed=True)
        result=json.loads((RUN/'results.json').read_text());session=json.loads((c.LAB/(RUN.name+'.wsl-session.json')).read_text(encoding='utf-8-sig'));identity=json.loads((RUN/'launcher-identity.json').read_text())
        validate_shutdown(result,session,identity)
        gone=stopped_processes(identity)
        c.owners((c.SYSTEM,c.BASE,c.NEW));result={'shutdown_clean0':True,'recorded_processes':gone,'originals_after':hash_chain(r['original_hashes']['files'])}
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
