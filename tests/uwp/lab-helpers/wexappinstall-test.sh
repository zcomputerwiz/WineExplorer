#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: build wexappx (with query) and wexappinstall (wexappinstall.tar: wexappx/wexappx.c, wexappinstall/wexappinstall.c),
# put both in the system directory of the sig-run prefix, print what "wexappx query" says about every fixture, and show the
# installer window under Xvfb: an untrusted package (screenshot), then the same package with its certificate trusted,
# installed through the window (WEXAPPINSTALL_TEST=install). usage: wexappinstall-test.sh   log: logs/wexappinstall-test.log
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/u11-2
build=$c/build
run=$c/sig-run
log=$root/claude/logs/wexappinstall-test.log
shots=$root/claude/shots
exec > $log 2>&1
mkdir -p $shots; rm -f $shots/*
rm -rf $root/claude/wai; mkdir -p $root/claude/wai
tar -xf $root/claude/wexappinstall.tar -C $root/claude/wai
a=x86_64-windows; cc=x86_64-w64-mingw32-gcc; triple=x86_64-w64-mingw32
cd "$build"
for prog in wexappx wexappinstall; do
  src=$root/claude/wai/$prog
  extra=""; [ $prog = wexappx ] && extra="-DZ_SOLO"
  $cc -c -o "$src/$prog.o" "$src/$prog.c" -I"$build/include" -I"$c/source/include" -I"$c/source/include/msvcrt" \
      -I"$c/source/libs/zlib" -D_UCRT -DFAR= $extra -D__WINE_PE_BUILD -Wall -fno-strict-aliasing -g -O2 2>&1 | grep -E "error|warning" | head
done
tools/winegcc/winegcc -o "$root/claude/wai/wexappx/wexappx.exe" --wine-objdir . --cc-cmd="$cc" -b $triple -mconsole -municode \
    "$root/claude/wai/wexappx/wexappx.o" libs/zlib/$a/libz.a dlls/xmllite/$a/libxmllite.a dlls/ole32/$a/libole32.a \
    dlls/advapi32/$a/libadvapi32.a dlls/kernelbase/$a/libkernelbase.a dlls/wintrust/$a/libwintrust.a dlls/crypt32/$a/libcrypt32.a \
    libs/uuid/$a/libuuid.a libs/winecrt0/$a/libwinecrt0.a libs/compiler-rt/$a/libcompiler-rt.a dlls/ucrtbase/$a/libucrtbase.a \
    dlls/kernel32/$a/libkernel32.a dlls/ntdll/$a/libntdll.a 2>&1 | tail -3
tools/winegcc/winegcc -o "$root/claude/wai/wexappinstall/wexappinstall.exe" --wine-objdir . --cc-cmd="$cc" -b $triple -mwindows -municode \
    "$root/claude/wai/wexappinstall/wexappinstall.o" dlls/user32/$a/libuser32.a dlls/gdi32/$a/libgdi32.a dlls/shell32/$a/libshell32.a \
    libs/winecrt0/$a/libwinecrt0.a libs/compiler-rt/$a/libcompiler-rt.a dlls/ucrtbase/$a/libucrtbase.a \
    dlls/kernel32/$a/libkernel32.a dlls/ntdll/$a/libntdll.a 2>&1 | tail -3
ls -la $root/claude/wai/*/*.exe

export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver" WINEDEBUG=-all DISPLAY=:99
cd "$run"
sys="$WINEPREFIX/drive_c/wexapp"; mkdir -p "$sys"
cp $root/claude/wai/wexappx/wexappx.exe $root/claude/wai/wexappinstall/wexappinstall.exe "$sys/"
"$build/wine" reg add 'HKCU\Software\WineExplorer\Shell' /v UwpCompatibility /t REG_DWORD /d 1 /f > /dev/null 2>&1
"$build/server/wineserver" -w

echo "=== query, certificate not trusted"
for f in signed unsigned bad-block other-publisher; do
  "$build/wine" "$sys/wexappx.exe" query "Z:$run/fix/$f.appx" 'C:\query.txt' 2>&1 | tr -d '\r'
  echo "-- $f: $(iconv -f UTF-16 -t UTF-8 "$WINEPREFIX/drive_c/query.txt" 2>/dev/null | tr '\r\n' '|' | cut -c1-420)"
done

shot() {   # shot NAME: the root window
  import -display :99 -window root "$shots/$1.png" 2>/dev/null
}
echo "=== window, certificate not trusted"
"$build/wine" "$sys/wexappinstall.exe" "Z:$run/fix/signed.appx" > /dev/null 2>&1 &
sleep 8; shot untrusted
WEXAPPINSTALL_TEST=cancel true
"$build/server/wineserver" -k; sleep 2

echo "=== window, unsigned"
"$build/wine" "$sys/wexappinstall.exe" "Z:$run/fix/unsigned.appx" > /dev/null 2>&1 &
sleep 8; shot unsigned
"$build/server/wineserver" -k; sleep 2

echo "=== certificate trusted"
"$build/wine" signature/trustcert-x64.exe signature/fixtures/test.cer
"$build/server/wineserver" -w
"$build/wine" "$sys/wexappx.exe" query "Z:$run/fix/signed.appx" 'C:\query.txt' 2>&1 | tr -d '\r'
echo "-- signed: $(iconv -f UTF-16 -t UTF-8 "$WINEPREFIX/drive_c/query.txt" 2>/dev/null | tr '\r\n' '|' | cut -c1-420)"
"$build/wine" "$sys/wexappinstall.exe" "Z:$run/fix/signed.appx" > /dev/null 2>&1 &
sleep 8; shot trusted
"$build/server/wineserver" -k; sleep 2

echo "=== installed through the window"
WEXAPPINSTALL_TEST=install "$build/wine" "$sys/wexappinstall.exe" "Z:$run/fix/signed.appx" > /dev/null 2>&1 &
sleep 6; shot installing
for i in 1 2 3 4 5 6 7 8; do sleep 2; pgrep -f wexappinstall > /dev/null || break; done
"$build/wine" "$sys/wexappx.exe" list 2>&1 | tr -d '\r'
for p in $("$build/wine" "$sys/wexappx.exe" list 2>&1 | tr -d '\r'); do "$build/wine" "$sys/wexappx.exe" uninstall "$p" 2>&1 | tr -d '\r'; done
echo wexappinstall-done
