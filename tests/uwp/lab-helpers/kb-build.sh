#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: rebuild kernelbase, apisetschema and the kernelbase process test of candidate u12, run the test for x64 and x86
C=/home/debian/uwp-u1-20261111/build/candidates/u12
cd $C/build
make -j8 dlls/kernelbase/all dlls/apisetschema/all dlls/kernelbase/tests/x86_64-windows/kernelbase_test.exe dlls/kernelbase/tests/i386-windows/kernelbase_test.exe 2>&1 | grep -E " error|Error" | head
export WINEPREFIX=$C/wasdk-run/prefix WINEDEBUG=-all DISPLAY=:99
for a in x86_64 i386; do echo "== $a"; timeout 120 ./wine dlls/kernelbase/tests/$a-windows/kernelbase_test.exe process 2>&1 | tr -d '\r' | tail -5; done
cp dlls/kernelbase/tests/x86_64-windows/kernelbase_test.exe /home/debian/uwp-u1-20261111/claude/kernelbase_test_x64.exe
base64 -w0 /home/debian/uwp-u1-20261111/claude/kernelbase_test_x64.exe > /home/debian/uwp-u1-20261111/claude/kernelbase_test_x64.b64
