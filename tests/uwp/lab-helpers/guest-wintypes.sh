#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# wintypes and windows.storage.applicationdata conformance tests on both architectures in a fresh prefix.
# usage: guest-wintypes.sh CANDIDATE RUN_NAME
set -u
root=/home/debian/uwp-u1-20261111
candidate="$root/build/candidates/$1"
run="$candidate/$2"
build="$candidate/build"
test "$(cat "$root/owner")" = codex-uwp-u1-20261111 || exit 1
mkdir -- "$run" || exit 1
make -C "$build" -j4 dlls/wintypes/tests/all dlls/windows.storage.applicationdata/tests/all > "$run/build.log" 2>&1
echo "build rc=$?" > "$run/summary"
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver"
export WINEDEBUG=-all WINEDLLOVERRIDES=mscoree,mshtml= WINETEST_INTERACTIVE=0 WINETEST_COLOR=0
mkdir -- "$WINEPREFIX"
cd "$run"
timeout --kill-after=10s 180s "$build/wine" wineboot -i > boot.log 2>&1
for architecture in x86_64 i386; do
  for spec in wintypes:wintypes windows.storage.applicationdata:data; do
    dll=${spec%%:*}; unit=${spec#*:}
    exe="$build/dlls/$dll/tests/$architecture-windows/${dll}_test.exe"
    timeout --kill-after=10s 300s "$build/wine" "$exe" "$unit" > "$architecture-$dll.log" 2>&1
    printf '%s %s exit=%s %s\n' "$architecture" "$dll" "$?" "$(tail -n 1 "$architecture-$dll.log" | tr -d '\r')" >> summary
    grep -E 'Test failed|Test succeeded inside todo' "$architecture-$dll.log" | head -40 >> summary
  done
done
timeout 30s "$WINESERVER" -k > /dev/null 2>&1
echo done >> summary
