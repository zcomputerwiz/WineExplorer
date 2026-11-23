# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the ASCII or UTF-16 string at a virtual address of a PE64: readstr.py FILE VA_HEX [VA_HEX...]
import struct, sys
d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
o = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
for a in sys.argv[2:]:
    v = int(a, 16)
    r = v - base if v >= base else v
    for i in range(n):
        nm, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, pe + 24 + o + i * 40)
        if va <= r < va + vs:
            b = d[r - va + ro:r - va + ro + 200]
            if len(b) > 1 and b[1] == 0 and b[0] != 0:
                s = b.decode('utf-16-le', 'replace').split('\0')[0]
            else:
                s = b.split(b'\0')[0].decode('latin-1')
            print(a, repr(s))
