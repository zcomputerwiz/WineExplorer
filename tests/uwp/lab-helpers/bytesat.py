# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the bytes at RVAs of a PE: bytesat.py FILE COUNT RVA_HEX...
import struct, sys
d = open(sys.argv[1], 'rb').read()
n_bytes = int(sys.argv[2])
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
o = struct.unpack_from('<H', d, pe + 20)[0]
for a in sys.argv[3:]:
    rva = int(a, 16)
    for i in range(n):
        nm, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, pe + 24 + o + i * 40)
        if va <= rva < va + rs:
            print(a, d[rva - va + ro:rva - va + ro + n_bytes].hex())
