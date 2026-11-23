#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: install the pushed windows.storage.applicationdata files into candidate u12, rebuild the module and its test, run the test
c=/home/debian/uwp-u1-20261111/claude
C=/home/debian/uwp-u1-20261111/build/candidates/u12
S=$C/source
cp $c/ad-windows.storage.idl $S/include/windows.storage.idl
D=$S/dlls/windows.storage.applicationdata
cp $c/ad-appdatapaths.c $D/appdatapaths.c
cp $c/ad-classes.idl $D/classes.idl
cp $c/ad-main.c $D/main.c
cp $c/ad-private.h $D/private.h
cp $c/ad-Makefile.in $D/Makefile.in
cp $c/ad-test.c $D/tests/data.c
cp $c/ad-test-Makefile.in $D/tests/Makefile.in
cd $C/build
make -j6 include/all dlls/windows.storage.applicationdata/all dlls/windows.storage.applicationdata/tests/x86_64-windows/windows.storage.applicationdata_test.exe dlls/windows.storage.applicationdata/tests/i386-windows/windows.storage.applicationdata_test.exe 2>&1 | grep -E " error|Error|warning.*(appdatapaths|data.c)" | head -20
export WINEPREFIX=$C/wasdk-run/prefix WINEDEBUG=-all DISPLAY=:99
for a in x86_64 i386; do echo "== $a"; timeout 90 ./wine dlls/windows.storage.applicationdata/tests/$a-windows/windows.storage.applicationdata_test.exe 2>&1 | tr -d '\r' | tail -8; done
cp dlls/windows.storage.applicationdata/tests/x86_64-windows/windows.storage.applicationdata_test.exe $c/ad_test_x64.exe
base64 -w0 $c/ad_test_x64.exe > $c/ad_test_x64.b64
