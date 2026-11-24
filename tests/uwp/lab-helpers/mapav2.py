# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the frames of the Nth "LAB av" of thread TID (1 = the first) of a +seh,+loaddll log: mapav2.py LOG TID [N] [MAXFRAMES]
# Module names come from the +loaddll lines (the bases differ from run to run), offsets are RVAs.
import re, sys
log, tid = sys.argv[1], sys.argv[2]
nth = int(sys.argv[3]) if len(sys.argv) > 3 else 1
maxf = int(sys.argv[4]) if len(sys.argv) > 4 else 60
lines = open(log, errors='replace').read().split('\n')
mods = {}
for l in lines:
    m = re.search(r'Loaded (?:L)?"([^"]+)" at ([0-9A-Fa-f]+)', l)
    if m: mods[int(m.group(2), 16)] = m.group(1).split('\\')[-1]
hits = [i for i, l in enumerate(lines) if l.startswith(tid + ':') and 'LAB av' in l]
if nth < 0 and hits: nth = len(hits) + 1 + nth   # -1 is the last one
count = 0
for i, l in enumerate(lines):
    if l.startswith(tid + ':') and 'LAB av' in l:
        count += 1
        if count != nth: continue
        print(l[:200])
        n = 0
        for k in lines[i + 1:i + 700]:
            m = re.search(r'LAB code\[(\d+)\] ([0-9A-Fa-fx]+)\+([0-9a-f]+)', k)
            if m and k.startswith(tid + ':'):
                base = int(m.group(2), 16)
                print(m.group(1), mods.get(base, hex(base)), '+' + m.group(3))
                n += 1
                if n >= maxf: break
        break
