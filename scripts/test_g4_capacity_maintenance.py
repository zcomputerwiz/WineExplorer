# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline maintenance gates. Fakes only for QEMU creation/boot/guest commands."""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

import g4_capacity as c
import g4_capacity_guest as guest
import g4_capacity_maintenance as m
from test_g4_capacity import tables


def fixture():
    p,b=tables();layout=c.layout(p,b);manifest={'layout':layout,'known_binary_sha256':{p:'a'*64 for p in guest.BINARIES}}
    partitions=[]
    for old in layout['partitions']:
        end=layout['projected_new_last_usable_lba'] if old['number']==1 else old['end_lba']
        partitions.append(dict(node='/dev/vda'+str(old['number']),start=old['start_lba'],size=end-old['start_lba']+1,type=old['type'],uuid=old['guid']))
    root_bytes=(layout['projected_new_last_usable_lba']-layout['root']['start_lba']+1)*512
    texts={
        'lsblk':json.dumps({'blockdevices':[{'path':'/dev/vda','type':'disk','size':c.NEW_BYTES,'log-sec':512,'children':[
            {'path':'/dev/vda1','fstype':'ext4','uuid':m.FS_UUID,'partuuid':c.ROOT}]}]}),
        'root':json.dumps({'filesystems':[dict(source='/dev/vda1',fstype='ext4',uuid=m.FS_UUID,target='/')]}),
        'partition':json.dumps({'partitiontable':dict(device='/dev/vda',label='gpt',unit='sectors',sectorsize=512,id=layout['disk_guid'],partitions=partitions)}),
        'partition_dump':'label: gpt\n',
        'filesystem':f'Filesystem UUID: {m.FS_UUID}\nFilesystem state: clean\nBlock size: 4096\nBlock count: {root_bytes//4096}\n',
        'df':'/dev/vda1 ext4 adequate capacity\n','cloud_init':json.dumps({'status':'done','errors':[],'recoverable_errors':{}}),'services':'running\n',
        'binaries':''.join('a'*64+'  '+p+'\n' for p in guest.BINARIES)}
    return manifest,{'records':[dict(label=label,argv=argv,exit=0,within_deadline=True,reaped=True,stdout=texts[label],stderr='') for label,argv in guest.COMMANDS],
                     'free_bytes':9*1024**3,'elapsed_seconds':1}


class GuestGateTests(unittest.TestCase):
    def test_exact19_including_edge_compositor_root_and_growth(self):
        manifest,r=fixture();out=m.validate_guest(r,manifest)
        self.assertEqual(len(manifest['known_binary_sha256']),19);self.assertEqual(out['binary_count'],19)
        self.assertTrue(out['capacity_growth_verified']);self.assertEqual(out['root_fs_uuid'],m.FS_UUID)

    def test_wrong_uuid_partition_extent_guid_or_root_disk(self):
        for label,key,bad in [('root','uuid','foreign'),('root','source','/dev/vdb1'),('root','fstype','xfs'),
                              ('lsblk','size',c.OLD_BYTES),('lsblk','log-sec',4096),('partition','sectorsize',True)]:
            manifest,r=fixture();row=next(p for p in r['records'] if p['label']==label);value=json.loads(row['stdout'])
            target=value['filesystems'][0] if label=='root' else value['blockdevices'][0] if label=='lsblk' else value['partitiontable']
            target[key]=bad;row['stdout']=json.dumps(value)
            with self.subTest(label=label,key=key),self.assertRaises(ValueError):m.validate_guest(r,manifest)
        for number in (1,14,15):
            for field,bad in [('start',True),('size',1),('uuid','wrong'),('type','wrong')]:
                manifest,r=fixture();row=next(p for p in r['records'] if p['label']=='partition');value=json.loads(row['stdout'])
                p=next(p for p in value['partitiontable']['partitions'] if p['node']=='/dev/vda'+str(number));p[field]=bad;row['stdout']=json.dumps(value)
                with self.assertRaises(ValueError):m.validate_guest(r,manifest)

    def test_state_filesystem_growth_cloud_init_and_services_failclosed(self):
        for label,text in [('filesystem','Filesystem UUID: wrong\n'),('filesystem',f'Filesystem UUID: {m.FS_UUID}\nFilesystem state: not clean\n'),
                           ('cloud_init','{"status":"running","errors":[],"recoverable_errors":{}}'),
                           ('cloud_init','{"status":"done","errors":["repair"],"recoverable_errors":{}}'),
                           ('cloud_init','{"status":"done","errors":[],"recoverable_errors":{"x":["error"]}}'),('services','degraded')]:
            manifest,r=fixture();next(p for p in r['records'] if p['label']==label)['stdout']=text
            with self.subTest(label=label),self.assertRaises(ValueError):m.validate_guest(r,manifest)

    def test_hash_mismatch_missing_duplicate_and_wrong_paths(self):
        for action in ('wrong','missing','duplicate','path'):
            manifest,r=fixture();row=r['records'][-1]
            if action=='wrong':row['stdout']=row['stdout'].replace('a'*64,'b'*64,1)
            if action=='missing':row['stdout']='\n'.join(row['stdout'].splitlines()[1:])
            if action=='duplicate':row['stdout']+=row['stdout'].splitlines()[0]+'\n'
            if action=='path':row['stdout']=row['stdout'].replace(guest.BINARIES[0],'/foreign')
            with self.assertRaises(ValueError):m.validate_guest(r,manifest)

    def test_bad_timing_free_quota_schema_exit_and_reap(self):
        for key,bad in [('free_bytes',True),('free_bytes',8*1024**3-1),('elapsed_seconds',True),('elapsed_seconds',float('nan')),
                        ('elapsed_seconds',float('inf')),('elapsed_seconds',121),('elapsed_seconds',-1)]:
            manifest,r=fixture();r[key]=bad
            with self.assertRaises(ValueError):m.validate_guest(r,manifest)
        for field,bad in [('argv',['repair']),('exit',True),('exit',1),('within_deadline',False),('reaped',False),('stdout','x'*1048577),('cleanup_error','reap failure')]:
            manifest,r=fixture();r['records'][0][field]=bad
            with self.assertRaises(ValueError):m.validate_guest(r,manifest)


