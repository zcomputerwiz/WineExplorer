#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: wire a new Wine module into the candidate (configure.ac and include/Makefile.in on the guest), regenerate, build.
# The new files (dlls/elscore/*, include/elscore.h) are already in the source (apply.sh). usage: elscore-wire.sh   log: logs/elscore-wire.log
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2
exec > $root/claude/logs/elscore-wire.log 2>&1
set -x
python3 - <<'PYEOF'
import re
s = '/home/debian/uwp-u1-20261111/build/candidates/u11-2/source/'
p = s + 'configure.ac'
t = open(p).read()
if 'dlls/elscore' not in t:
    t = t.replace('WINE_CONFIG_MAKEFILE(dlls/esent)\n', 'WINE_CONFIG_MAKEFILE(dlls/elscore)\nWINE_CONFIG_MAKEFILE(dlls/esent)\n', 1)
    open(p, 'w').write(t)
p = s + 'include/Makefile.in'
t = open(p).read()
if 'elscore.h' not in t:
    t = t.replace('\tendpointvolume.idl \\\n', '\telscore.h \\\n\tendpointvolume.idl \\\n', 1)
    open(p, 'w').write(t)
PYEOF
cd $c/source && autoconf
cd $c/build && ./config.status --recheck && ./config.status
make -k -j4 include/all dlls/elscore/all
echo "rc=$?"
ls -la dlls/elscore/*/elscore.dll
echo elscore-done
