#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# apply delta $1 and build only the storage DLLs for a quick compile check; the diagnostics go to logs/fast15.log
set -u
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2
tar -xf "$root/claude/$1" -C $c/source
cd $c/build
make -k -j4 dlls/windows.storage/all dlls/wintypes/all > $root/claude/logs/fast15-full.log 2>&1
echo "rc=$?" > $root/claude/logs/fast15.log
grep -E 'error|warning' $root/claude/logs/fast15-full.log | sed 's|/home/debian/uwp-u1-20261111/build/candidates/u11-2/source/||' | cut -c1-300 | head -80 >> $root/claude/logs/fast15.log
echo make-done >> $root/claude/logs/fast15.log
