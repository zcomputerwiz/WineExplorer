#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Assemble pinned project modules in a temporary Git index and prove equality."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--git-dir', default='build/refactor/wine.git')
    parser.add_argument('--branch', default='wineexplorer/11.19')
    parser.add_argument('--map', type=Path, default=Path('docs/refactor/PATHS.json'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--patch', type=Path, help='Export a binary patch against the pinned fork master')
    args = parser.parse_args()
    mapping = json.loads(args.map.read_text())

    def git(*command, env=None, data=None):
        return subprocess.check_output(['git', '--git-dir=' + args.git_dir, *command], env=env, input=data)

    def tree(ref):
        result = {}
        for entry in git('ls-tree', '-r', '-z', ref).split(b'\0'):
            if entry:
                metadata, path = entry.split(b'\t', 1)
                mode, kind, oid = metadata.decode().split()
                assert kind == 'blob', 'Unexpected non-file entry: ' + path.decode()
                result[path.decode()] = (mode, oid)
        return result

    source = mapping['revisions']['source']
    stock_head = git('rev-parse', args.branch).decode().strip()
    source_tree = tree(source + ':wine')
    stock_tree = tree(stock_head)
    classes = {}
    for row in mapping['files']:
        decisions = row.get('hunk_decisions', [])
        classes[row['path']] = {d['class'] for d in decisions} if decisions else {row['class']}
    updates = bytearray()
    owned_paths = []
    for path in sorted(classes):
        if classes[path] != {'M'}:
            continue
        owned_paths.append(path)
        mode, oid = source_tree.get(path, ('0', '0' * 40))
        updates.extend(f'{mode} {oid}\t{path}\0'.encode())
    with tempfile.TemporaryDirectory(prefix='wex-assemble-') as temporary:
        env = dict(os.environ, GIT_INDEX_FILE=str(Path(temporary) / 'index'))
        git('read-tree', stock_head, env=env)
        git('update-index', '-z', '--index-info', env=env, data=bytes(updates))
        assembled = git('write-tree', env=env).decode().strip()
    assembled_tree = tree(assembled)
    differences = sorted(path for path in set(source_tree) | set(assembled_tree)
                         if source_tree.get(path) != assembled_tree.get(path))
    unexpected = [path for path in differences if classes.get(path) != {'N'}]
    assert not unexpected, 'Assembled source mismatch: ' + ', '.join(unexpected)
    # Noise stays exactly as it was in the retained stock branch, never silently copied.
    assert all(assembled_tree.get(p) == stock_tree.get(p) for p in differences)
    result = {'source': source, 'stock_head': stock_head, 'assembled_tree': assembled,
              'owned_paths': owned_paths, 'unexpected_differences': unexpected,
              'intentional_noise_differences': differences}
    if args.patch:
        patch = git('diff', '--binary', '--no-renames', mapping['revisions']['fork_master'], assembled, '--')
        args.patch.parent.mkdir(parents=True, exist_ok=True)
        args.patch.write_bytes(patch)
        result['patch_base'] = mapping['revisions']['fork_master']
        result['patch_sha256'] = hashlib.sha256(patch).hexdigest()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f'Assembled {len(owned_paths)} owned paths: {assembled}; '
          f'zero unexpected differences, {len(differences)} classified noise differences.')


if __name__ == '__main__':
    main()
