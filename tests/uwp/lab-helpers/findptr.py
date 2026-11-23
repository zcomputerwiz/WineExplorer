# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: where a PE64 stores a pointer to code (a vtable slot, a function table entry): findptr.py FILE RVA_LO RVA_HI [CONTEXT_SLOTS]
# prints the address of each qword whose value is an image address in [RVA_LO, RVA_HI] and the qwords around it as RVAs
import struct, sys
d = open(sys.argv[1], 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
o = struct.unpack_from('<H', d, pe + 20)[0]
base = struct.unpack_from('<Q', d, pe + 24 + 24)[0]
lo, hi = int(sys.argv[2], 16) + base, int(sys.argv[3], 16) + base
ctx = int(sys.argv[4]) if len(sys.argv) > 4 else 3
for i in range(n):
    nm, vs, va, rs, ro = struct.unpack_from('<8sIIII', d, pe + 24 + o + i * 40)
    if nm.startswith(b'.text'): continue
    for k in range(0, rs - 8, 8):
        v = struct.unpack_from('<Q', d, ro + k)[0]
        if lo <= v <= hi:
            around = [struct.unpack_from('<Q', d, ro + k + 8 * j)[0] - base if ro + k + 8 * j >= 0 else 0 for j in range(-ctx, ctx + 1)]
            print('slot at RVA %#x (%s), value RVA %#x; around:' % (va + k, nm.decode().strip('\0'), v - base), ' '.join('%x' % a for a in around))
