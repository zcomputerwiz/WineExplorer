#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Compile wexappx.exe for i386 against a built candidate, as a check only. usage: guest-build-wexappx32.sh CANDIDATE
set -euo pipefail
root=/home/debian/uwp-u1-20261111
c="$root/build/candidates/$1"
src="$root/claude/wexappx"
a=i386-windows
cd "$c/build"
i686-w64-mingw32-gcc -c -o "$src/wexappx32.o" "$src/wexappx.c" -I"$c/build/include" -I"$c/source/include" \
    -I"$c/source/include/msvcrt" -I"$c/source/libs/zlib" -D_UCRT -DFAR= -DZ_SOLO -D__WINE_PE_BUILD \
    -Wall -fno-strict-aliasing -Wdeclaration-after-statement -g -O2
tools/winegcc/winegcc -o "$src/wexappx32.exe" --wine-objdir . --cc-cmd="i686-w64-mingw32-gcc" -b i686-w64-mingw32 \
    -mconsole -municode "$src/wexappx32.o" libs/zlib/$a/libz.a dlls/xmllite/$a/libxmllite.a dlls/ole32/$a/libole32.a \
    dlls/advapi32/$a/libadvapi32.a dlls/kernelbase/$a/libkernelbase.a libs/uuid/$a/libuuid.a \
    libs/winecrt0/$a/libwinecrt0.a libs/compiler-rt/$a/libcompiler-rt.a dlls/ucrtbase/$a/libucrtbase.a \
    dlls/kernel32/$a/libkernel32.a dlls/ntdll/$a/libntdll.a
ls -la "$src/wexappx32.exe"
