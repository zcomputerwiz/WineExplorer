#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Compare retained stock files with the pinned source, excluding M/N items."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--git-dir', default='build/refactor/wine.git')
    parser.add_argument('--branch', default='wineexplorer/11.19')
    parser.add_argument('--map', type=Path, default=Path('docs/refactor/PATHS.json'))
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    mapping = json.loads(args.map.read_text())

    def git(*command):
        return subprocess.check_output(['git', '--git-dir=' + args.git_dir, *command])

    def tree(ref):
        result = {}
        for entry in git('ls-tree', '-r', '-z', ref).split(b'\0'):
            if not entry:
                continue
            metadata, path = entry.split(b'\t', 1)
            mode, kind, oid = metadata.decode().split()
            result[path.decode()] = {'mode': mode, 'kind': kind, 'oid': oid}
        return result

    source = mapping['revisions']['source']
    before = tree(args.branch)
    after = tree(source + ':wine')
    rows = {r['path']: r for r in mapping['files']}
    pending, noise = [], []
    owned = 0
    for path in sorted(set(before) | set(after)):
        if before.get(path) == after.get(path):
            continue
        row = rows.get(path)
        if not row:
            raise ValueError('Unmapped difference: ' + path)
        decisions = row.get('hunk_decisions', [])
        classes = sorted({d['class'] for d in decisions} if decisions else {row['class']})
        if classes == ['M']:
            owned += 1
        elif classes == ['N']:
            noise.append(path)
        else:
            pending.append({'path': path, 'classes': classes, 'target': row['target']})
    result = {'source': source, 'branch': args.branch,
              'head': git('rev-parse', args.branch).decode().strip(),
              'stock_pending': pending, 'owned_files_pending': owned,
              'intentional_noise_differences': noise}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(f'{len(pending)} stock paths pending; {owned} owned paths reserved for assembly; '
          f'{len(noise)} noise paths restored to upstream.')


if __name__ == '__main__':
    main()
