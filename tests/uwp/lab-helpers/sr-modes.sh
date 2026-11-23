#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: Notepad with each StateRepository answer; count access violations and the windows seen
root=/home/debian/uwp-u1-20261111
R=$root/build/candidates/u12/wasdk-run
cd $root/claude
for mode in ${1:-0 1 5}; do
  rm -f $R/win.txt
  WEX_SR_MODE=$mode WD=err+all WAIT=25 CAND=u12 DISPLAY=:99 bash wasdk-win.sh > /dev/null 2>&1
  echo "== mode $mode: AV $(tr -d '\0' < $R/win-app.log | grep -ac 'ignoring exception c0000005'), program error dialogs $(grep -ac 'Program Error' $R/win.txt), other windows:"
  grep -a "SHOWN" $R/win.txt | grep -av "Program Error\|#32770\|\[Static\]\|\[Button\]" | cut -c1-170 | head -6
done
