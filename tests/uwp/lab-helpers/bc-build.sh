#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: install the pushed bcp47mrm files into candidate u12, rebuild the module and its test, run the test for x64 and x86
c=/home/debian/uwp-u1-20261111/claude
C=/home/debian/uwp-u1-20261111/build/candidates/u12
S=$C/source/dlls/bcp47mrm
cp $c/bc-main.c $S/main.c
cp $c/bc-spec $S/bcp47mrm.spec
cp $c/bc-test.c $S/tests/bcp47mrm.c
cd $C/build
make -j6 dlls/bcp47mrm/all dlls/bcp47mrm/tests/x86_64-windows/bcp47mrm_test.exe dlls/bcp47mrm/tests/i386-windows/bcp47mrm_test.exe 2>&1 | grep -E " error|Error|warning.*main.c" | head
export WINEPREFIX=$C/wasdk-run/prefix WINEDEBUG=-all DISPLAY=:99
for a in x86_64 i386; do echo "== $a"; timeout 60 ./wine dlls/bcp47mrm/tests/$a-windows/bcp47mrm_test.exe 2>&1 | tr -d '\r' | tail -6; done
cp dlls/bcp47mrm/tests/x86_64-windows/bcp47mrm_test.exe $c/bcp47mrm_test_x64.exe
base64 -w0 $c/bcp47mrm_test_x64.exe > $c/bcp47mrm_test_x64.b64
