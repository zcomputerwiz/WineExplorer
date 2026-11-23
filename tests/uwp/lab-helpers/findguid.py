# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: which files under a directory contain a GUID (raw little-endian bytes, as in an IID table or a metadata GuidAttribute), and where:
# findguid.py DIR GUID [GLOB...]
import fnmatch, os, struct, sys, uuid
root, guid = sys.argv[1], sys.argv[2].strip('{}')
pats = sys.argv[3:] or ['*']
raw = uuid.UUID(guid).bytes_le
for base, dirs, files in os.walk(root):
    for f in files:
        if not any(fnmatch.fnmatch(f.lower(), p.lower()) for p in pats): continue
        p = os.path.join(base, f)
        try:
            d = open(p, 'rb').read()
        except OSError:
            continue
        i = d.find(raw)
        while i >= 0:
            print(p, hex(i))
            i = d.find(raw, i + 1)
