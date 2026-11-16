# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""The SDK classes a set of .winmd files references: a class counts when its namespace and its name are both strings of
the metadata (a TypeRef names both). Prints those the source scan did not find.
usage: winmd-refs.py WINMD_STRINGS SDK_CLASSES SOURCE_USES_TSV"""
import sys

strings = set(l.strip() for l in open(sys.argv[1], encoding='utf-8', errors='replace'))
sdk = [l.strip() for l in open(sys.argv[2], encoding='utf-8') if l.strip()]
used = {l.split('\t')[0] for l in open(sys.argv[3], encoding='utf-8')}
refs = [c for c in sdk if c.rsplit('.', 1)[0] in strings and c.rsplit('.', 1)[1] in strings]
print(len(refs), 'referenced;', 'not in the source scan:')
for c in refs:
    if c not in used: print(' ', c)
