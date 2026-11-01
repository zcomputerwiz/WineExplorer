# SPDX-License-Identifier: GPL-2.0-or-later
"""Build the R6 in-tree runtime archive, including wine.inf metadata resources."""
import hashlib
import io
import os
from pathlib import Path
import re
import tarfile


def required_winmd(build):
    inf = Path(build) / 'wow64/loader/wine.inf'
    if not inf.is_file():
        raise ValueError('Missing recipe wine.inf: ' + str(inf))
    names = []
    in_section = False
    for line in inf.read_text().splitlines():
        line = line.split(';', 1)[0].strip()
        if line.startswith('['):
            in_section = line.lower() == '[winmdfiles]'
        elif in_section and line:
            if not re.fullmatch(r'[a-zA-Z0-9_.-]+\.winmd', line):
                raise ValueError('Invalid WinmdFiles entry: ' + line)
            names.append(line)
    if not names:
        raise ValueError('Recipe wine.inf lacks [WinmdFiles] entries: ' + str(inf))
    return list(dict.fromkeys(names))


def preflight_winmd(build, resource_dir=None):
    names = required_winmd(build)
    resource_dir = Path(resource_dir) if resource_dir is not None else Path(build) / 'include'
    missing = []
    for name in names:
        path = resource_dir / name
        if path.is_symlink() or not path.is_file() or not path.stat().st_size:
            missing.append(name)
    if missing:
        raise ValueError('Host build missing required wine.inf WinmdFiles: ' + ', '.join(missing))
    return names


def deploy_relpaths(build, winmd=()):
    build = Path(build)
    paths = set()
    for root, dirs, files in os.walk(build / 'wow64'):
        dirs[:] = [name for name in dirs if name != 'tests']
        paths.update((Path(root) / name).relative_to(build).as_posix() for name in files)
    paths.update('wow64/include/' + name for name in winmd)
    return sorted(paths)


def build_deploy(build, archive_dir, resource_dir=None):
    build = Path(build)
    resource_dir = Path(resource_dir) if resource_dir is not None else build / 'include'
    winmd = preflight_winmd(build, resource_dir)
    resources = {'wow64/include/' + name: resource_dir / name for name in winmd}
    paths = deploy_relpaths(build, winmd)
    archive_dir = Path(archive_dir)
    archive_dir.mkdir(parents=True, exist_ok=True)
    archive = archive_dir / 'wine-build.tar.gz'
    manifest = {}
    total = 0
    with tarfile.open(archive, 'w:gz') as tar:
        for rel in paths:
            path = resources.get(rel, build / rel)
            data = path.read_bytes()
            manifest[rel] = hashlib.sha256(data).hexdigest()
            stat = path.lstat()
            entry = tarfile.TarInfo(rel)
            entry.mtime = stat.st_mtime
            entry.mode = stat.st_mode & 0o7777
            if path.is_symlink():
                entry.type = tarfile.SYMTYPE
                entry.linkname = os.readlink(path)
                tar.addfile(entry)
            else:
                entry.size = len(data)
                tar.addfile(entry, io.BytesIO(data))
            total += len(data)
    return archive, manifest, total, len(manifest)
