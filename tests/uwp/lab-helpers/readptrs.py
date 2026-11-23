# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: N qwords at a virtual address of a PE64 (a function table in .rdata): readptrs.py FILE VA_HEX COUNT
import struct, sys
d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
o = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
v = int(sys.argv[2], 16)
r = v - base if v >= base else v
for i in range(n):
    nm, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, pe + 24 + o + i * 40)
    if va <= r < va + vs:
        for k in range(int(sys.argv[3])):
            print(hex(v + 8 * k), hex(struct.unpack_from('<Q', d, r - va + ro + 8 * k)[0]))
