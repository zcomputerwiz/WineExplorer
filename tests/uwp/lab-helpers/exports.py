# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the named exports of a PE (name, ordinal, RVA): exports.py FILE [SUBSTRING]
import struct, sys
d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
o = struct.unpack_from('<H', d, pe + 20)[0]
opt = pe + 24
magic = struct.unpack_from('<H', d, opt)[0]
dd = opt + (112 if magic == 0x20b else 96)
secs = [struct.unpack_from('<8sIIII', d, pe + 24 + o + i * 40) for i in range(n)]
def off(r):
    for nm, vs, va, rs, ro in secs:
        if va <= r < va + max(vs, rs): return r - va + ro
exp = struct.unpack_from('<I', d, dd)[0]
e = off(exp)
base, nfun, nnames, afun, anames, aords = struct.unpack_from('<IIIIII', d, e + 16)
for i in range(nnames):
    name = d[off(struct.unpack_from('<I', d, off(anames) + 4 * i)[0]):].split(b'\0')[0].decode()
    ordn = struct.unpack_from('<H', d, off(aords) + 2 * i)[0]
    rva = struct.unpack_from('<I', d, off(afun) + 4 * ordn)[0]
    if len(sys.argv) < 3 or sys.argv[2] in name:
        print('%-50s ordinal %3d rva %#x' % (name, ordn + base, rva))
