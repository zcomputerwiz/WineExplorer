# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the GUID stored at a virtual address of a PE64 (an IID passed to QueryInterface): readguid.py FILE VA_HEX [VA_HEX...]
import struct, sys
d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
o = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
for a in sys.argv[2:]:
    r = int(a, 16) - base if int(a, 16) >= base else int(a, 16)
    for i in range(n):
        nm, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, pe + 24 + o + i * 40)
        if va <= r < va + vs:
            g = struct.unpack('<IHH8B', d[r - va + ro:r - va + ro + 16])
            print(a, '{%08x-%04x-%04x-%s-%s}' % (g[0], g[1], g[2], ''.join('%02x' % x for x in g[3:5]), ''.join('%02x' % x for x in g[5:])))
