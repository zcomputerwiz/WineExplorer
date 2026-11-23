# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
import struct, sys

path = sys.argv[1]
rva = int(sys.argv[2], 16)
d = open(path, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
nsec = struct.unpack_from('<H', d, pe + 6)[0]
optsz = struct.unpack_from('<H', d, pe + 20)[0]
sec = pe + 24 + optsz
secs = []
for i in range(nsec):
    name, vsize, va, rsize, raw = struct.unpack_from('<8sIIII', d, sec + i * 40)
    secs.append((va, max(vsize, rsize), raw))

def off(r):
    for va, sz, raw in secs:
        if va <= r < va + sz:
            return raw + r - va
    raise ValueError(hex(r))

def u32(r):
    return struct.unpack_from('<I', d, off(r))[0]

attrs, unwind, fwd, cta = struct.unpack_from('<IIII', d, off(rva))
print('throwinfo attrs %x unwind %x cta %x' % (attrs, unwind, cta))
n = u32(cta)
for i in range(n):
    ct = u32(cta + 4 + i * 4)
    td = u32(ct + 4)
    nm = d[off(td) + 16: off(td) + 16 + 80].split(b'\0')[0]
    print(i, nm.decode('latin1'))
