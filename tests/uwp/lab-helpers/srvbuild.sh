#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: apply delta $1 and build the wineserver; log in logs/srvbuild.log
# usage: srvbuild.sh DELTA.tar
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2
log=$root/claude/logs/srvbuild.log
tar -xf "$root/claude/$1" -C $c/source
cd $c/build
make -k -j4 server/all > $root/claude/logs/srvbuild-full.log 2>&1
echo "rc=$?" > $log
grep -E "error|warning:" $root/claude/logs/srvbuild-full.log | sed "s|$c/source/||" | cut -c1-240 | sort -u | head -40 >> $log
echo srvbuild-done >> $log
