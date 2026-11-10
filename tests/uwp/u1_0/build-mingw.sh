#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Cross-compile the U1.0 probe and in-process component with the MinGW
# compilers for both x64 and x86, so the same bits can run on Wine.
#
# Flags: -O2 -Wall -municode, with static libstdc++/libgcc so no MinGW runtime
# DLL (libstdc++-6.dll / libgcc_s_dw2-1.dll) is needed. INITGUID is defined so
# the WinRT IIDs (IID_IInspectable, IID_IActivationFactory) are defined here
# rather than expected from a library MinGW does not ship; the MSVC build
# (build.ps1) links uuid.lib and is unaffected.
#
# Output goes to build/mingw-x64 and build/mingw-x86 (gitignored like build/).
set -euo pipefail

cd "$(dirname "$0")"

CXXFLAGS=(-O2 -Wall -municode -DINITGUID -static-libstdc++ -static-libgcc)
LIBS=(-lruntimeobject -lwindowsapp -lole32 -ladvapi32 -luuid -lkernel32)

build_arch() {
    arch="$1"
    prefix="$2"
    out="build/mingw-$arch"
    mkdir -p "$out"
    echo "=== $arch ($prefix) -> $out ==="
    "$prefix-g++" "${CXXFLAGS[@]}" -shared -o "$out/component.dll" component.cpp -Wl,component.def "${LIBS[@]}"
    "$prefix-g++" "${CXXFLAGS[@]}" -o "$out/probe.exe" probe.cpp "${LIBS[@]}"
    ls -l "$out"
}

build_arch x64 x86_64-w64-mingw32
build_arch x86 i686-w64-mingw32
echo "BUILD_MINGW_DONE"
