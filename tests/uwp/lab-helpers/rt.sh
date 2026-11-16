#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: run the Wine tests of the given modules (x64, x86) in a fresh prefix; summary in logs/rt.log
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2/build
log=$root/claude/logs/rt.log
: > $log
export WINEPREFIX=$root/claude/rt-prefix WINESERVER=$c/server/wineserver WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml=
rm -rf $WINEPREFIX; cd $c; timeout 300 ./wine wineboot -i > /dev/null 2>&1
for m in "$@"; do
  for a in x86_64 i386; do
    exe=$(ls dlls/$m/tests/$a-windows/*_test.exe 2>/dev/null | head -1)
    [ -n "$exe" ] || continue
    out=$root/claude/logs/rt-$m-$a.txt
    timeout 300 ./wine $exe > $out 2>&1
    rc=$?
    if grep -q "^Valid test names" $out; then
      names=$(sed -n '/^Valid test names/,$p' $out | tail -n +2 | tr -d ' \r')
      : > $out; rc=0
      for t in $names; do timeout 300 ./wine $exe $t >> $out 2>&1 || rc=$?; done
    fi
    echo "$m $a rc=$rc $(grep -E 'tests executed' $out | sed -E 's/^[0-9a-f]+://' | tr '\n' ' ')" >> $log
    grep -E "Test failed|Test succeeded inside todo" $out | head -5 >> $log
  done
done
$WINESERVER -k
echo rt-done >> $log
