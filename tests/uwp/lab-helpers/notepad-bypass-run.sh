#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: notepad-run.sh with the LAB-ONLY fail-fast bypass (fastfail-bypass.py) in the candidate's ntdll, so that the input thread's
# fail-fast ends that thread only and Notepad goes on drawing; the original ntdll is put back and rebuilt at the end, and the
# script checks that no bypass line is left.
#   notepad-bypass-run.sh [WINEDEBUG] [LABEL]    log copied to claude/LABEL.log, screenshot to claude/LABEL.png
g=${UWP_WS:-/home/debian/uwp-u1-20261111}
C=$g/build/candidates/${CAND:-u13}
b=$C/build
r=$C/wasdk-run
label=${2:-bypass}
f=$C/source/dlls/ntdll/unix/thread.c
cp $f $g/claude/thread-before-bypass.c
python3 $g/claude/fastfail-bypass.py $f
cd $b && make -j6 dlls/ntdll/all 2>&1 | grep -E ': error|undefined reference' | head
export WINE_LAB_FASTFAIL_THREAD=1
bash $g/claude/notepad-run.sh riched20 "${1:-err+all,fixme+all,+loaddll}" > /dev/null 2>&1
unset WINE_LAB_FASTFAIL_THREAD
cp $g/claude/thread-before-bypass.c $f
cd $b && make -j6 dlls/ntdll/all 2>&1 | grep -E ': error' | head -3
echo "bypass lines left in the source: $(grep -c 'LAB-BYPASS' $f)"
echo "lines $(wc -l < $r/ovr.log); D3D devices created $(grep -a -c 'DXGID3D10CreateDevice' $r/ovr.log)"
grep -a -E 'LAB-BYPASS|RoFailFast|unimplemented function|private method [0-9]+ is not' $r/ovr.log | cut -c1-180 | head -6
cp $r/ovr.log $g/claude/$label.log; cp $r/ovr.png $g/claude/$label.png 2>/dev/null
ls -la $g/claude/$label.png 2>&1 | cut -c1-120
