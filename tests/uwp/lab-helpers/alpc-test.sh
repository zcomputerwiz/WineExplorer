#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: rebuild ntdll (unix side) and the ntdll tests of candidate u12, run the alpc test under Wine for x64 and x86
c=/home/debian/uwp-u1-20261111/claude
C=/home/debian/uwp-u1-20261111/build/candidates/u12
S=$C/source
cp $c/alpc-unix.c $S/dlls/ntdll/unix/alpc.c
cp $c/alpc-test.c $S/dlls/ntdll/tests/alpc.c
cp $c/wow64-syscall.c $S/dlls/wow64/syscall.c
cd $C/build
make -j6 dlls/wow64/all dlls/ntdll/ntdll.so dlls/ntdll/tests/x86_64-windows/ntdll_test.exe dlls/ntdll/tests/i386-windows/ntdll_test.exe 2>&1 | grep -E "error|Error|warning: .*alpc" | head
export WINEPREFIX=$C/wasdk-run/prefix WINEDEBUG=-all DISPLAY=:99
./server/wineserver -k; sleep 1
for a in x86_64 i386; do
  echo "== $a"
  timeout 120 ./wine dlls/ntdll/tests/$a-windows/ntdll_test.exe alpc 2>&1 | tr -d '\r' | head -40
done
cp dlls/ntdll/tests/x86_64-windows/ntdll_test.exe $c/ntdll_test_x64.exe
base64 -w0 $c/ntdll_test_x64.exe > $c/ntdll_test_x64.b64
