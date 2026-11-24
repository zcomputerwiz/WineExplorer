#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: IPackageManager6::ProvisionPackageForAllUsersAsync on the Wine build of candidate u12, in a fresh prefix.
#   1. a family that was never installed from a file: the operation fails, saying the package file is not known
#   2. a package installed from a file (StagePackageAsync, DevelopmentMode), then provisioned for all users: wexappx install
#      --all-users runs on that file (here without the elevation broker, so it says so; with the broker, the consent window)
# usage: pm-provision-run.sh PACKAGE_FILE_IN_GUEST FAMILY_NAME
# the workspace of the lab guest (override with UWP_WS)
UWP_WS=${UWP_WS:-/home/debian/uwp-u1-20261111}
root=${UWP_WS}
b=$root/build/candidates/u12/build
p=$root/build/candidates/u12/pm-prefix
c=$root/claude
x86_64-w64-mingw32-gcc -O2 -o $c/pm-probe.exe $c/pm-probe.c -lole32 || exit 1
export WINEPREFIX=$p WINESERVER=$b/server/wineserver WINEDEBUG=-all DISPLAY=:99
$WINESERVER -k 2>/dev/null; rm -rf $p
WINEDLLOVERRIDES=mscoree,mshtml= $b/wine wineboot -i > /dev/null 2>&1; $WINESERVER -w
$b/wine reg add 'HKCU\Software\WineExplorer\Shell' /v UwpCompatibility /t REG_DWORD /d 1 /f > /dev/null 2>&1; $WINESERVER -w
export WINEDEBUG=err+all,fixme+appx
echo "== unknown family"
timeout 120 $b/wine $c/pm-probe.exe provision "$2" 2>&1 | grep -v "libEGL\|gpsvc"
echo "== install, then provision"
timeout 120 $b/wine $c/pm-probe.exe install "file:///Z:$1" development 2>&1 | grep -v "libEGL\|gpsvc"
timeout 120 $b/wine $c/pm-probe.exe provision "$2" 2>&1 | grep -v "libEGL\|gpsvc"
$b/wine reg query 'HKCU\Software\WineExplorer\Appx\SourcePackages' 2>&1 | grep -v "libEGL\|gpsvc"
