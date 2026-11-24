# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: map the address of the first "Unhandled page fault" of a log (with +loaddll) to a module: mapfault.py LOG
import re, sys
text = open(sys.argv[1], errors='replace').read()
log = text.split('\n')
for m in re.finditer(r'Unhandled (page fault|exception) .*? at address ([0-9A-F]+) \(thread (\w+)', text):
    addr = int(m.group(2), 16)
    best = None
    for l in log:
        mm = re.search(r'Loaded L"([^"]+)" at ([0-9A-F]+):', l)
        if mm:
            b = int(mm.group(2), 16)
            if b <= addr and (best is None or b > best[0]):
                best = (b, mm.group(1))
    print(m.group(0)[:120], '->', hex(best[0]) if best else None, best[1] if best else None, hex(addr - best[0]) if best else None)
    break
