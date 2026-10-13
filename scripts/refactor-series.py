#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Export exact reviewed text hunks for one retained stock-Wine change."""
import argparse
import hashlib
import json
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--evidence', type=Path, default=Path('build/refactor/baseline'))
    parser.add_argument('--map', type=Path, default=Path('docs/refactor/PATHS.json'))
    parser.add_argument('--class', dest='classification', choices=['U', 'X', 'H', 'B'], default='U')
    parser.add_argument('--target', help='Target before the first semicolon, exactly as in the map')
    parser.add_argument('--output', type=Path)
    parser.add_argument('--hunk', action='append', default=[],
                        help='Export only these exact SHA-256 hunks within the target (repeatable)')
    args = parser.parse_args()
    evidence = json.loads(args.evidence.with_suffix('.json').read_text())
    mapping = json.loads(args.map.read_text())
    patch = args.evidence.with_suffix('.patch').read_bytes()
    assert evidence['revisions'] == mapping['revisions'], 'Revision pin mismatch'
    assert hashlib.sha256(patch).hexdigest() == mapping['patch_sha256'], 'Patch hash mismatch'
    blocks = {}
    for block in re.split(br'(?=^diff --git )', patch, flags=re.M):
        if block:
            match = re.match(br'diff --git a/(.+) b/\1\n', block)
            assert match, 'Unsupported patch path'
            blocks[match[1].decode()] = block
    groups = {}
    whole_files = {}
    records = {r['path']: r for r in evidence['files']}
    for row in mapping['files']:
        block = blocks[row['path']]
        assert hashlib.sha256(block).hexdigest() == row['patch_sha256']
        hunks = {hashlib.sha256(h).hexdigest(): h for h in
                 re.split(br'(?=^@@ )', block, flags=re.M)[1:]}
        if not row.get('hunk_decisions') and row['class'] == args.classification:
            target = row['target'].split(';')[0]
            groups.setdefault(target, {}).setdefault(row['path'], [])
            whole_files[row['path']] = block
        for decision in row.get('hunk_decisions', []):
            if decision['class'] != args.classification:
                continue
            target = decision['target'].split(';')[0]
            hunk = hunks[decision['sha256']]
            groups.setdefault(target, {}).setdefault(row['path'], []).append(hunk)
    if not args.target:
        for target, files in sorted(groups.items()):
            print(f'{target}: {len(files)} paths, {sum(map(len, files.values()))} hunks')
        return
    if not args.output:
        parser.error('--target requires --output')
    files = groups[args.target]
    if args.hunk:
        requested = set(args.hunk)
        available = {hashlib.sha256(h).hexdigest() for hunks in files.values() for h in hunks}
        assert requested <= available, 'Selected hunk is absent from the reviewed target'
        files = {path: [h for h in hunks if hashlib.sha256(h).hexdigest() in requested]
                 for path, hunks in files.items()}
        files = {path: hunks for path, hunks in files.items() if hunks}
    output = bytearray()
    for path, hunks in sorted(files.items()):
        if path in whole_files:
            output.extend(whole_files[path])
            continue
        # No full-file blob ids: these are selected hunks, not the full patch.
        record = records[path]
        output.extend(f'diff --git a/{path} b/{path}\n'.encode())
        if not record['before']:
            assert len(hunks) == len(record['hunks']), 'Partial new-file export'
            output.extend(f'new file mode {record["after"]["mode"]}\n'.encode())
        if not record['after']:
            assert len(hunks) == len(record['hunks']), 'Partial deleted-file export'
            output.extend(f'deleted file mode {record["before"]["mode"]}\n'.encode())
        old_path = 'a/' + path if record['before'] else '/dev/null'
        new_path = 'b/' + path if record['after'] else '/dev/null'
        output.extend(f'--- {old_path}\n+++ {new_path}\n'.encode())
        output.extend(b''.join(hunks))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print(f'Exported {len(files)} paths to {args.output}')


if __name__ == '__main__':
    main()
