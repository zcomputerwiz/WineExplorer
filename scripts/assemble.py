#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Link project-owned source into Wine, preserving explicitly replaced stubs."""
import argparse
import hashlib
import os
import re
import shutil
from pathlib import Path
import subprocess


def register_project_sources(root, wine):
    """Register the overlay even when the selected Wine base has clean build lists."""
    configure = wine / 'configure.ac'
    text = configure.read_text()
    modules = sorted(kind + '/' + source.name
                     for kind in ('dlls', 'programs')
                     for source in (root / 'src' / kind).iterdir()
                     if (source / 'Makefile.in').is_file())
    declared = {value.split(',', 1)[0].strip()
                for value in re.findall(r'WINE_CONFIG_MAKEFILE\(([^)]+)\)', text)}
    missing = [name for name in modules if name not in declared]
    if missing:
        marker = re.search(r'^WINE_CONFIG_MAKEFILE\(', text, re.M)
        if not marker:
            raise RuntimeError('Wine configure.ac has no module registration list')
        block = ''.join('WINE_CONFIG_MAKEFILE(' + name + ')\n' for name in missing)
        configure.write_text(text[:marker.start()] + block + text[marker.start():])
    includes = wine / 'include/Makefile.in'
    text = includes.read_text()
    headers = sorted(p.name for p in (root / 'src/include').iterdir() if p.is_file())
    listed = set(re.findall(r'^\s*([A-Za-z0-9_.-]+)\s*\\?\s*$', text, re.M))
    missing = [name for name in headers if name not in listed]
    if missing:
        marker = 'SOURCES = ' + chr(92) + '\n'
        if marker not in text:
            raise RuntimeError('Wine include Makefile has no SOURCES list')
        block = ''.join('\t' + name + ' ' + chr(92) + '\n' for name in missing)
        includes.write_text(text.replace(marker, marker + block, 1))


def assemble(root, wine):
    allowed = {line.strip() for line in (root / 'src/stock-replacements.txt').read_text().splitlines()
               if line.strip() and not line.startswith('#')}
    backup = root / 'build/assembly-stock' / hashlib.sha256(str(wine).encode()).hexdigest()[:12]

    def preserve(target, key, source):
        if key not in allowed:
            raise RuntimeError('Nonempty undeclared destination: ' + str(target))
        if target.is_symlink() or not target.is_dir():
            raise RuntimeError('Replacement must be a real module directory: ' + str(target))
        # Require the stock checkout itself to be pristine, including untracked
        # or ignored files. Never archive a user's edited module silently.
        top = subprocess.check_output(['git', '-C', str(wine), 'rev-parse', '--show-toplevel']).decode().strip()
        if Path(top).resolve() != wine:
            raise RuntimeError('Replacement requires a standalone Wine checkout: ' + str(wine))
        subprocess.run(['git', '-C', str(wine), 'diff', '--exit-code', '--quiet', 'HEAD', '--', key], check=True)
        for options in ([], ['--ignored']):
            extra = subprocess.check_output(['git', '-C', str(wine), 'ls-files', '--others',
                                              '--exclude-standard', *options, '--', key])
            if extra.strip():
                raise RuntimeError('Untracked files in replacement: ' + str(target))
        saved = backup / key
        if saved.exists():
            raise RuntimeError('Stock backup already exists: ' + str(saved))
        saved.parent.mkdir(parents=True, exist_ok=True)
        shutil.move(str(target), str(saved))
        print('Preserved stock module:', key)

    def link(source, target, key):
        expected = wine.joinpath(*Path(key).parts[:-1]).resolve()
        if target.parent.resolve() != expected or not expected.is_relative_to(wine):
            raise RuntimeError('Destination escapes Wine checkout: ' + str(target))
        if target.is_symlink():
            if target.resolve() != source.resolve():
                raise RuntimeError('Unexpected source link: ' + str(target))
            return
        if target.exists():
            if target.is_dir() and not any(target.iterdir()):
                target.rmdir()
            else:
                preserve(target, key, source)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.symlink_to(os.path.relpath(source, target.parent), target_is_directory=source.is_dir())

    for kind in ('dlls', 'programs'):
        directory = root / 'src' / kind
        if not directory.exists():
            continue
        for source in sorted(directory.iterdir()):
            if (source / 'Makefile.in').is_file():
                link(source, wine / kind / source.name, kind + '/' + source.name)
    for source in sorted((root / 'src/include').iterdir()):
        if source.is_file():
            link(source, wine / 'include' / source.name, 'include/' + source.name)
    resources = root / 'src/resources'
    if resources.exists():
        (wine / 'resources').mkdir(exist_ok=True)
        for source in sorted(resources.iterdir()):
            link(source, wine / 'resources' / source.name, 'resources/' + source.name)

    register_project_sources(root, wine)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--wine-root', type=Path, required=True)
    args = parser.parse_args()
    assemble(Path(__file__).resolve().parent.parent, args.wine_root.resolve())