class HostGateTests(unittest.TestCase):
    def test_windows_command_and_exact_launcher_proposal(self):
        self.assertEqual(m.windows(c.REPO/'scripts/g4-virgl-launcher-session.ps1'),r'C:\Users\Owner\.codex\worktrees\chromium-graphics-plan\WineExplorer\scripts\g4-virgl-launcher-session.ps1')
        with self.assertRaises(ValueError):m.windows(Path('/mnt/d/wrong'))
        original=b"start\nstr(root / 'system.qcow2')\nend\n";proposal,diff=m.proposal(original)
        self.assertEqual(proposal,b"start\nstr(root / 'system-capacity-r1-20261023.qcow2')\nend\n");self.assertIn('-str(root',diff)
        for old in (b'wrong',original+original):
            with self.assertRaises(ValueError):m.proposal(old)

    def test_actual_drive_not_stale_placeholder_or_other_vm(self):
        argv=['qemu-system-x86_64','-drive','file='+str(c.NEW)+',format=qcow2,if=virtio','-drive','file='+str(c.LAB/'seed.iso')+',format=raw,media=cdrom,readonly=on']
        self.assertTrue(m.validate_drive(argv))
        for wrong in ([],[v.replace(str(c.NEW),str(c.SYSTEM)) for v in argv],argv+['-hda','foreign'],argv+['-drive','other']):
            with self.assertRaises(ValueError):m.validate_drive(wrong)

    def test_shutdown_requires_both_qemu0_windows0_exact_identity(self):
        identity=dict(pid=123,start_ticks=99,run_dir=str(m.RUN),identity_record=str(m.RUN/'launcher-identity.json'))
        result=dict(stop_reason='signal',graceful_powerdown=True,qemu_exit_status=0,cleanup=dict(qemu_stopped=True,xvfb_stopped=True),launcher_identity=identity)
        session=dict(runName=m.RUN.name,state='exited',windowsExitCode=0)
        self.assertTrue(m.validate_shutdown(result,session,identity))
        for field,bad in [('qemu_exit_status',1),('qemu_exit_status',True),('graceful_powerdown',False),('launcher_identity',{})]:
            with self.assertRaises(ValueError):m.validate_shutdown(dict(result,**{field:bad}),session,identity)
        for code in (1,True,None):
            with self.assertRaises(ValueError):m.validate_shutdown(result,dict(session,windowsExitCode=code),identity)

    def test_full_content_hashes_changes_and_timeout_no_mutation(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);one=root/'one';two=root/'two';one.write_bytes(b'original');two.write_bytes(b'base')
            with patch.multiple(c,SYSTEM=one,BASE=two),patch.object(c,'owners',return_value={}):
                result=m.hash_chain();self.assertEqual(result['files'][0]['sha256'],hashlib.sha256(b'original').hexdigest())
                m.hash_chain(result['files']);one.write_bytes(b'changed')
                with self.assertRaises(ValueError):m.hash_chain(result['files'])
                with self.assertRaises(TimeoutError):m.hash_chain(now=Mock(side_effect=[0,601]))
                for seconds in (True,float('nan'),0,601):
                    with self.assertRaises(ValueError):m.hash_chain(seconds=seconds)

    def test_preparation_existing_paths_refuse_before_hash_or_subprocess(self):
        with tempfile.TemporaryDirectory() as tmp:
            root=Path(tmp);prep=root/'prep';prep.mkdir()
            with patch.object(m,'PREP',prep),patch.object(m,'hash_chain') as hashes,patch.object(m.subprocess,'run') as run:
                with self.assertRaises(ValueError):m.prepare()
                hashes.assert_not_called();run.assert_not_called()

    def test_missing_failed_or_wrong_creation_record_never_allows_partial_image(self):
        manifest={'create_argv':['exact fixed future create']}
        for record in ({},{'exit':1,'argv':manifest['create_argv']},{'exit':True,'argv':manifest['create_argv']},{'exit':0,'argv':['wrong']}):
            with self.assertRaises(ValueError):m.validate_creation_record(record,manifest)
        m.validate_creation_record({'exit':0,'argv':manifest['create_argv']},manifest)

    def test_stopped_exact_processes_absent_not_just_empty_startup_cmdline(self):
        identity=dict(pid=1,start_ticks=10,qemu_pid=2,qemu_start_ticks=20,xvfb_pid=3,xvfb_start_ticks=30,qemu_cmdline=[])
        result=m.stopped_processes(identity,query=Mock(side_effect=FileNotFoundError()))
        self.assertEqual(result,dict(launcher='absent',qemu='absent',xvfb='absent'))
        with self.assertRaises(ValueError):m.stopped_processes(identity,query=lambda pid:{'start_ticks':pid*10})
        self.assertIn('PID reused',m.stopped_processes(identity,query=lambda pid:{'start_ticks':999})['qemu'])

    def test_new_overlay_chain_wrong_size_backing_or_original_metadata(self):
        original=[{'filename':str(c.SYSTEM)},{'filename':str(c.BASE)}]
        new=dict(filename=str(c.NEW),format='qcow2',**{'virtual-size':c.NEW_BYTES,'backing-filename':str(c.SYSTEM),'full-backing-filename':str(c.SYSTEM),
            'backing-filename-format':'qcow2','dirty-flag':False,'format-specific':{'data':{'corrupt':False}}})
        with patch.object(c,'validate_chain',return_value=original):
            self.assertTrue(m.validate_new_chain([new,*original],original))
            for field,bad in [('virtual-size',True),('full-backing-filename',str(c.BASE)),('dirty-flag',True)]:
                with self.assertRaises(ValueError):m.validate_new_chain([dict(new,**{field:bad}),*original],original)
            with self.assertRaises(ValueError):m.validate_new_chain([new,{'filename':'foreign'},original[1]],original)


