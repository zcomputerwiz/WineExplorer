#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the answers to method 21 of the StateRepository stand-in (WEX_SR_MODE 0 to 5), one launch of the application of the prefix
# wasdk-run each (wasdk-win.sh); what each launch did is collected in sr-experiments.txt of the prefix directory.
# usage: sr-experiments.sh "0 1 2 3 4 5"      (env: CAND)
root=/home/debian/uwp-u1-20261111
run=$root/build/candidates/${CAND:-u12}/wasdk-run
out=$run/sr-experiments.txt
: > $out
for mode in ${1:-0 1 2 3 4 5}; do
  rm -f $run/win.txt
  WEX_SR_MODE=$mode WD=err+all,fixme+model WAIT=30 CAND=${CAND:-u12} DISPLAY=:99 bash $root/claude/wasdk-win.sh > /dev/null 2>&1
  {
    echo "################ mode $mode"
    echo "-- methods called:"
    grep -a "slot_log" $run/win-app.log | tr -d '\0' | sed 's/.*slot_log //; s/ called, iface.*//' | sort | uniq -c
    echo "-- err lines:"
    grep -a "err:" $run/win-app.log | sed 's/^[0-9a-f]*://' | sort | uniq -c | sort -rn | head -6 | cut -c1-200
    echo "-- processes and windows:"
    grep -a "process \|Notepad\|SHOWN\|hidden" $run/win.txt | cut -c1-160 | head -8
    echo "-- last lines of the log:"
    tail -3 $run/win-app.log | tr -d '\0' | cut -c1-200
  } >> $out
done
echo experiments-done >> $out
