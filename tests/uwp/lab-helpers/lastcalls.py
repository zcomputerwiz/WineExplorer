# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the last calls of the thread that took the first unhandled exception of a +relay log, ret= mapped to modules: lastcalls.py LOG [N]
import re, sys
text = open(sys.argv[1], errors='replace').read()
lines = text.split('\n')
n = int(sys.argv[2]) if len(sys.argv) > 2 else 40
mods = {}
for l in lines:
    m = re.search(r'Call PE DLL \(proc=\w+,module=([0-9A-F]+) L"([^"]+)"', l)
    if m:
        mods[int(m.group(1), 16)] = m.group(2)
bases = sorted(mods)
def where(a):
    best = None
    for b in bases:
        if b <= a: best = b
        else: break
    if best is None or a - best > 0x4000000: return '?'
    return '%s+%x' % (mods[best], a - best)
fi = next(i for i, l in enumerate(lines) if 'Unhandled' in l and 'thread' in l)
m = re.search(r'at address ([0-9A-F]+) \(thread (\w+)', lines[fi])
addr, tid = int(m.group(1), 16), m.group(2)[-4:]
print(lines[fi][:140]); print('fault in', where(addr))
calls = [l for l in lines[:fi] if l.startswith(tid + ':Call ') or l.startswith(tid + ':Ret  ')]
for l in calls[-n:]:
    r = re.search(r'ret=([0-9a-f]+)', l)
    print(l[5:150], '=>', where(int(r.group(1), 16)) if r else '')
