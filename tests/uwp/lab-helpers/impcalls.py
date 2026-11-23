# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the call sites (ff 15 rel32, and jmp ff 25) of an import of a PE64 by DLL and ordinal or name: impcalls.py FILE DLL ORDINAL|NAME
import struct, sys
path, dll, want = sys.argv[1], sys.argv[2].lower(), sys.argv[3]
d = open(path, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
o = struct.unpack_from('<H', d, pe + 20)[0]
opt = pe + 24
sec = opt + o
secs = [struct.unpack_from('<8sIIII', d, sec + i * 40) for i in range(n)]
def off(rva):
    for nm, vs, va, rs, ro in secs:
        if va <= rva < va + max(vs, rs): return rva - va + ro
    raise ValueError(hex(rva))
imp = struct.unpack_from('<II', d, opt + 112 + 8)[0]
slots = []
p = off(imp)
while True:
    ilt, _, _, name, iat = struct.unpack_from('<IIIII', d, p)
    if not name: break
    if d[off(name):].split(b'\0')[0].decode().lower() == dll:
        t = off(ilt or iat)
        i = 0
        while True:
            v = struct.unpack_from('<Q', d, t + i * 8)[0]
            if not v: break
            if v >> 63: ent = str(v & 0xffff)
            else: ent = d[off(v & 0x7fffffff) + 2:].split(b'\0')[0].decode()
            if ent == want: slots.append(iat + i * 8)
            i += 1
    p += 20
print('IAT slots', [hex(s) for s in slots])
for nm, vs, va, rs, ro in secs:
    if not nm.startswith(b'.text'): continue
    for i in range(rs - 6):
        if d[ro + i] == 0xff and d[ro + i + 1] in (0x15, 0x25):
            rel = struct.unpack_from('<i', d, ro + i + 2)[0]
            if va + i + 6 + rel in slots: print('call site rva', hex(va + i), 'call' if d[ro + i + 1] == 0x15 else 'jmp')
