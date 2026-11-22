#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: wire new Wine modules into the candidate (configure.ac on the guest), regenerate, build the named targets.
# The new files are already in the source (apply.sh). usage: modwire.sh "dlls/NAME dlls/NAME/tests ..." "TARGET ..."
#   log: logs/modwire.log
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2
exec > $root/claude/logs/modwire.log 2>&1
set -x
python3 - "$1" <<'PYEOF'
import sys
p = '/home/debian/uwp-u1-20261111/build/candidates/u11-2/source/configure.ac'
t = open(p).read()
for m in sys.argv[1].split():
    line = 'WINE_CONFIG_MAKEFILE(%s)\n' % m
    if line not in t:
        t = t.replace('WINE_CONFIG_MAKEFILE(dlls/esent)\n', line + 'WINE_CONFIG_MAKEFILE(dlls/esent)\n', 1)
open(p, 'w').write(t)
PYEOF
cd $c/source && autoconf
cd $c/build && ./config.status --recheck && ./config.status
make -k -j4 $2
echo "rc=$?"
echo modwire-done
