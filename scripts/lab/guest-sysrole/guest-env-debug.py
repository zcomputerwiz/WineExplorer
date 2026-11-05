# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root on the guest: set (or remove with arg "off") WINEDEBUG in the machine-wide environment so processes started by
# the broker (CreateEnvironmentBlock) inherit it.
import sys, re
p = '/var/lib/wineexplorer/machine/system.reg'
s = open(p, encoding='latin-1').read()
BS = chr(92)
head = '[System' + BS*2 + 'CurrentControlSet' + BS*2 + 'Control' + BS*2 + 'Session Manager' + BS*2 + 'Environment]'
val = sys.argv[1] if len(sys.argv) > 1 else 'err+all,fixme+msi,trace+msi,warn+file'
line = '"WINEDEBUG"="' + val + '"'
i = s.find(head)
if i < 0:
    s += '\n' + head + ' 1700000000\n#time=1d9a0000000000\n' + line + '\n'
else:
    j = s.find('\n[', i + 1)
    j = len(s) if j < 0 else j
    sec = s[i:j]
    sec = re.sub(r'\n"WINEDEBUG"="[^\n]*', '', sec)
    if val != 'off':
        sec = sec.rstrip('\n') + '\n' + line + '\n'
    s = s[:i] + sec + s[j:]
open(p, 'w', encoding='latin-1').write(s)
print('ok', val)
