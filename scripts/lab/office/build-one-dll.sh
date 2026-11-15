#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Build one single-source 32-bit builtin DLL against an existing WoW64 build tree, without configure
# (to try a new module while the tree is busy or not yet reconfigured).  Guest side.
#   build-one-dll.sh SOURCE_DIR NAME OUT_DIR [IMPORT...]      BUILD=~/build/wow64  SRC=~/src/wine
set -euo pipefail
dir=$(realpath "$1") name=$2 out=$(realpath "$3")
shift 3
build=${BUILD:-$HOME/build/wow64} src=${SRC:-$HOME/src/wine}
cd "$build"
source=$dir/$name.c
[ -f "$source" ] || source=$dir/main.c
i686-w64-mingw32-gcc -c -o "$out/$name.o" "$source" -I"$dir" -D_SLC_ -Iinclude -I"$src/include" \
    -I"$src/include/msvcrt" -D_UCRT -D__WINESRC__ -D__WINE_PE_BUILD -Wall -fno-strict-aliasing \
    -Wdeclaration-after-statement -Wstrict-prototypes -fno-omit-frame-pointer \
    -mpreferred-stack-boundary=2 -mlong-double-64 -msse2 -gdwarf-4 -g -O2
# import libraries of the installed Wine: the tree's own may not be built yet
libdir=${LIBDIR:-/usr/local/lib/wine/i386-windows}
libs=()
for import in winecrt0 "$@" ucrtbase kernel32 ntdll; do libs+=("$libdir/lib$import.a"); done
tools/winegcc/winegcc -o "$out/$name.dll" --wine-objdir . -b i686-w64-mingw32 -Wl,--wine-builtin -shared \
    "$dir/$name.spec" "$out/$name.o" "${libs[@]}" -Wl,--disable-stdcall-fixup
ls -la "$out/$name.dll"
