# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: calls made from a module (by ret= range) before a given message, thread TID: dcalls.py LOG TID MODULE SIZE_HEX "message" [N]
import re, sys
log, tid, mod, size, msg = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4], 16), sys.argv[5]
nlast = int(sys.argv[6]) if len(sys.argv) > 6 else 70
lines = open(log, errors='replace').read().split('\n')
fail = next(i for i, l in enumerate(lines) if msg in l)
base = None
for l in lines[:fail]:
    m = re.search(r'^' + tid + r':Call PE DLL \(proc=\w+,module=([0-9A-F]+) L"' + re.escape(mod) + '"', l)
    if m: base = int(m.group(1), 16)
print('base', hex(base), 'fail line', fail)
out = []
for i, l in enumerate(lines[:fail]):
    if not l.startswith(tid + ':Call '): continue
    m = re.search(r'ret=([0-9a-f]+)', l)
    if not m: continue
    r = int(m.group(1), 16)
    if base <= r < base + size:
        out.append((hex(r - base), l[5:190]))
print(len(out), 'calls; last', nlast)
for o in out[-nlast:]: print(o[0], o[1])
