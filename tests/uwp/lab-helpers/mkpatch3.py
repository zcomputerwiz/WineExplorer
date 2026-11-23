# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
import struct, shutil, os, sys
# lab: the original goes to FILE.orig; ud2 (0f 0b) at the given RVAs of a PE: mkpatch3.py FILE RVA_HEX [RVA_HEX...]
p = sys.argv[1]
d = bytearray(open(p, 'rb').read())
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
o = struct.unpack_from('<H', d, pe + 20)[0]
if not os.path.exists(p + '.orig'):
    shutil.copy(p, p + '.orig')
for a in sys.argv[2:]:
    rva = int(a, 16)
    for i in range(n):
        nm, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, pe + 24 + o + i * 40)
        if va <= rva < va + rs:
            off = rva - va + ro
            print('rva %#x orig %s' % (rva, d[off:off + 4].hex()))
            d[off] = 0x0f; d[off + 1] = 0x0b
            break
open(p, 'wb').write(d)
