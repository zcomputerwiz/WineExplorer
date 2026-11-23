# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
import re, sys
# guest: the inlined "begin" sites of Input: lea eax,[rcx+1] followed by mov DWORD PTR [rbx|rdi+0x40],eax -> "rva reg"
lines = open('/tmp/input.dis', errors='replace').read().split('\n')
out = []
for i, l in enumerate(lines):
    m = re.match(r'\s*18([0-9a-f]+):\tmov    DWORD PTR \[(rbx|rdi)\+0x40\],eax', l)
    if m and i > 0 and 'lea    eax,[rcx+0x1]' in lines[i - 1]:
        out.append('%x %s' % (int(m.group(1), 16), m.group(2)))
print('\n'.join(out))
