# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: wide-string cross reference with the containing function (.pdata): wxref.py FILE STRING [STRING...]
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
def rva_of(off):
    for nm, vs, va, rs, raw in secs:
        if raw <= off < raw + rs:
            return va + off - raw
def off_of(rva):
    for nm, vs, va, rs, raw in secs:
        if va <= rva < va + max(vs, rs):
            return raw + rva - va
funcs = []
eo = off_of(exc_rva)
for i in range(exc_size // 12):
    b, e, u = struct.unpack_from('<III', d, eo + i * 12)
    funcs.append((b, e))
def func_of(rva):
    lo, hi = 0, len(funcs) - 1
    while lo <= hi:
        m = (lo + hi) // 2
        b, e = funcs[m]
        if rva < b: hi = m - 1
        elif rva >= e: lo = m + 1
        else: return b, e
    return None
def refs_to(trva):
    out = []
    for nm, vs, va, rs, raw in secs:
        if not nm.startswith(b'.text'):
            continue
        for j in range(raw, raw + rs - 7):
            if d[j] in (0x48, 0x4c) and d[j + 1] == 0x8d and (d[j + 2] & 0xc7) == 0x05:
                disp = struct.unpack_from('<i', d, j + 3)[0]
                if va + (j - raw) + 7 + disp == trva:
                    out.append(va + j - raw)
    return out
if sys.argv[2] == '--rva':
    for h in sys.argv[3:]:
        r = int(h, 16)
        xs = refs_to(r)
        print('%x' % r, 'xrefs:', ['%x' % x for x in xs], 'funcs:', [('%x-%x' % func_of(x)) if func_of(x) else None for x in xs])
    sys.exit()
for s in sys.argv[2:]:
    b = s.encode('utf-16le') + b'\0\0'
    i = d.find(b)
    if i < 0:
        print(s, 'not found'); continue
    r = rva_of(i)
    xs = refs_to(r)
    print(s, 'at rva %x' % r, 'xrefs:', ['%x' % x for x in xs], 'funcs:', [('%x-%x' % func_of(x)) if func_of(x) else None for x in xs])
