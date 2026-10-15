#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Full public-source P4 clone/build/install acceptance on the build VM.
set -euo pipefail
root=/srv/protonexplorer/wex-refactor-p4-fresh
mkdir -p "$root"
exec 9>"$root/build.lock"
flock -n 9
trap 'echo FRESH_BUILD_FAILED > "$root/build.status"' ERR
echo FRESH_CLONING > "$root/build.status"
if [ ! -d "$root/source" ]; then
    git clone --recurse-submodules "${PROJECT_SOURCE:-https://github.com/zcomputerwiz/WineExplorer.git}" "$root/source"
fi
git -C "$root/source" rev-parse HEAD > "$root/project.commit"
git -C "$root/source/wine" rev-parse HEAD > "$root/wine.commit"
test -z "$(git -C "$root/source" status --porcelain)"
test -z "$(git -C "$root/source/wine" status --porcelain)"
echo FRESH_ASSEMBLING > "$root/build.status"
bash "$root/source/scripts/assemble.sh"
mkdir -p "$root/source/build/wow64"
cd "$root/source/build/wow64"
CFLAGS='-O2 -pipe' CXXFLAGS='-O2 -pipe' "$root/source/wine/configure" --enable-archs=i386,x86_64 --prefix="$root/install"
echo FRESH_BUILD_RUNNING > "$root/build.status"
make -j8
echo FRESH_INSTALL_RUNNING > "$root/build.status"
make install
echo FRESH_BUILD_INSTALL_PASSED > "$root/build.status"
