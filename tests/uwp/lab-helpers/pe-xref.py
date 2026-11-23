# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: find the code that refers to a string of a PE file: pe-xref.py FILE STRING
import struct, sys
path, needle = sys.argv[1], sys.argv[2].encode()
d = open(path, 'rb').read()
pe = struct.unpack_from('<I', d, 0x3c)[0]
n = struct.unpack_from('<H', d, pe + 6)[0]
o = struct.unpack_from('<H', d, pe + 20)[0]
sec = pe + 24 + o
secs = [struct.unpack_from('<8sIIII', d, sec + i * 40) for i in range(n)]
def rva_of(off):
    for nm, vs, va, rs, raw in secs:
        if raw <= off < raw + rs:
            return va + off - raw, nm
i = d.find(b'\0' + needle + b'\0') + 1
srva, snm = rva_of(i)
print('string at rva %x in %s' % (srva, snm))
# code xrefs: lea reg,[rip+disp32] (48/4c 8d xx disp)
for nm, vs, va, rs, raw in secs:
    if not nm.startswith(b'.text'):
        continue
    for j in range(raw, raw + rs - 7):
        if d[j] in (0x48, 0x4c) and d[j + 1] == 0x8d and (d[j + 2] & 0xc7) == 0x05:
            disp = struct.unpack_from('<i', d, j + 3)[0]
            tgt = va + (j - raw) + 7 + disp
            if tgt == srva:
                print('xref at rva %x' % (va + j - raw))
