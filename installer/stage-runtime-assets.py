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


# The WebView2 SDK's loader for the token broker's sign-in window (src/programs/tokenbroker/webview.c), with the
# package's licence (BSD-3-Clause) and third-party notices. Only these members are taken, each pinned by its own hash;
# the extracted copies are kept in the download cache so a build extracts them once.
WEBVIEW2_SDK = '1.0.4258.31'
WEBVIEW2_MEMBERS = (
    ('build/native/x64/WebView2Loader.dll', 'x64/WebView2Loader.dll',
     '3426dcc55fdfb8b5e7ac623cf33b4ccf283fbf68a0c5a65bddfdb4d749a8abee'),
    ('build/native/x86/WebView2Loader.dll', 'x86/WebView2Loader.dll',
     '87c61e2cae380112c60853e28178b8ea9a0d63739fc03cead680d4ad3fde6b7d'),
    ('LICENSE.txt', 'LICENSE.txt', '0af8f1b807512aae39c2ac1aa4d0cae65cabecb6fd554b8439a5162a0d6eca55'),
    ('NOTICE.txt', 'NOTICE.txt', '106423785c5b7eba0a8e61d1837f2132e9c828e20ad530f565d981c1df60dd90'),
)


def stage_webview2_loader(cache, package, target):
    extracted = cache / f'webview2-{WEBVIEW2_SDK}'
    archive = None
    try:
        for member, name, sha in WEBVIEW2_MEMBERS:
            path = extracted / name
            if not (path.is_file() and digest(path) == sha):
                if archive is None:
                    archive = zipfile.ZipFile(package)
                data = archive.read(member)
                if hashlib.sha256(data).hexdigest() != sha:
                    raise ValueError(f'SHA256 mismatch for WebView2 SDK member {member}')
                path.parent.mkdir(parents=True, exist_ok=True)
                # a name of its own: another packaging run may extract the same member at the same time
                with tempfile.NamedTemporaryFile(dir=path.parent, prefix=path.name + '.', delete=False) as stream:
                    stream.write(data)
                os.replace(stream.name, path)
            copy(path, target / name)
    finally:
        if archive is not None:
            archive.close()
    for arch in ('x64', 'x86'):
        for name in ('LICENSE.txt', 'NOTICE.txt'):
            copy(target / name, target / arch / name)


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
    stage_webview2_loader(args.cache, assets['microsoft.web.webview2.1.0.4258.31.nupkg'], share / 'wineexplorer/webview2')
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
