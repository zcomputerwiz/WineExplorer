#!/bin/bash
# lab: run ntdll tests (om, file) of candidate u12 in a fresh prefix; log logs/ntdll-u12.log
b=/home/debian/uwp-u1-20261111/build/candidates/u12/build
export WINEPREFIX=$b/../ntdll-prefix WINEDEBUG=-all WINESERVER=$b/server/wineserver DISPLAY=:99
l=/home/debian/uwp-u1-20261111/claude/logs/ntdll-u12.log; : > $l
$WINESERVER -k 2>/dev/null; rm -rf $WINEPREFIX; WINEDLLOVERRIDES=mscoree,mshtml= $b/wine wineboot -i > /dev/null 2>&1; $WINESERVER -w
for a in x86_64 i386; do for t in om file; do timeout 600 $b/wine $b/dlls/ntdll/tests/$a-windows/ntdll_test.exe $t >> $l 2>&1; echo "$a $t rc=$?" >> $l; done; done
echo ntdll-done >> $l
