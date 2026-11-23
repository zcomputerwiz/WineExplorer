# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the stack words (LAB code) after the first "LAB av" of a +seh,+loaddll log, resolved to module names: mapav.py LOG [MAXFRAMES]
import re, sys
lines = open(sys.argv[1], errors='replace').read().split('\n')
maxf = int(sys.argv[2]) if len(sys.argv) > 2 else 60
names = {}
for l in lines:
    m = re.search(r'Loaded L"([^"]+)" at ([0-9A-F]+):', l)
    if m:
        names[int(m.group(2), 16)] = m.group(1).split('\\')[-1]
start = next(i for i, l in enumerate(lines) if 'LAB av' in l)
print(lines[start][:200])
n = 0
for l in lines[start + 1:start + 2000]:
    m = re.search(r'LAB code\[(\d+)\] ([0-9a-fx]+)\+([0-9a-f]+)', l)
    if m:
        b = int(m.group(2), 16)
        print(m.group(1), names.get(b, hex(b)), '+' + m.group(3))
        n += 1
        if n >= maxf: break
