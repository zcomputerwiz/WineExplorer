#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: wait for the end of a wasdk-run.sh run of candidate u12 (summary has "done"), turn the launch log into text (l.log) and print
# what matters: the unhandled exceptions, calls to unimplemented functions, the lines of the stubs (FILTER, an egrep pattern) and
# the first lines after the first line that matches AFTER (default: none).
# usage: wasdk-report.sh [FILTER [AFTER]]
# the workspace of the lab guest (override with UWP_WS)
UWP_WS=${UWP_WS:-/home/debian/uwp-u1-20261111}
R=${UWP_WS}/build/candidates/u12/wasdk-run
sleep 20
for i in $(seq 1 100); do grep -q "^done" $R/summary 2>/dev/null && break; sleep 5; done
tr -d '\0' < $R/launch.log > $R/l.log
echo "== exceptions"
grep -aE "Unhandled|unimplemented function|NtRaiseException" $R/l.log | cut -c1-230 | head -8
if [ -n "$1" ]; then
  echo "== filter $1"
  grep -aE "$1" $R/l.log | cut -c1-230 | head -30
fi
if [ -n "$2" ]; then
  n=$(grep -anE "$2" $R/l.log | head -1 | cut -d: -f1)
  echo "== after line $n"
  sed -n "$((n+1)),$((n+500))p" $R/l.log | grep -avE "property_value|Etw|Wer|d3d:|wgl|dxcore|dxgi|wined3d|shcore" | cut -c1-230 | head -40
fi
echo "== launcher"; grep -a "launcher exit" $R/summary
