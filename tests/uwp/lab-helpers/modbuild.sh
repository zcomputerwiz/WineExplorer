#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: apply delta $1, build the given dlls and their tests (both architectures); log in logs/modbuild.log
# usage: modbuild.sh DELTA.tar MODULE...
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2
log=$root/claude/logs/modbuild.log
delta=$1; shift
tar -xf "$root/claude/$delta" -C $c/source
cd $c/build
targets=""
for m in "$@"; do targets="$targets dlls/$m/all dlls/$m/tests/all"; done
make -k -j4 $targets > $root/claude/logs/modbuild-full.log 2>&1
echo "rc=$?" > $log
grep -E "error|warning:" $root/claude/logs/modbuild-full.log | sed "s|$c/source/||" | cut -c1-240 | sort -u | head -40 >> $log
echo modbuild-done >> $log
