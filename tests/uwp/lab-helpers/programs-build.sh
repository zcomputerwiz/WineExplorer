#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: build the programs wexappx and wexappinstall through the Wine tree's own Makefiles (both architectures), as the
# assembled tree does, so that Wine's headers (not the host compiler's) judge the sources. programs.tar holds
# programs/wexappx and programs/wexappinstall (Makefile.in and the source).
# usage: programs-build.sh      log: logs/programs-build.log
root=/home/debian/uwp-u1-20261111
c=$root/build/candidates/${CAND:-u11-2}
log=$root/claude/logs/programs-build.log
exec > $log 2>&1
set -x
tar -xf $root/claude/programs.tar -C $c/source
cd $c/source
changed=0
for p in wexappx wexappinstall; do
  if ! grep -q "WINE_CONFIG_MAKEFILE(programs/$p)" configure.ac; then
    sed -i "s|^WINE_CONFIG_MAKEFILE(programs/msiexec)\$|WINE_CONFIG_MAKEFILE(programs/$p)\n&|" configure.ac
    changed=1
  fi
done
if [ $changed = 1 ]; then
  autoconf
  cd $c/build
  ./config.status --recheck
  ./config.status
fi
cd $c/build
make -j4 programs/wexappx/all programs/wexappinstall/all 2>&1 | grep -E "error|warning|\*\*\*" | head -30
echo "make rc=${PIPESTATUS[0]}"
ls -la programs/wexappx/*-windows/wexappx.exe programs/wexappinstall/*-windows/wexappinstall.exe
echo programs-done
