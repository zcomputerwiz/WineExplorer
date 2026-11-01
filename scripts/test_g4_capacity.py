# SPDX-License-Identifier: GPL-2.0-or-later
"""Bounded synthetic GPT and fake mutation outcomes; no image creation/VM."""
import copy
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch
import uuid
import zlib
import g4_capacity as c


def tables(change=None):
    sectors=c.OLD_BYTES//512;array=bytearray(16384)
    rows={14:(34,2047),15:(2048,262143),1:(262144,sectors-34)}
    for n,(start,end) in rows.items():
        uid=c.ROOT if n==1 else str(uuid.UUID(int=n))
        entry=struct.pack('<16s16sQQQ72s',uuid.UUID(c.TYPES[n]).bytes_le,uuid.UUID(uid).bytes_le,start,end,0,b'\0'*72)
        array[(n-1)*128:n*128]=entry
    if change:change(array)
    def h(at,other,entries):
        raw=bytearray(512)
        struct.pack_into('<8sIIIIQQQQ16sQIII',raw,0,b'EFI PART',0x10000,92,0,0,at,other,34,sectors-34,uuid.UUID(int=123).bytes_le,entries,128,128,zlib.crc32(array))
        struct.pack_into('<I',raw,16,zlib.crc32(raw[:92]));return raw
    mbr=bytearray(512);mbr[510:512]=b'\x55\xaa';mbr[450]=0xee;struct.pack_into('<II',mbr,454,1,sectors-1)
    return mbr+h(1,sectors-1,2)+array,array+h(sectors-1,1,sectors-33)


def repair_header(raw,offset):
    raw[offset+16:offset+20]=b'\0'*4;struct.pack_into('<I',raw,offset+16,zlib.crc32(raw[offset:offset+92]))


