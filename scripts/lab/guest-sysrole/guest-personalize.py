# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root on the guest: use the product helper, staged by wexdev as /tmp/gsr/
# personalize-prefix.sh. Keep this Python entry point for the historical recipe.
import os, subprocess, sys
user = sys.argv[1]
subprocess.check_call(['/tmp/gsr/personalize-prefix.sh', '/home/%s/.wine' % user, user])
uid = int(subprocess.check_output(['id', '-u', user]))
gid = int(subprocess.check_output(['id', '-g', user]))
os.chown('/home/%s/.wine/user.reg' % user, uid, gid)
