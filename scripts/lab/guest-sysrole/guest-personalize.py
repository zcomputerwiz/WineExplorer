# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root on the guest: the lab equivalent of the first-logon personalization: replace the template profile name in a user's user.reg.
import sys, os
user = sys.argv[1]
p = '/home/%s/.wine/user.reg' % user
s = open(p, encoding='latin-1').read()
BS = chr(92)
old = 'users' + BS*2 + 'wexsetup'
new = 'users' + BS*2 + user
print(user, 'occurrences', s.count(old), 'USERNAME wexsetup' , s.count('wexsetup'))
s = s.replace(old, new).replace('wexsetup', user)
open(p, 'w', encoding='latin-1').write(s)
os.system('chown %s:%s %s' % (user, user, p))
