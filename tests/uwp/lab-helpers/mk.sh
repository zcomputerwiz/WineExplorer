#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: make the given targets in the candidate build tree (the sources are already in place); log in logs/mk.log
# usage: mk.sh TARGET...      example: mk.sh include/all dlls/windows.applicationmodel/all
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2
cd $c/build
make -k -j4 "$@" > $root/claude/logs/mk-full.log 2>&1
echo "rc=$?" > $root/claude/logs/mk.log
grep -E "error|warning:|No rule" $root/claude/logs/mk-full.log | sed "s|$c/source/||" | cut -c1-260 | sort -u | head -60 >> $root/claude/logs/mk.log
echo mk-done >> $root/claude/logs/mk.log
