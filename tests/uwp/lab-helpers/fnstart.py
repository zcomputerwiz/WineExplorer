# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the likely function start (the first instruction after a run of int3 padding) at or before addresses of an objdump -d listing:
# fnstart.py DISASM ADDR_HEX...   (addresses as the listing prints them, e.g. 18002a44a)
import re, sys
lines = open(sys.argv[1], errors='replace').read().split('\n')
idx = {}
for i, l in enumerate(lines):
    m = re.match(r'\s*([0-9a-f]+):\t', l)
    if m: idx[int(m.group(1), 16)] = i
for a in sys.argv[2:]:
    v = int(a, 16)
    i = idx[v]
    while i > 0 and 'int3' not in lines[i - 1] and 'nop' not in lines[i - 1][:40] or (i > 0 and not re.match(r'\s*[0-9a-f]+:\t', lines[i - 1])):
        i -= 1
    print(a, '->', lines[i].split(':')[0].strip())
