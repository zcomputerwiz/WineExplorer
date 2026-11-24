#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: install the pushed coremessaging files into candidate u12 and rebuild the module
# the workspace of the lab guest (override with UWP_WS)
UWP_WS=${UWP_WS:-/home/debian/uwp-u1-20261111}
c=${UWP_WS}/claude
C=${UWP_WS}/build/candidates/u12
S=$C/source/dlls/coremessaging
rm -f $S/forward.c
cp $c/cm-coreui.c $S/coreui.c
cp $c/cm-spec $S/coremessaging.spec
cp $c/cm-Makefile.in $S/Makefile.in
cd $C/build
make -j6 dlls/coremessaging/all 2>&1 | grep -E " error|Error|warning.*coreui" | head
ls -la dlls/coremessaging/x86_64-windows/coremessaging.dll
