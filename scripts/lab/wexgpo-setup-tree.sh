#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# wexgpo-setup-tree.sh BRANCH WINE_BRANCH - the build tree that scripts/lab/wex-run.sh expects (/root/WE-prefetch:
# the project, its wine clone, a configured WoW64 build in build/wow64), on a fresh machine made by
# wexgpo-provision.sh (docs/GPO_LAB.md, "The build machine"). Shallow clones from GitHub; wex-run's sync then
# fetches the worktree's own commits. Then a first build (about 30 minutes on 16 cores).
set -euo pipefail
BRANCH=${1:?WineExplorer branch}
WINE_BRANCH=${2:?Wine branch}
P=/root/WE-prefetch

[ -d "$P/.git" ] || git clone -q --depth 1 -b "$BRANCH" https://github.com/zcomputerwiz/WineExplorer.git "$P"
[ -d "$P/wine/.git" ] || { rmdir "$P/wine" 2>/dev/null || true; git clone -q --depth 1 -b "$WINE_BRANCH" https://github.com/zcomputerwiz/wine.git "$P/wine"; }
find "$P/scripts" -name '*.py' -exec sed -i 's/\r$//' {} +
( cd "$P" && python3 scripts/assemble.py --wine-root wine )
( cd "$P/wine" && autoconf -f && perl tools/make_requests )
mkdir -p "$P/build/wow64"
( cd "$P/build/wow64" && ../../wine/configure --enable-archs=i386,x86_64 --prefix="$P/build/wow64/install" > /root/configure.log 2>&1 ) ||
    { tail -20 /root/configure.log; exit 1; }
make -C "$P/build/wow64" -j16 > /root/first-build.log 2>&1 || { grep -m10 'error' /root/first-build.log; exit 1; }
echo "TREE-DONE $(git -C "$P/wine" rev-parse --short HEAD)"