class ProbeTransportTests(unittest.TestCase):
    def test_actual_linux_child_timeout_killed_reaped_no_remaining_pid(self):
        children=[];popen=subprocess.Popen
        def create(*args,**kwargs):
            child=popen(*args,**kwargs);children.append(child);return child
        command=[sys.executable,'-c','import os,time;print(os.getpid(),flush=True);time.sleep(30)']
        with patch.object(guest,'COMMANDS',[('original-python-timeout',command)]),patch.object(guest.subprocess,'Popen',side_effect=create):
            r=guest.collect()
        self.assertEqual(len(children),1);child=children[0];row=r['records'][0]
        self.assertEqual(int(row['stdout'].strip()),child.pid);self.assertEqual(child.returncode,-9);self.assertTrue(row['reaped'])
        self.assertIn('TimeoutExpired',r['error']);self.assertNotIn('cleanup_error',row);self.assertTrue(row['within_deadline'])
        with self.assertRaises(ProcessLookupError):os.kill(child.pid,0)
    def test_success_fixed_readonly_argv_and_reaped_output(self):
        child=Mock(pid=123,returncode=0);child.communicate.return_value=(b'fixed output',b'');child.poll.return_value=0
        with patch.object(guest.subprocess,'Popen',return_value=child) as popen:
            r=guest.collect()
        self.assertNotIn('error',r);self.assertEqual(popen.call_count,9)
        self.assertEqual([c.args[0] for c in popen.call_args_list],[v for _,v in guest.COMMANDS])
        self.assertTrue(all(v['reaped'] for v in r['records']))

    def test_timeout_kills_exact_group_and_reaps_inside_budget(self):
        child=Mock(pid=123);child.communicate.side_effect=[subprocess.TimeoutExpired('readonly',14),(b'partial',b'')]
        child.poll.side_effect=[None];child.returncode=-9
        with patch.object(guest.subprocess,'Popen',return_value=child),patch.object(guest.os,'getpgid',return_value=123),patch.object(guest.os,'killpg') as kill:
            r=guest.collect()
        kill.assert_called_once_with(123,guest.signal.SIGKILL);self.assertTrue(r['records'][0]['reaped']);self.assertIn('TimeoutExpired',r['error'])
        self.assertTrue(r['records'][0]['within_deadline']);self.assertEqual(len(r['records']),1)

    def test_reap_failure_separate_primary_retained_nonblocking_final_reap(self):
        child=Mock(pid=123,returncode=-9);child.communicate.side_effect=[subprocess.TimeoutExpired('primary timeout',14),RuntimeError('SECONDARY reap')]
        child.poll.side_effect=[None,-9]
        with patch.object(guest.subprocess,'Popen',return_value=child),patch.object(guest.os,'getpgid',return_value=123),patch.object(guest.os,'killpg'):
            r=guest.collect()
        self.assertIn('primary timeout',r['error']);self.assertIn('SECONDARY',r['records'][0]['cleanup_error']);self.assertTrue(r['records'][0]['reaped'])


if __name__=='__main__':unittest.main()
