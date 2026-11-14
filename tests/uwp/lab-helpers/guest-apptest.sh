#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# the windows.storage.applicationdata conformance test on both architectures in a fresh prefix
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2; b=$c/build
export WINEPREFIX=$c/apptest-prefix WINESERVER=$b/server/wineserver WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml=
rm -rf $WINEPREFIX; mkdir $WINEPREFIX
cd $b && make -j2 dlls/windows.storage.applicationdata/tests/all > $root/claude/logs/apptest-build.log 2>&1
timeout 120 $b/wine wineboot -i > /dev/null 2>&1
for a in x86_64 i386; do
  exe=$(ls $b/dlls/windows.storage.applicationdata/tests/$a-windows/*.exe | head -1)
  echo "== $a $(basename $exe)"
  timeout 120 $b/wine $exe 2>&1 | tail -6
done
timeout 30 $WINESERVER -k; true
