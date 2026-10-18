#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Stage only verified open assets. No Wine installers are executed here."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import urllib.request
import zipfile

HERE = Path(__file__).resolve().parent


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def fetch(cache, name, url, sha, offline):
    path = cache / name
    if path.is_file() and digest(path) == sha:
        return path
    if offline:
        raise ValueError(f'Missing or corrupt offline asset: {name}')
    with tempfile.NamedTemporaryFile(dir=cache, prefix=name + '.', delete=False) as stream:
        temporary = Path(stream.name)
        try:
            with urllib.request.urlopen(url, timeout=120) as source:
                shutil.copyfileobj(source, stream)
            stream.close()
            if digest(temporary) != sha:
                raise ValueError(f'SHA256 mismatch for {name}')
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)
    return path


def copy(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
    target.chmod(0o644)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--stage', type=Path, required=True)
    parser.add_argument('--cache', type=Path, required=True)
    parser.add_argument('--offline', action='store_true')
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    # Lock the cache against concurrent packaging runs; never trust an old .tmp.
    import fcntl
    descriptor = os.open(args.cache / '.lock', os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(descriptor, 'a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assets = {name: fetch(args.cache, name, url, sha, args.offline)
                  for name, url, sha in json.loads((HERE / 'runtime-assets.json').read_text())['assets']}
    share = args.stage / 'usr/local/share'
    fonts = share / 'fonts/selawik'
    fonts.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(assets['Selawik_Release.zip']) as archive:
        for name in ('selawk', 'selawkb', 'selawkl', 'selawksb', 'selawksl'):
            (fonts / (name + '.ttf')).write_bytes(archive.read(name + '.ttf'))
    copy(assets['LICENSE-selawik'], fonts / 'LICENSE.txt')
    for name, path in assets.items():
        if name.endswith('.msi'):
            kind = 'mono' if name.startswith('wine-mono') else 'gecko'
            copy(path, share / 'wine' / kind / name)
    graphics = share / 'wineexplorer/dxvk'
    # Extract only named DLLs, never paths supplied by an archive.
    with tarfile.open(assets['dxvk-2.4.1.tar.gz']) as archive:
        for arch in ('x32', 'x64'):
            for dll in ('d3d8', 'd3d9', 'd3d10core', 'd3d11', 'dxgi'):
                target = graphics / arch / (dll + '.dll')
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(f'dxvk-2.4.1/{arch}/{dll}.dll') as source:
                    target.write_bytes(source.read())
    for arch, upstream in (('x32', 'x86'), ('x64', 'x64')):
        for dll in ('d3d12', 'd3d12core'):
            result = subprocess.run(['tar', '--zstd', '-xOf', str(assets['vkd3d-proton-2.13.tar.zst']),
                                     f'vkd3d-proton-2.13/{upstream}/{dll}.dll'], check=True, capture_output=True)
            (graphics / arch / (dll + '.dll')).write_bytes(result.stdout)
    for name, target in (('LICENSE-dxvk', graphics), ('LICENSE-vkd3d', graphics),
                         ('COPYING-vkd3d', graphics), ('COPYING-mono', share / 'wine/mono'),
                         ('LICENSE-mono', share / 'wine/mono')):
        copy(assets[name], target / name)
    for name, path in assets.items():
        if name.startswith('LICENSE-mono-'):
            copy(path, share / 'wine/mono' / name)
    # Gecko's optimized JAR triggers unzip warnings even for correct output.
    # Verify the embedded full notices by content hash, not by ignoring errors.
    with tempfile.TemporaryDirectory(prefix='wex-gecko-notices-') as scratch:
        subprocess.run(['msiextract', '-C', scratch, str(assets['wine-gecko-2.47.4-x86.msi'])],
                       check=True, stdout=subprocess.DEVNULL)
        jar = Path(scratch) / 'gecko/2.47.4/wine_gecko/omni.ja'
        for member, name, sha in (
            ('chrome/toolkit/content/global/license.html', 'license.html',
             'a626b3badf131fa848ae4d2a344a9c6ebe7e6165911a1c4da0bf44e542287a94'),
            ('chrome/toolkit/content/mozapps/extensions/OpenH264-license.txt', 'OpenH264-license.txt',
             '63b75e7d1f5062a6b85e87be72a6158bc6a15807ad91570802fd2d034935e96b')):
            result = subprocess.run(['unzip', '-p', str(jar), member], capture_output=True)
            if result.returncode not in (0, 1, 2) or hashlib.sha256(result.stdout).hexdigest() != sha:
                raise ValueError(f'Cannot verify embedded Gecko notice: {member}')
            (share / 'wine/gecko' / name).write_bytes(result.stdout)
    resource_dir = share / 'wineexplorer/fonts'
    for name in ('font-substitutes.reg', 'font-links.txt'):
        copy(HERE.parent / 'src/resources/fonts' / name, resource_dir / name)
    # Include full copyleft terms beside the runtime notices as well.
    copy(HERE.parent / 'COPYING', share / 'wine/mono/COPYING.GPL')
    copy(HERE.parent / 'wine/COPYING.LIB', share / 'wine/mono/COPYING.LGPL')
    copy(HERE / 'runtime-assets.json', share / 'wineexplorer/runtime-assets.json')
    print('Verified and staged open runtime assets (no proprietary components).')


if __name__ == '__main__':
    main()
