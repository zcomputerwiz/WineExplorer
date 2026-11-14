#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
root=/home/debian/uwp-u1-20261111
cd $root/claude
cp wexappx/wexappx.c wexappx/wexappx.c.keep
python3 - <<'PYEOF'
p = 'wexappx/wexappx.c'
s = open(p).read()
old = '        if (manifest.apps[i].entry[0])\n'
assert s.count(old) == 1
s = s.replace(old, '        fwprintf( stderr, L"DBG app %s entry [%s]\\n", manifest.apps[i].id, manifest.apps[i].entry );\n' + old)
open(p, 'w').write(s)
PYEOF
grep -n "DBG app" wexappx/wexappx.c | head -2
bash guest-build-wexappx.sh u11-2 2>&1 | grep -E "error|wexappx.exe" | cut -c1-150
b=$root/build/candidates/u11-2/build
r=$root/build/candidates/u11-2/install2-1
export WINEPREFIX=$r/prefix WINESERVER=$b/server/wineserver WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml=
cp fixture/wexappx.exe $WINEPREFIX/drive_c/windows/system32/wexappx.exe
timeout 120 $b/wine wexappx uninstall WineExplorer.U1View_1.0.0.0_x64__neb1jjyzpw9ht 2>&1 | tr -d '\r' | head -3
timeout 120 $b/wine wexappx install --development "Z:$r/pkg/u11-x64.appx" 2>&1 | tr -d '\r' | head -8
timeout 60 $WINESERVER -k; timeout 60 $WINESERVER -w
cp wexappx/wexappx.c.keep wexappx/wexappx.c
bash guest-build-wexappx.sh u11-2 > /dev/null 2>&1
