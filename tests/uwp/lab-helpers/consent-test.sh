#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: consent.c with the test hook, run standalone on a package: what the window shows for "wexappx install --all-users"
root=/home/debian/uwp-u1-20261111; c=$root/build/candidates/u11-2; b=$c/build; run=$c/sig-run
out=$root/claude/consent-test; log=$root/claude/logs/consent-test.log
exec > $log 2>&1
rm -rf $out; mkdir -p $out /tmp/wexelev; tar -xf $root/claude/consent.tar -C $out
mkdir -p $out/shim/wine
printf '#define WINE_DEFAULT_DEBUG_CHANNEL(x)\n#define TRACE(...) do {} while (0)\n#define WARN(...) do {} while (0)\n#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))\n' > $out/shim/wine/debug.h
CS=$out/src/programs/consent
x86_64-w64-mingw32-windres -I$CS -o $out/consent_rc.o $CS/consent.rc
x86_64-w64-mingw32-gcc -O2 -Wall -municode -mwindows -DWEXCONSENT_TEST_HOOK -I$out/shim -I$CS -o $out/consent_test.exe $CS/consent.c $out/consent_rc.o -lcomctl32 -lversion -lwintrust -lcrypt32 -lmsi -luser32 -lgdi32 -ladvapi32 || exit 1
cp $b/programs/wexappx/x86_64-windows/wexappx.exe $out/wexappx.exe
export WINEPREFIX=$run/prefix WINESERVER=$b/server/wineserver WINEDLLOVERRIDES=mscoree,mshtml= WINEDEBUG=-all DISPLAY=:99
# the gate is off for this account (consent runs in an account that never set it): query has to work anyway
$b/wine reg delete 'HKCU\Software\WineExplorer\Shell' /v UwpCompatibility /f > /dev/null 2>&1
try() {
  # name, package
  rm -f /tmp/wexelev/consent.a1.*
  timeout 60 $b/wine $out/consent_test.exe /testid a1 /cmd Z:$out/wexappx.exe install --all-users Z:$run/fix/$2.appx > $out/$1.log 2>&1 &
  for i in $(seq 1 40); do [ -f /tmp/wexelev/consent.a1.shown ] && break; sleep 1; done
  echo "== $1"; grep -E "^(program|publisher)=" /tmp/wexelev/consent.a1.shown
  touch /tmp/wexelev/consent.a1.approve; wait
}
cd $run
try untrusted-signed signed
try unsigned unsigned
try bad-block bad-block
# the certificate trusted in this prefix: the signer is named
$b/wine signature/trustcert-x64.exe signature/fixtures/test.cer
try trusted-signed signed
echo consent-test-done
