# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: replace bytes at an RVA of a PE, checking the old ones first (the original goes to FILE.orig): bytepatch.py FILE RVA_HEX OLD_HEX NEW_HEX
import struct, shutil, os, sys
p, rva, old, new = sys.argv[1], int(sys.argv[2], 16), bytes.fromhex(sys.argv[3]), bytes.fromhex(sys.argv[4])
d = bytearray(open(p, 'rb').read())
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
o = struct.unpack_from('<H', d, pe + 20)[0]
for i in range(n):
    nm, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, pe + 24 + o + i * 40)
    if va <= rva < va + rs:
        off = rva - va + ro
        if bytes(d[off:off + len(old)]) != old:
            print('old bytes are', bytes(d[off:off + len(old)]).hex(), 'not', old.hex()); sys.exit(1)
        if not os.path.exists(p + '.orig'): shutil.copy(p, p + '.orig')
        d[off:off + len(new)] = new
        open(p, 'wb').write(d)
        print('patched %#x: %s -> %s' % (rva, old.hex(), new.hex()))
        break