class GPTTests(unittest.TestCase):
    def test_exact_x86_64_root_type_rejects_generic_and_other_architectures(self):
        self.assertEqual(c.TYPES[1],'4f68bce3-e8cd-4db1-96e7-fbcaf984b709')
        for kind in ('0fc63daf-8483-4772-8e79-3d69d8477de4','b921b045-1df0-41c3-af44-4c6f280d3fae','44479540-f297-41b2-9af7-d131d5f0458a'):
            p,b=tables(lambda a:a.__setitem__(slice(0,16),uuid.UUID(kind).bytes_le))
            with self.assertRaises(ValueError):c.layout(p,b)

    def test_valid_inclusive_extents_unused_guids_and_last_root(self):
        p,b=tables();r=c.layout(p,b)
        self.assertEqual(r['root']['end_lba'],r['last_usable_lba'])
        self.assertEqual(len(r['partitions']),3);self.assertEqual(r['additional_root_sectors'],16*1024**3//512)
        self.assertEqual(r['partitions'][0]['number'],1)

    def test_header_signatures_crcs_reciprocal_and_schema(self):
        for offset,value,fix in ((512,b'NOT GPT!',False),(528,b'bad!',False),(536,struct.pack('<Q',2),True),
                                 (544,struct.pack('<Q',999),True),(592,struct.pack('<I',129),True)):
            p,b=tables();p[offset:offset+len(value)]=value
            if fix:repair_header(p,512)
            with self.subTest(offset=offset),self.assertRaises(ValueError):c.layout(p,b)

    def test_backup_header_array_and_array_crc(self):
        for action in ('header','array','primary-array','reciprocal'):
            p,b=tables()
            if action=='header':b[-512]=0
            if action=='array':b[12]^=1
            if action=='primary-array':p[1030]^=1
            if action=='reciprocal':struct.pack_into('<Q',b,len(b)-512+32,99);repair_header(b,len(b)-512)
            with self.assertRaises(ValueError):c.layout(p,b)

    def test_mbr_hybrid_signature_and_size(self):
        for offset in (510,450,454,458,466):
            p,b=tables();p[offset]^=1
            with self.subTest(offset=offset),self.assertRaises(ValueError):c.layout(p,b)

    def test_bounds_overlap_duplicate_guid_nonlast_root_and_root_uuid(self):
        def start(a,n,v):struct.pack_into('<Q',a,(n-1)*128+32,v)
        def end(a,n,v):struct.pack_into('<Q',a,(n-1)*128+40,v)
        changes=[lambda a:start(a,14,33),lambda a:end(a,1,c.OLD_BYTES//512),
                 lambda a:start(a,1,262143),lambda a:end(a,14,2048),
                 lambda a:a.__setitem__(slice(13*128+16,13*128+32),a[16:32]),
                 lambda a:a.__setitem__(slice(16,32),b'\0'*16),
                 lambda a:(start(a,1,34),end(a,1,100),start(a,14,101),end(a,14,2047)),
                 lambda a:a.__setitem__(slice(16,32),uuid.UUID(int=99).bytes_le)]
        for change in changes:
            p,b=tables(change)
            with self.assertRaises(ValueError):c.layout(p,b)

    def test_bad_lengths_sector_size_bool_nonfinite_and_overflow(self):
        p,b=tables()
        for cap,new in ((True,c.NEW_BYTES),(float('nan'),c.NEW_BYTES),(2**64,c.NEW_BYTES),(c.OLD_BYTES,False),(c.OLD_BYTES,c.OLD_BYTES),(c.OLD_BYTES+1,c.NEW_BYTES)):
            with self.assertRaises(ValueError):c.layout(p,b,cap,new)
        for a,z in ((p[:-1],b),(p,b+b'\0'),(p[:512],b)):
            with self.assertRaises(ValueError):c.layout(a,z)

    def test_unknown_table_and_noncanonical_backup_metadata(self):
        p,b=tables(lambda a:a.__setitem__(slice(0,16),uuid.UUID(int=9).bytes_le))
        with self.assertRaises(ValueError):c.layout(p,b)
        p,b=tables();struct.pack_into('<Q',b,len(b)-512+40,35);repair_header(b,len(b)-512)
        with self.assertRaises(ValueError):c.layout(p,b)


class PreparationTests(unittest.TestCase):
    def test_exact_readonly_bounded_commands_no_unsupported_skip_or_share(self):
        cmds=c.extraction_commands(c.PREP)
        self.assertEqual(cmds[0][-3:],['count=34','if='+str(c.SYSTEM),'of='+str(c.PREP/'primary.bin')])
        self.assertIn('count=33',cmds[1]);self.assertIn('offset='+str(c.OLD_BYTES-33*512),cmds[1][-2])
        self.assertNotIn('-U',sum(cmds,[]));self.assertFalse(any('skip=' in arg for cmd in cmds for arg in cmd))

    def test_partial_create_fake_outcome_never_retry_delete_or_accept(self):
        fake=Mock(return_value=SimpleNamespace(returncode=1))
        done=fake(['not dispatched to QEMU'])
        with self.assertRaisesRegex(ValueError,'preserve output'):c.creation_result(done.returncode,True,{})
        fake.assert_called_once()
        info=dict(filename=str(c.NEW),format='qcow2',**{'virtual-size':c.NEW_BYTES,'full-backing-filename':str(c.SYSTEM),'backing-filename-format':'qcow2'})
        self.assertTrue(c.creation_result(0,True,info))
        for field,bad in [('filename','/wrong'),('virtual-size',True),('full-backing-filename','/wrong'),('format','raw')]:
            with self.assertRaises(ValueError):c.creation_result(0,True,dict(info,**{field:bad}))

    def test_existing_wrong_path_missing_tool_space_before_subprocess(self):
        with tempfile.TemporaryDirectory() as tmp:
            lab=Path(tmp);prep=lab/'prep';new=lab/'new'
            with patch.multiple(c,LAB=lab,PREP=prep,NEW=new),patch.object(c.subprocess,'run') as run:
                prep.mkdir()
                with self.assertRaises(ValueError):c.prepare()
                prep.rmdir();new.touch()
                with self.assertRaises(ValueError):c.prepare()
                new.unlink()
                with patch.object(c,'NEW',lab/'other'/'new'):
                    with self.assertRaises(ValueError):c.prepare()
                with patch.object(c.shutil,'which',return_value=None):
                    with self.assertRaises(ValueError):c.prepare()
                with patch.object(c.shutil,'which',return_value='/usr/bin/tool'),patch.object(c.shutil,'disk_usage',return_value=SimpleNamespace(free=0)):
                    with self.assertRaises(ValueError):c.prepare()
                run.assert_not_called();self.assertFalse(prep.exists())

    def test_changed_chain_bool_size_and_symlink_rejected(self):
        rows=[dict(filename=str(c.SYSTEM),format='qcow2',**{'virtual-size':c.OLD_BYTES,'dirty-flag':False,'format-specific':{'data':{'corrupt':False}},'full-backing-filename':str(c.BASE),'backing-filename':str(c.BASE),'backing-filename-format':'qcow2'}),
              dict(filename=str(c.BASE),format='qcow2',**{'virtual-size':3*1024**3,'dirty-flag':False,'format-specific':{'data':{'corrupt':False}}})]
        with patch.object(c,'file_id',return_value={}):
            c.validate_chain(rows)
            for field,bad in [('filename','/foreign'),('virtual-size',True),('backing-filename','relative.qcow2'),('dirty-flag',True)]:
                badrows=copy.deepcopy(rows);badrows[0][field]=bad
                with self.assertRaises(ValueError):c.validate_chain(badrows)
        with tempfile.TemporaryDirectory() as tmp:
            path=Path(tmp)/'link';path.symlink_to(Path(tmp)/'missing')
            with self.assertRaises((ValueError,FileNotFoundError)):c.file_id(path)

    def test_active_fd_and_qemu_refuse(self):
        with tempfile.TemporaryDirectory() as tmp:
            proc=Path(tmp);p=proc/'123';(p/'fd').mkdir(parents=True)
            (p/'cmdline').write_bytes(b'python\0');(p/'exe').symlink_to('/usr/bin/python3');(p/'fd/7').symlink_to(c.SYSTEM)
            with self.assertRaisesRegex(ValueError,'open chain'):c.owners([c.SYSTEM,c.BASE],proc)
            (p/'fd/7').unlink();(p/'exe').unlink();(p/'exe').symlink_to('/usr/bin/qemu-system-x86_64')
            with self.assertRaisesRegex(ValueError,'QEMU'):c.owners([c.SYSTEM,c.BASE],proc)


if __name__=='__main__':unittest.main()
