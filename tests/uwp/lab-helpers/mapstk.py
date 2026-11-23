# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: name the modules of "LABSTK n module BASE +OFFSET" lines (ntdll tramp patch) with the +loaddll lines of the same log: mapstk.py LOG [MAXLINES]
import re, sys
log = sys.argv[1]
mx = int(sys.argv[2]) if len(sys.argv) > 2 else 80
mods = {}
stk = []
for l in open(log, errors='replace'):
    m = re.search(r'Loaded (?:L)?"([^"]+)" at ([0-9A-Fa-f]+)', l)
    if m: mods[int(m.group(2), 16)] = m.group(1).split('\\')[-1]
    m = re.search(r'LABSTK (\d+) module ([0-9A-Fa-f]+) \+([0-9a-f]+)', l)
    if m: stk.append((int(m.group(1)), int(m.group(2), 16), m.group(3)))
for n, base, off in stk[:mx]:
    print(n, mods.get(base, hex(base)), '+' + off)
