# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
import re, sys
p = '/var/lib/wineexplorer/machine/system.reg'
s = open(p, encoding='latin-1').read()
BS = chr(92)
head = '[System' + BS*2 + 'ControlSet001' + BS*2 + 'Services' + BS*2 + 'wexelevate]'
i = s.index(head)
j = s.find('\n[', i + 1)
sec = s[i:j if j > 0 else len(s)]
print(sec[:700])
new_path = '"ImagePath"="C:' + BS*2 + 'windows' + BS*2 + 'system32' + BS*2 + 'wexelevate_test.exe"'
sec2 = re.sub(r'"ImagePath"=[^\n]*', lambda m: new_path, sec)
s = s[:i] + sec2 + s[i + len(sec):]
open(p, 'w', encoding='latin-1').write(s)
print('--- patched'); print(sec2[:700])
