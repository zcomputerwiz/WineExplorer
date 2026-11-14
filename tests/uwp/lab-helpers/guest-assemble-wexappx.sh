#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Build wexappx the way an assembled tree does: module directory in programs/, registered in configure.ac,
# configure regenerated, built by its own Makefile.in for both architectures. usage: guest-assemble-wexappx.sh CANDIDATE
set -euo pipefail
root=/home/debian/uwp-u1-20261111
c="$root/build/candidates/$1"
log="$root/claude/logs/assemble-wexappx"
echo RUNNING > "$log.phase"
trap 'echo "exit $?" >> "$log.phase"' EXIT
if ! command -v autoconf > /dev/null; then
    sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends autoconf > "$log.apt" 2>&1
fi
mkdir -p "$c/source/programs/wexappx"
cp "$root/claude/wexappx/wexappx.c" "$root/claude/wexappx/Makefile.in" "$c/source/programs/wexappx/"
cd "$c/source"
if ! grep -q 'WINE_CONFIG_MAKEFILE(programs/wexappx)' configure.ac; then
    sed -i 's|^WINE_CONFIG_MAKEFILE(programs/wevtutil)$|&\nWINE_CONFIG_MAKEFILE(programs/wexappx)|' configure.ac
    grep -q 'WINE_CONFIG_MAKEFILE(programs/wexappx)' configure.ac
fi
autoconf -f > "$log.autoconf" 2>&1
cd "$c/build"
"$c/source/configure" --enable-archs=i386,x86_64 > "$log.configure" 2>&1
make -j8 programs/wexappx/all > "$log.make" 2>&1
ls -la programs/wexappx/*/wexappx.exe >> "$log.phase"
cp programs/wexappx/x86_64-windows/wexappx.exe "$root/claude/fixture/wexappx.exe"
echo BUILT >> "$log.phase"
