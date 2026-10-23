# SPDX-License-Identifier: GPL-2.0-or-later
import base64
import copy
import hashlib
import io
import json
from pathlib import Path
import stat
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

import g4_chromium_cache as cache


class CacheTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)

    def response(self,data,url='https://official/test',length=None):
        result=io.BytesIO(data);result.geturl=lambda:url;result.status=200
        result.headers={} if length is None else {'Content-Length':str(length)}
        return result

    def test_bounded_download_positive_hash(self):
        data=b'original payload'
        r=cache.fetch('https://official/test',self.root/'payload',len(data),expected=len(data),
            md5=base64.b64encode(hashlib.md5(data).digest()).decode(),open_url=lambda *a,**k:self.response(data,length=len(data)))
        self.assertEqual(r['sha256'],hashlib.sha256(data).hexdigest())
        self.assertEqual((self.root/'payload').read_bytes(),data)

    def test_download_failures_no_retry_and_preserve(self):
        cases=[(b'xx',dict(expected=3)),(b'xxxx',{}),(b'xxx',dict(md5='wrong')),
               (b'xxx',dict(now=iter([0,0,4]).__next__,timeout=3))]
        for i,(data,kw) in enumerate(cases):
            calls=[]
            def open_url(*a,**k): calls.append(a);return self.response(data)
            with self.assertRaises((ValueError,TimeoutError)):cache.fetch('https://official/test',self.root/str(i),3,open_url=open_url,**kw)
            self.assertEqual(len(calls),1);self.assertTrue((self.root/str(i)).exists())
        with self.assertRaises(ValueError):cache.fetch('url',self.root/'0',3)
        for response in (self.response(b'x',url='https://foreign/test'),self.response(b'x',length=100)):
            with self.assertRaises(ValueError):cache.fetch('https://official/test',self.root/'bad',3,open_url=lambda *a,**k:response)
            if (self.root/'bad').exists(): (self.root/'bad').unlink()

    def test_redirects_never_followed(self):
        with self.assertRaises(ValueError):cache.NoRedirect().redirect_request(None,None,None,None,None,None)

    def test_exact_metadata_catalog_version(self):
        m=dict(bucket='chromium-browser-snapshots',name='Win_x64/1706509/chrome-win.zip',generation=cache.GENERATION,
               size=str(cache.BYTES),md5Hash=cache.MD5,mediaLink=cache.MEDIA)
        r=dict(got_revision=cache.REVISION,chromium_revision='1706509',got_revision_cp='refs/heads/main@{#1706509}')
        v=base64.b64encode(b'MAJOR=156\nMINOR=0\nBUILD=8078\nPATCH=0\n')
        cache.metadata(m,r,v)
        for key in m:
            bad=dict(m);bad[key]='wrong'
            with self.assertRaises(ValueError):cache.metadata(bad,r,v)
        for key in r:
            bad=dict(r);bad[key]='wrong'
            with self.assertRaises(ValueError):cache.metadata(m,bad,v)
        with self.assertRaises(ValueError):cache.metadata(m,r,base64.b64encode(b'other version'))

    def archive(self,extras=()):
        output=io.BytesIO()
        with zipfile.ZipFile(output,'w') as z:
            for name in cache.REQUIRED:z.writestr('chrome-win/'+name,b'original')
            for name in extras:
                if isinstance(name,zipfile.ZipInfo):z.writestr(name,b'x')
                else:z.writestr(name,b'x')
        return output.getvalue()

    def test_all_hostile_entries_rejected_before_output(self):
        for name in ('../evil','/absolute','C:/evil','//unc/x','chrome-win/../escape','chrome-win\\evil',
                     'chrome-win/a:ads','chrome-win/a//b','chrome-win/CON.txt','chrome-win/LPT¹.txt',
                     'chrome-win/a.','chrome-win/a ','chrome-win/CHROME.EXE','other/file',
                     'chrome-win/chrome.exe/child','chrome-win/a\x00hidden'):
            with self.subTest(name=name):
                z=self.root/'test.zip';z.write_bytes(self.archive([name]))
                # ZipFile truncates NUL on write; test raw parsed original separately below.
                if '\x00' in name:continue
                with self.assertRaises(ValueError):cache.extract(z,self.root/'out',free=10*1024**3)
                self.assertFalse((self.root/'out').exists())
        item=zipfile.ZipInfo('chrome-win/a\x00hidden')
        self.assertNotEqual(item.orig_filename,item.filename)
        fake=type('Archive',(),{'infolist':lambda s:[item]})()
        with self.assertRaises(ValueError):cache.zip_inventory(fake)

    def test_links_special_encrypted_and_bombs(self):
        for mode in (stat.S_IFLNK|0o777,stat.S_IFIFO|0o600,stat.S_IFSOCK|0o600):
            item=zipfile.ZipInfo('chrome-win/special');item.external_attr=mode<<16
            with zipfile.ZipFile(io.BytesIO(self.archive([item]))) as z:
                with self.assertRaises(ValueError):cache.zip_inventory(z)
        with zipfile.ZipFile(io.BytesIO(self.archive())) as z:
            for attr,value in (('file_size',cache.MAX_MEMBER+1),('flag_bits',1)):
                setattr(z.infolist()[0],attr,value)
                with self.assertRaises(ValueError):cache.zip_inventory(z)
                setattr(z.infolist()[0],attr,0)
            with patch.object(cache,'MAX_TOTAL',1),self.assertRaises(ValueError):cache.zip_inventory(z)
            with patch.object(cache,'MAX_MEMBERS',1),self.assertRaises(ValueError):cache.zip_inventory(z)

    def test_extraction_hashes_space_fresh_deadline_and_required(self):
        z=self.root/'test.zip';z.write_bytes(self.archive())
        with self.assertRaises(ValueError):cache.extract(z,self.root/'low',free=0)
        self.assertFalse((self.root/'low').exists())
        with self.assertRaises(TimeoutError):cache.extract(z,self.root/'late',free=10*1024**3,now=iter([0,5]).__next__,timeout=1)
        records=cache.extract(z,self.root/'out',free=10*1024**3)
        self.assertEqual(set(records),{'chrome-win/'+n for n in cache.REQUIRED})
        self.assertEqual(records['chrome-win/chrome.exe']['sha256'],hashlib.sha256(b'original').hexdigest())
        with self.assertRaises(ValueError):cache.extract(z,self.root/'out')
        with zipfile.ZipFile(io.BytesIO(self.archive())) as source:
            source.filelist.pop()
            with self.assertRaises(ValueError):cache.zip_inventory(source)

    def test_pe_bounds_and_architecture(self):
        p=self.root/'chrome.exe';data=bytearray(128);data[:2]=b'MZ';struct.pack_into('<I',data,60,64)
        data[64:68]=b'PE\0\0';struct.pack_into('<H',data,68,0x8664);p.write_bytes(data)
        self.assertEqual(cache.pe(p)['machine'],0x8664)
        for offset,value in ((0,b'NO'),(68,b'\x4c\x01'),(60,b'\xff'*4)):
            bad=bytearray(data);bad[offset:offset+len(value)]=value;p.write_bytes(bad)
            with self.assertRaises(ValueError):cache.pe(p)

    def test_version_resource_mismatch_fails(self):
        paths=['D:\\cache\\'+name for name in ('chrome.exe','chrome.dll','chrome_elf.dll')]
        versions=[dict(path=p,file=cache.VERSION,product=cache.VERSION+' source') for p in paths]
        with patch.object(cache.subprocess,'check_output',side_effect=paths*2) as convert, \
             patch.object(cache.subprocess,'run',return_value=type('R',(),{'stdout':json.dumps(versions)})()) as call:
            self.assertEqual(cache.file_versions(self.root),versions)
            self.assertEqual(call.call_args.args[0][0],'pwsh.exe')
            self.assertTrue(all(0<c.kwargs['timeout']<=20 for c in convert.call_args_list))
            self.assertLessEqual(call.call_args.kwargs['timeout'],20)
            versions[0]['file']='other';call.return_value.stdout=json.dumps(versions)
            with self.assertRaises(ValueError):cache.file_versions(self.root)

    def test_version_paths_unique_exact_schema_and_complete_budget(self):
        paths=['D:\\cache\\'+name for name in ('chrome.exe','chrome.dll','chrome_elf.dll')]
        rows=[dict(path=p,file=cache.VERSION,product=cache.VERSION) for p in paths]
        for mutation in (lambda r:r[0].update(path='D:\\foreign\\chrome.exe'),
                         lambda r:r.__setitem__(1,dict(r[0])),lambda r:r.reverse(),
                         lambda r:r[0].update(file=123),lambda r:r[0].pop('path'),
                         lambda r:r[0].update(extra=True),lambda r:r.__setitem__(0,'bad')):
            bad=copy.deepcopy(rows);mutation(bad)
            with self.assertRaises(ValueError):cache.version_records(bad,paths)
        for bad in ({},None,rows[:2]):
            with self.assertRaises(ValueError):cache.version_records(bad,paths)
        with patch.object(cache.subprocess,'check_output',side_effect=paths) as convert, \
             patch.object(cache.subprocess,'run') as query:
            with self.assertRaises(TimeoutError):cache.file_versions(self.root,now=iter([0,1,10,21]).__next__)
            self.assertEqual(convert.call_count,2);query.assert_not_called()

    def test_missing_standalone_notices_fails_without_inventing_embedded_evidence(self):
        runtime=self.root/'runtime/chrome-win';runtime.mkdir(parents=True)
        (runtime/'resources.pak').write_bytes(b'opaque embedded resources')
        with self.assertRaisesRegex(ValueError,'No retained license/notices'):cache.retained_notices(runtime)
        (runtime/'LICENSE').write_text('original notice fixture')
        self.assertEqual(cache.retained_notices(runtime),['chrome-win/LICENSE'])

    def test_acquisition_failure_retains_primary_record_and_never_retries(self):
        # Synthetic dispatch only; no real network or payloads in tests.
        target=Path('/mnt/d/VirtualBox_VMs/WEX-G4-VirGL-20261022/test-cache')
        with patch.object(Path,'exists',return_value=False),patch.object(Path,'is_symlink',return_value=False), \
             patch.object(Path,'mkdir'),patch.object(cache.shutil,'disk_usage',return_value=type('D',(),{'free':10*1024**3})()), \
             patch.object(cache,'fetch',side_effect=TimeoutError('primary download deadline')) as fetch, \
             patch.object(cache,'save') as save,patch.object(cache,'extract') as extract:
            with self.assertRaisesRegex(TimeoutError,'primary download deadline'):cache.acquire(target)
        self.assertEqual(fetch.call_count,1);extract.assert_not_called()
        record=save.call_args.args[1]
        self.assertEqual(record['status'],'FAILED_PRESERVED');self.assertIn('primary download deadline',record['error'])


if __name__=='__main__':unittest.main()
