# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: callers (e8 rel32 and jmp e9) of a function in a PE, up the call graph: callers.py FILE RVA [DEPTH]
import struct, sys
path = sys.argv[1]
d = open(path, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
o = struct.unpack_from('<H', d, pe + 20)[0]
sec = pe + 24 + o
secs = [struct.unpack_from('<8sIIII', d, sec + i * 40) for i in range(n)]
opt = pe + 24
exc_rva, exc_size = struct.unpack_from('<II', d, opt + 112 + 3 * 8)
def off_of(rva):
    for nm, vs, va, rs, raw in secs:
        if va <= rva < va + max(vs, rs):
            return raw + rva - va
eo = off_of(exc_rva)
funcs = sorted(struct.unpack_from('<II', d, eo + i * 12) for i in range(exc_size // 12))
def func_of(rva):
    lo, hi = 0, len(funcs) - 1
    while lo <= hi:
        m = (lo + hi) // 2
        b, e = funcs[m]
        if rva < b: hi = m - 1
        elif rva >= e: lo = m + 1
        else: return b
    return None
# all call sites: map target -> [call site rvas]
calls = {}
for nm, vs, va, rs, raw in secs:
    if not nm.startswith(b'.text'):
        continue
    for j in range(raw, raw + rs - 5):
        if d[j] == 0xe8:
            rel = struct.unpack_from('<i', d, j + 1)[0]
            t = va + (j - raw) + 5 + rel
            calls.setdefault(t, []).append(va + j - raw)
target = int(sys.argv[2], 16)
depth = int(sys.argv[3]) if len(sys.argv) > 3 else 2
seen = set()
def walk(t, lvl):
    if lvl > depth or t in seen:
        return
    seen.add(t)
    sites = calls.get(t, [])
    print('  ' * lvl + '%x called from %s' % (t, ['%x(f%x)' % (s, func_of(s) or 0) for s in sites][:12]))
    for f in sorted(set(func_of(s) for s in sites if func_of(s))):
        walk(f, lvl + 1)
walk(target, 0)
