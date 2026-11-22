#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
"""Variants of a signed cabinet that each break it in one way (byte flips; nothing is re-signed).
usage: tamper-cab.py SIGNED.cab UNSIGNED.cab OUTDIR"""
import os
import struct
import sys

d = open(sys.argv[1], 'rb').read()
out = sys.argv[3]
os.makedirs(out, exist_ok=True)
cert, size = struct.unpack_from('<II', d, 0x28 + 4)
coff = struct.unpack_from('<I', d, 0x10)[0]


def flip(name, at, mask=0x20):
    b = bytearray(d)
    b[at] ^= mask
    open(os.path.join(out, name), 'wb').write(b)


flip('bad-data.cab', cert - 100)       # a byte of the compressed data
flip('bad-name.cab', coff + 16 + 2)    # a byte of a file name in a CFFILE
flip('bad-flags.cab', 0x1e + 1)        # the flags of the header (hashed)
flip('bad-version.cab', 0x18, 0x01)    # the minor version, still a valid one (hashed)
flip('bad-setid.cab', 0x20)            # the set id (hashed)
open(os.path.join(out, 'trailing.cab'), 'wb').write(d + b'x')   # data after the signature
open(os.path.join(out, 'no-signature.cab'), 'wb').write(open(sys.argv[2], 'rb').read())
