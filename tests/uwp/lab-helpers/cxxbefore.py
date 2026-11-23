# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the C++ throws (LABCXX stacks of the ntdll tramp patch) just before a marker line of a +loaddll log, with module names:
# cxxbefore.py LOG MARKER [COUNT]
import re, sys
log, marker = sys.argv[1], sys.argv[2]
count = int(sys.argv[3]) if len(sys.argv) > 3 else 2
mods = {}
lines = open(log, errors='replace').read().split('\n')
last = max(i for i, l in enumerate(lines) if marker in l)
for l in lines:
    m = re.search(r'Loaded (?:L)?"([^"]+)" at ([0-9A-Fa-f]+)', l)
    if m: mods[int(m.group(2), 16)] = m.group(1).split('\\')[-1]
throws = [i for i, l in enumerate(lines[:last]) if 'LABCXX throw' in l]
for t in throws[-count:]:
    print('--- throw at log line', t + 1, 'of marker line', last + 1)
    for l in lines[t + 1:t + 30]:
        m = re.search(r'LABCXX (\d+) module ([0-9A-Fa-f]+) \+([0-9a-f]+)', l)
        if m: print(m.group(1), mods.get(int(m.group(2), 16), m.group(2)), '+' + m.group(3))
        elif 'LABCXX throw' in l or 'LABTRAMP' in l: break
