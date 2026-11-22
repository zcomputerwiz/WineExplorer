#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: compile wexappx (wexappx.tar: wexappx/wexappx.c, as guest-build-wexappx.sh does, plus wintrust and crypt32) and
# install the signature fixtures with it in the prefix of sig-run.sh: without --development every package that is not
# signed by a trusted publisher is refused, with it the package goes in and the signature is described.
# usage: wexappx-sig.sh [32]      log: logs/wexappx-sig.log
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2
build=$c/build
run=$c/sig-run
log=$root/claude/logs/wexappx-sig$1.log
exec > $log 2>&1
rm -rf $root/claude/wexappx-sig; mkdir -p $root/claude/wexappx-sig
tar -xf $root/claude/wexappx.tar -C $root/claude/wexappx-sig
src=$root/claude/wexappx-sig/wexappx
if [ "$1" = 32 ]; then a=i386-windows; cc=i686-w64-mingw32-gcc; triple=i686-w64-mingw32; else a=x86_64-windows; cc=x86_64-w64-mingw32-gcc; triple=x86_64-w64-mingw32; fi
cd "$build"
$cc -c -o "$src/wexappx.o" "$src/wexappx.c" -I"$build/include" -I"$c/source/include" \
    -I"$c/source/include/msvcrt" -I"$c/source/libs/zlib" -D_UCRT -DFAR= -DZ_SOLO -D__WINE_PE_BUILD \
    -Wall -fno-strict-aliasing -Wdeclaration-after-statement -g -O2 2>&1 | grep -E "error|warning" | head
tools/winegcc/winegcc -o "$src/wexappx.exe" --wine-objdir . --cc-cmd="$cc" -b $triple \
    -mconsole -municode "$src/wexappx.o" libs/zlib/$a/libz.a dlls/xmllite/$a/libxmllite.a dlls/ole32/$a/libole32.a \
    dlls/advapi32/$a/libadvapi32.a dlls/kernelbase/$a/libkernelbase.a dlls/wintrust/$a/libwintrust.a \
    dlls/crypt32/$a/libcrypt32.a libs/uuid/$a/libuuid.a \
    libs/winecrt0/$a/libwinecrt0.a libs/compiler-rt/$a/libcompiler-rt.a dlls/ucrtbase/$a/libucrtbase.a \
    dlls/kernel32/$a/libkernel32.a dlls/ntdll/$a/libntdll.a 2>&1 | tail -3
ls -la "$src/wexappx.exe"
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver" WINEDEBUG=-all
cd "$run"
"$build/wine" reg add 'HKCU\Software\WineExplorer\Shell' /v UwpCompatibility /t REG_DWORD /d 1 /f > /dev/null 2>&1
"$build/server/wineserver" -w
exe="$src/wexappx.exe"
check() {   # check NAME ARGS...
  name=$1; shift
  out=$("$build/wine" "$exe" install "$@" 2>&1 | tr -d '\r')
  echo "-- $name"
  echo "$out" | grep -E "Installed|Signature|wexappx:" | cut -c1-220
  full=$(echo "$out" | sed -n 's/^Installed //p')
  [ -n "$full" ] && "$build/wine" "$exe" uninstall "$full" > /dev/null 2>&1
  "$build/server/wineserver" -w
}
for f in signed unsigned bad-block bad-blockmap other-publisher extra-member no-signature bad-p7x; do
  check "$f" "Z:$run/fix/$f.appx"
  check "$f --development" --development "Z:$run/fix/$f.appx"
done
# a trusted publisher: the test certificate in the prefix's Root store (the prefix only, never a Windows host)
"$build/wine" signature/trustcert-x64.exe signature/fixtures/test.cer
"$build/server/wineserver" -w
check "signed, certificate trusted" "Z:$run/fix/signed.appx"
check "other-publisher, certificate trusted" "Z:$run/fix/other-publisher.appx"
check "bad-block, certificate trusted" "Z:$run/fix/bad-block.appx"
"$build/wine" signature/sigtest-x64.exe fix/signed.appx 2>&1 | tr -d '\r'
echo wexappx-sig-done
