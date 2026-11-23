# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the functions of a disassembly (objdump -d -M intel --no-show-raw-insn, on stdin or a file) that call a getter (a CALL target) and
# then a virtual method at a given vtable offset of the result: singleton-calls.py DISASM GETTER_ADDR_HEX OFFSET_HEX [WINDOW]
import re, sys
lines = open(sys.argv[1], errors='replace').read().split('\n')
getter, off = sys.argv[2].lower().replace('0x', ''), sys.argv[3].lower().replace('0x', '')
window = int(sys.argv[4]) if len(sys.argv) > 4 else 10
for i, l in enumerate(lines):
    if re.search(r'call\s+0x' + getter + r'\s*$', l):
        for k in lines[i + 1:i + 1 + window]:
            if re.search(r'\[r[a-z0-9]+\+0x' + off + r'\]', k):
                print(l.strip()); print('   ', k.strip()); break
