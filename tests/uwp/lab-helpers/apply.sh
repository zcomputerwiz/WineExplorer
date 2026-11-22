#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Host side (Git Bash): copy the named files of the Wine worktree (paths relative to its root) into the candidate source on the
# guest, then make the targets there and print the log. Whole files only: do not use it for files the candidate has an older
# version of than the worktree (apply a patch for those).
# usage: apply.sh "FILE FILE..." "TARGET TARGET..."
set -e
wt=/c/Users/Owner/.codex/worktrees/uwp-runtime/wine
pad=/c/Users/Owner/AppData/Local/Temp/claude/f10dd599a76cdd8a/cc6dd05f-7e73-49f2-be6b-aa4119777252/scratchpad
here=$(dirname "$0")
rm -f "$pad/ap.tar"
(cd "$wt" && tar -cf "$pad/ap.tar" $1)
"$here/lab.sh" push ap.tar
"$here/lab.sh" ssh "cd /home/debian/uwp-u1-20261111/build/candidates/u11-2/source && tar -xf ../../../../claude/ap.tar && cd /home/debian/uwp-u1-20261111/claude && rm -f logs/mk.log && (setsid nohup bash mk.sh $2 > /dev/null 2>&1 &) ; echo started"
for i in $(seq 1 40); do
    sleep 6
    out=$("$here/lab.sh" ssh "cat /home/debian/uwp-u1-20261111/claude/logs/mk.log 2>/dev/null" || true)
    case "$out" in *mk-done*) echo "$out"; exit 0;; esac
done
echo "timeout"
