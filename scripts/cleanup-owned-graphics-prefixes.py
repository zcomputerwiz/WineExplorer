#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Dry-run first: remove only obsolete, stopped, explicitly owned Wine prefixes.

Logs, screenshots, results, profiles, builds, runtimes and all r4-* runs stay.
No VM images or other workers' trees are candidates. Deletion is not reversible.
"""
import argparse
import fcntl
import json
from pathlib import Path
import shutil
import subprocess

ROOT = Path('/var/tmp/wex-g4-tree-20261020')
BUILD = '/var/tmp/wex-g3-repair-20261018/clean-source/build/wow64'


def eligible(run):
    if run.is_symlink() or not run.is_dir() or run.name.startswith(('r4-', 'private-')):
        return None
    prefix = run / 'prefix'
    if prefix.is_symlink() or not prefix.is_dir() or prefix.resolve() != prefix:
        return None
    if not all((prefix / name).is_file() for name in ('system.reg', 'user.reg')):
        return None
    if not (prefix / 'drive_c').is_dir():
        return None
    report = run / 'results.json'
    if not report.is_file() or report.is_symlink():
        return None
    result = json.loads(report.read_text())
    if (result.get('wine_build') != BUILD
            or result.get('environment', {}).get('WINEPREFIX') != str(prefix)
            or result.get('cleanup', {}).get('remaining_servers') != []
            or result.get('profile_token') or result.get('restart_from') or result.get('profile_diagnostic')):
        return None
    return prefix


def assert_stopped(prefix):
    wanted = b'WINEPREFIX=' + str(prefix).encode()
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            environment = (proc / 'environ').read_bytes().split(bytes([0]))
        except (FileNotFoundError, ProcessLookupError):
            continue
        if wanted in environment:
            raise RuntimeError('Live process uses prefix: ' + str(prefix))
    for row in Path('/proc/self/mountinfo').read_text().splitlines():
        mount = row.split()[4]
        if mount == str(prefix) or mount.startswith(str(prefix) + '/'):
            raise RuntimeError('Mounted filesystem under prefix: ' + str(prefix))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--apply', action='store_true', help='Delete ONLY targets in the reviewed dry-run manifest')
    args = parser.parse_args()
    if ROOT.resolve(strict=True) != ROOT or ROOT.is_symlink():
        raise RuntimeError('Owned root changed')
    if not args.apply:
        entries = []
        for run in sorted(ROOT.iterdir()):
            prefix = eligible(run)
            if prefix is None:
                continue
            assert_stopped(prefix)
            size = int(subprocess.check_output(['du', '-sx', '--block-size=1', str(prefix)], text=True).split()[0])
            entries.append(dict(path=str(prefix), bytes=size, inode=prefix.stat().st_ino,
                                registry_mtime=(prefix / 'system.reg').stat().st_mtime_ns))
        manifest = dict(root=str(ROOT), entries=entries, total_bytes=sum(item['bytes'] for item in entries))
        args.manifest.write_text(json.dumps(manifest, indent=2) + '\n')
        print(json.dumps(manifest, indent=2))
        return
    manifest = json.loads(args.manifest.read_text())
    if manifest['root'] != str(ROOT):
        raise RuntimeError('Manifest root mismatch')
    # Validate ALL targets before performing ANY deletion.
    for item in manifest['entries']:
        prefix = Path(item['path'])
        if (prefix.name != 'prefix' or prefix.parent.parent != ROOT or eligible(prefix.parent) != prefix
                or prefix.stat().st_ino != item['inode']
                or (prefix / 'system.reg').stat().st_mtime_ns != item['registry_mtime']):
            raise RuntimeError('Target changed since review: ' + str(prefix))
        assert_stopped(prefix)
    removed = []
    try:
        for item in manifest['entries']:
            prefix = Path(item['path'])
            with (prefix.parent / '.consumer.lock').open('a') as lock:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                if eligible(prefix.parent) != prefix:
                    raise RuntimeError('Target changed before delete: ' + str(prefix))
                assert_stopped(prefix)
                shutil.rmtree(prefix)  # validated exact leaf, symlinks are unlinked, never followed
            removed.append(item)
            print('Removed', prefix, flush=True)
    finally:
        args.manifest.with_suffix('.removed.json').write_text(json.dumps(dict(removed=removed,
            total_bytes=sum(item['bytes'] for item in removed)), indent=2) + '\n')


if __name__ == '__main__':
    main()
