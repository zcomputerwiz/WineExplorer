#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: WinVerifyTrust on the signature fixtures with the Appx SIP built by appxsip-build.sh, both architectures, against what
# Wine should say (expected-wine.txt; the two differences from Windows are in tests/uwp/signature/README.md). sigtest.tar holds signature/{fixtures,tamper.py,sigtest-x64.exe,sigtest-x86.exe,
# expected-wine.txt}. usage: sig-run.sh      log: logs/sig-run.log
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2
build=$c/build
run=$c/sig-run
log=$root/claude/logs/sig-run.log
exec > $log 2>&1
rm -rf "$run"; mkdir -p "$run"; cd "$run"
tar -xf $root/claude/sigtest.tar
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver" WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml=
"$build/wine" wineboot -i > boot.log 2>&1; "$build/server/wineserver" -w
echo "regsvr32: $("$build/wine" regsvr32 /s appxsip.dll > reg.log 2>&1; echo $?)"
"$build/server/wineserver" -w
echo "regsvr32 (32-bit): $("$build/wine" 'C:\windows\syswow64\regsvr32.exe' /s appxsip.dll >> reg.log 2>&1; echo $?)"
"$build/server/wineserver" -w
mkdir fix
cp signature/fixtures/signed.appx signature/fixtures/unsigned.appx fix/
python3 signature/tamper.py fix/signed.appx fix signature/fixtures/signed-tiny.exe
for arch in x64 x86; do
  echo "== $arch"
  "$build/wine" signature/sigtest-$arch.exe $(cd fix && ls *.appx | sort | sed 's|^|fix/|') 2>&1 | tr -d '\r' > out-$arch.txt
  diff <(tr -d '\r' < signature/expected-wine.txt) out-$arch.txt && echo "as expected (expected-wine.txt)"
done
echo sig-done
