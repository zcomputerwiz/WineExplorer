#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: rebuild the wineserver of u12 from the pushed completion.c/thread.c, run Notepad under the gdb watcher, summarise
c=/home/debian/uwp-u1-20261111/claude
C=/home/debian/uwp-u1-20261111/build/candidates/u12
R=$C/wasdk-run
cp $c/completion.c $c/thread.c $C/source/server/
cd $C/build && make -j4 server/wineserver 2>&1 | grep -i "error\|warning" | head
ls -la server/wineserver
rm -f $R/win.txt $R/ws-gdb.log
cd $c
GDB_WS=1 WD=err+all WAIT=30 CAND=u12 DISPLAY=:99 bash wasdk-win.sh > /dev/null 2>&1
sed -n '/SIGSEGV/,$p' $R/ws-gdb.log | head -6
grep -a "crashed" $R/win-app.log | head -3
grep -a "process \|SHOWN\|Notepad" $R/win.txt | head
tail -4 $R/win-app.log | cut -c1-200
