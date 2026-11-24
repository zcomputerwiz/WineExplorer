#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: install the pushed msspellcheckingfacility files (spell1.tar) into candidate u12, regenerate configure, rebuild the
# module and the setup list, and show the result; run detached: setsid nohup spell-build.sh > spell-build.log 2>&1 &
# the workspace of the lab guest (override with UWP_WS)
UWP_WS=${UWP_WS:-/home/debian/uwp-u1-20261111}
g=${UWP_WS}
C=$g/build/candidates/u12
S=$C/source
B=$C/build
cd $S && tar -xf $g/claude/${1:-spell1}.tar && echo untarred
cd $S && autoconf 2>&1 | tail -3 && echo autoconf-done
cd $B && ./config.status --recheck > $g/claude/logs/spell-recheck.log 2>&1; tail -2 $g/claude/logs/spell-recheck.log
./config.status > $g/claude/logs/spell-status.log 2>&1; tail -2 $g/claude/logs/spell-status.log
ls dlls/msspellcheckingfacility | head
make -j6 include/all dlls/msspellcheckingfacility/all loader/all 2>&1 | grep -E "error|Error|warning" | head -20
ls -l dlls/msspellcheckingfacility/*/msspellcheckingfacility.dll 2>/dev/null
echo build-finished
