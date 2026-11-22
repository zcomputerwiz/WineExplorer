#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: put the module src/dlls/appxsip (appxsip.tar: appxsip/Makefile.in, appxsip.c, appxsip.spec) into the candidate's
# Wine source, register it with configure (once), and build it for both architectures.
# usage: appxsip-build.sh        log: logs/appxsip-build.log
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2
log=$root/claude/logs/appxsip-build.log
exec > $log 2>&1
set -x
mkdir -p $c/source/dlls/appxsip
tar -xf $root/claude/appxsip.tar -C $c/source/dlls
cd $c/source
if ! grep -q 'WINE_CONFIG_MAKEFILE(dlls/appxsip)' configure.ac; then
  sed -i 's|^WINE_CONFIG_MAKEFILE(dlls/msisip)$|WINE_CONFIG_MAKEFILE(dlls/appxsip)\n&|' configure.ac
  grep -n 'dlls/appxsip' configure.ac
  autoconf
  cd $c/build
  ./config.status --recheck
  ./config.status
fi
cd $c/build
make -j4 dlls/appxsip/all
echo "make rc=$?"
ls -la dlls/appxsip/x86_64-windows/ dlls/appxsip/i386-windows/ 2>&1 | head
echo appxsip-done
