#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: map the LAB code frames of the last C++ exception of the Notepad run to modules (needs +seh,+loaddll in win-app.log)
R=/home/debian/uwp-u1-20261111/build/candidates/u12/wasdk-run
tr -d '\0' < $R/win-app.log > $R/w.log
n=$(grep -a -n "LAB cxx throw info" $R/w.log | tail -1 | cut -d: -f1)
echo "last throw at line $n; total throws $(grep -ac 'LAB cxx throw info' $R/w.log)"
sed -n "${n}p" $R/w.log | cut -c1-200
sed -n "$((n+1)),$((n+300))p" $R/w.log | grep -a "LAB code" | head -40 | sed 's/.*LAB code//' > /tmp/frames.txt
cat /tmp/frames.txt | tr '\n' ' ' | cut -c1-1500; echo
for b in $(sed 's/.*\] \(.*\)+.*/\1/' /tmp/frames.txt | sort -u) $(sed -n "${n}p" $R/w.log | sed 's/.*module \([0-9a-f]*\)).*/\1/'); do
  grep -a "loaddll.*at 0*$b\b" $R/w.log | head -1 | sed 's/.*Loaded L.\(.*\). at \(.*\):.*/\2 \1/' | cut -c1-150
done | sort -u
