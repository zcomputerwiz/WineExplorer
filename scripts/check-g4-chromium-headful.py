#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""ONE explicit portable Chromium HTTP baseline in the retained Edge prefix.

Same original fixture/security/backend; NOT a pristine-prefix or full-G4 proof.
No diagnostic/attachment flags, arbitrary extra arguments or alternate runtime.
"""
import argparse
import json
from pathlib import Path
import re
import shutil
import time

import g4_chromium_cache as cache
import g4_edge_owned_threads as ownership

# Import a fresh runner module, leaving Edge CLI/defaults unchanged.
import importlib.util
spec=importlib.util.spec_from_file_location('chromium_edge_runner',Path(__file__).with_name('check-g4-edge-headful.py'))
edge=importlib.util.module_from_spec(spec);spec.loader.exec_module(edge)
MANIFEST_SHA='8fc037ca464bc22952c9f9d63447cf0feb7dcab07e20489cb2d3a4ef3e1a8ba7'
FIXTURE_SHA={'index.html':'f0a47b1a6013c015228c3e6c76a0a73b5d3eb6c218ec031a34dd8da7e0c0edc3',
             'scene.js':'fe9b1801a7d4baa3512fb4e103330e317d17c9f2e2763abf8f14a275c686ee51'}


class Chromium:
    def __init__(self,runtime): self.runtime=runtime

    def require_payload(self,output,fixture):
        expected=edge.LAB/(output.name+'-stage')/'vendor/extracted/chrome-win'
        root=self.runtime
        if root!=expected or root.resolve(strict=True)!=expected or root.is_symlink():
            raise ValueError('Exact owned staged portable Chromium path required')
        if (edge.PREFIX/'dosdevices/z:').resolve(strict=True)!=Path('/'):
            raise ValueError('Portable Chromium requires verified Z: root mapping')
        manifest=root.parent.parent/'runtime-manifest.json'
        if manifest.is_symlink() or cache.digest(manifest)!=MANIFEST_SHA: raise ValueError('Pinned runtime inventory changed')
        record=json.loads(manifest.read_text())
        if (record.get('version')!=cache.VERSION or record.get('revision')!=cache.REVISION
                or record.get('zip_sha256')!=cache.ZIP_SHA): raise ValueError('Pinned Chromium version/source changed')
        paths=list(root.rglob('*'))
        if any(p.is_symlink() for p in paths): raise ValueError('Portable runtime symlink')
        files={p.relative_to(root.parent).as_posix():p for p in paths if p.is_file()}
        if set(files)!=set(record['files']): raise ValueError('Missing/extra Chromium runtime files')
        deadline=time.monotonic()+15
        for name,path in files.items():
            if time.monotonic()>=deadline: raise TimeoutError('Runtime verification deadline')
            item=record['files'][name]
            if path.stat().st_size!=item['bytes'] or cache.digest(path)!=item['sha256']:
                raise ValueError('Chromium runtime hash/size changed: '+name)
        for name in ('chrome.exe','chrome.dll','chrome_elf.dll','D3DCompiler_47.dll','libEGL.dll','libGLESv2.dll'):
            cache.pe(root/name)
        for name,expected_sha in FIXTURE_SHA.items():
            if cache.digest(fixture.with_name(name))!=expected_sha: raise ValueError('Original fixture changed')
        if shutil.disk_usage(edge.LAB).free<1024**3: raise ValueError('Need 1 GiB after runtime staging; never delete evidence')
        if time.monotonic()>=deadline: raise TimeoutError('Complete runtime verification deadline')
        return {'runtime':str(root),'version':cache.VERSION,'version_source':'host FileVersionInfo + pinned entire runtime hashes',
            'manifest_sha256':MANIFEST_SHA,'zip_sha256':cache.ZIP_SHA,'file_count':len(files),
            'owner_aliases':['Z:'+str(root/'chrome.exe').replace('/','\\')],
            'alias_mapping':{'z:':'/'},'shared_prefix_contains_edge':True}

    def command(self,fixture,output,nonce,url):
        if (not re.fullmatch('[0-9a-f]{32}',nonce) or not isinstance(url,str)
                or not re.fullmatch(r'http://127\.0\.0\.1:([1-9][0-9]{0,4})/'+nonce+'/index.html#nonce='+nonce,url)
                or not 1<=int(url.split(':')[2].split('/')[0])<=65535):
            raise ValueError('Exact own nonce loopback HTTP app URL required')
        argv=edge.command(fixture,output,nonce,http_url=url)
        argv[1]=str(self.runtime/'chrome.exe')
        # No caller-controlled flags; same logging/app/profile/security as Edge normal mode.
        return argv

    def linux_owner(self,pid,sample,argv):
        if not isinstance(sample,dict) or not ownership.integer(sample.get('start_ticks')):
            raise ValueError('Missing original Popen Linux identity; no PID scan/fallback')
        return ownership.linux_identity(Path('/proc'),pid,sample['start_ticks'],edge.PREFIX,argv)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--fixture',type=Path,required=True)
    parser.add_argument('--output-dir',type=Path,required=True)
    args=parser.parse_args()
    return edge.run(args.output_dir,args.fixture,edge.load('check-g3-correctness'),chromium=Chromium(args.runtime),
                    private_loopback_control=True,http_fixture_control=True)


if __name__=='__main__':raise SystemExit(main())
