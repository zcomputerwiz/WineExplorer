#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: WinVerifyTrust on the cabinet fixtures with the cabinet digest of wintrust, both architectures, against what Wine should
# say (expected-cab-wine.txt, the Windows answers). cabtest.tar holds signature/{fixtures-cab,tamper-cab.py,sigtest-x64.exe,
# sigtest-x86.exe,expected-cab-wine.txt}. usage: cab-run.sh      log: logs/cab-run.log
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2
build=$c/build
run=$c/cab-run
log=$root/claude/logs/cab-run.log
exec > $log 2>&1
rm -rf "$run"; mkdir -p "$run"; cd "$run"
tar -xf $root/claude/cabtest.tar
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver" WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml=
"$build/wine" wineboot -i > boot.log 2>&1; "$build/server/wineserver" -w
mkdir fix
cp signature/fixtures-cab/signed.cab signature/fixtures-cab/unsigned.cab fix/
python3 signature/tamper-cab.py fix/signed.cab fix/unsigned.cab fix
for arch in x64 x86; do
  echo "== $arch"
  "$build/wine" signature/sigtest-$arch.exe $(cd fix && ls *.cab | sort | sed 's|^|fix/|') 2>&1 | tr -d '\r' | sed 's|^fix/||' > out-$arch.txt
  diff <(tr -d '\r' < signature/expected-cab-wine.txt) out-$arch.txt && echo "as expected (expected-cab-wine.txt)"
done
echo cab-done
