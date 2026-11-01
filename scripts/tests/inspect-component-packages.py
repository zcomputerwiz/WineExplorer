#!/usr/bin/python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Offline research inventory. Never installs/executes PE payloads or emits terms.

Requires user-acquired packages in a private cache. It does NOT authorize those
packages for OOBE, change production policy, or establish license permissions.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess

spec = importlib.util.spec_from_file_location('components', Path(__file__).resolve().parents[1] / 'wex-components.py')
c = importlib.util.module_from_spec(spec)
spec.loader.exec_module(c)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def inventory(cache):
    records = []
    for name, expected in c.COMPONENTS_METADATA['corefonts']['recipe_files']:
        path = cache / name
        data = path.read_bytes()
        if digest(data) != expected:
            raise ValueError('Unapproved font package: ' + name)
        resource = subprocess.run(['wrestool', '-x', '-R', '-t10', '-nLICENSE', '-L1033', str(path)],
                                  capture_output=True, check=True, timeout=20).stdout
        if not 500 <= len(resource) <= 65536:
            raise ValueError('Missing/bounded embedded license: ' + name)
        # Preserve original resource identity; text stays in the private cache.
        text = resource.rstrip(b'\0').decode('cp1252', errors='strict')
        if '\0' in text:
            raise ValueError('Unexpected embedded NUL in terms')
        listing = subprocess.run(['cabextract', '-l', str(path)], capture_output=True,
                                 check=True, timeout=20).stdout.decode('utf-8')
        fonts = {}
        for line in listing.splitlines():
            member = line.rsplit('|', 1)[-1].strip()
            if not member.lower().endswith('.ttf'):
                continue
            if not re.fullmatch(r'[A-Za-z0-9_.-]+', member):
                raise ValueError('Unexpected font member path')
            content = subprocess.run(['cabextract', '-p', '-F', member, str(path)],
                                     capture_output=True, check=True, timeout=20).stdout
            if content[:4] not in (b'\0\1\0\0', b'OTTO'):
                raise ValueError('Not an SFNT font: ' + member)
            fonts[member] = {'size': len(content), 'sha256': digest(content)}
        if not fonts:
            raise ValueError('No font members in ' + name)
        records.append({'name': name, 'size': len(data), 'sha256': expected,
                        'license_resource_sha256': digest(resource), 'license_resource_bytes': len(resource),
                        'license_decoding': 'cp1252; terminal NUL removed, no other transformations', 'fonts': fonts})
    return {'scope': 'offline package inventory only, not install/permission acceptance', 'corefonts': records}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cache', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(inventory(args.cache.resolve(strict=True)), indent=2))
