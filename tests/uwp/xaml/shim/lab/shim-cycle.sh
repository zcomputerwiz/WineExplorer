#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: tar the probe stage and the published shim, run cxprobe-run.sh RUN in the guest under xvfb, wait, print summary
# usage: shim-cycle.sh RUN [WINEDEBUG]
pad=/c/Users/Owner/AppData/Local/Temp/claude/f10dd599a76cdd8a/cc6dd05f-7e73-49f2-be6b-aa4119777252/scratchpad
cd $pad || exit 1
cp /d/DeepThought/Projects/WineExplorer-uwp/tests/uwp/u1_1/suspend.c cxprobe/suspend.c
tar -cf cxprobe.tar cxprobe/stage cxprobe/xamlshim cxprobe/xamlshim.reg cxprobe/suspend.c || exit 1
bash lab.sh push cxprobe.tar >/dev/null && bash lab.sh push cxprobe-run.sh >/dev/null || exit 1
# SUSPEND=1 in the environment of this script: the run minimizes and restores the CoreWindow (suspend and resume)
bash lab.sh ssh "cd /home/debian/uwp-u1-20261111/claude && (SUSPEND=${SUSPEND:-0} setsid nohup xvfb-run -a -s '-screen 0 1920x1200x24' bash cxprobe-run.sh $1 ${2:-} > logs/cx$1.log 2>&1 < /dev/null &); echo started"
for i in 1 2 3 4 5; do
  r=$(bash lab.sh ssh "f=/home/debian/uwp-u1-20261111/build/candidates/u11-2/cx-$1/summary; for i in \$(seq 50); do grep -qs '^done' \$f && break; sleep 1; done; grep -qs '^done' \$f && cat \$f || echo pending")
  [ "$r" != pending ] && break
done
echo "$r"
