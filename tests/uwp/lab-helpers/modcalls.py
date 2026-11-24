# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the last calls of thread TID made from a module (ret= in the module) before a message, in a +relay,+loaddll log:
# modcalls.py LOG TID MODULE_BASENAME "message" [N]
import re, sys
log, tid, modname, msg = sys.argv[1:5]
n = int(sys.argv[5]) if len(sys.argv) > 5 else 25
lines = open(log, errors='replace').read().split('\n')
f = next(i for i, l in enumerate(lines) if msg in l)
mods = []
for l in lines:
    m = re.search(r'Loaded (?:L)?"([^"]+)" at ([0-9A-Fa-f]+)', l)
    if m: mods.append((int(m.group(2), 16), m.group(1).split('\\')[-1]))
mods.sort()
def mod(a):
    b = None
    for base, name in mods:
        if base <= a: b = (base, name)
    return b
out = []
for l in lines[:f]:
    if not l.startswith(tid + ':Call '): continue
    m = re.search(r'ret=([0-9a-f]+)', l)
    if not m: continue
    r = int(m.group(1), 16)
    b = mod(r)
    if b and b[1].lower() == modname.lower():
        out.append(l[:190] + '  [%s+%x]' % (b[1], r - b[0]))
print('\n'.join(out[-n:]))
