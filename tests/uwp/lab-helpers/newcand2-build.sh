#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: a new candidate (both architectures) from claude/wine-src.tgz (git archive of the merged Wine worktree), plus the modules that
# an assembled tree has and the archive does not (directories of dlls/ and programs/ with a Makefile.in that FROM_CAND has and the
# archive lacks), registered in configure.ac. Log: logs/newcand-NAME.log; phase file: the candidate's phase.
# usage: newcand2-build.sh NAME [FROM_CAND]     run detached: setsid nohup newcand2-build.sh u13 u12 &
root=/home/debian/uwp-u1-20261111
name=$1
from=$root/build/candidates/${2:-u12}
c=$root/build/candidates/$name
exec > $root/claude/logs/newcand-$name.log 2>&1
set -x
rm -rf "$c"
mkdir -p "$c/source" "$c/build"
echo EXTRACTING > "$c/phase"
tar -C "$c/source" -xzf $root/claude/wine-src.tgz
for kind in dlls programs; do
    for d in "$from/source/$kind"/*/; do
        n=$(basename "$d")
        if [ -f "$d/Makefile.in" ] && [ ! -d "$c/source/$kind/$n" ]; then
            cp -a "$d" "$c/source/$kind/$n"
            echo "assembled module $kind/$n"
            sed -i "0,/^WINE_CONFIG_MAKEFILE(/s||WINE_CONFIG_MAKEFILE($kind/$n)\nWINE_CONFIG_MAKEFILE(|" "$c/source/configure.ac"
        fi
    done
done
cd "$c/source" && autoconf -f
cd "$c/build"
echo CONFIGURING > "$c/phase"
"$c/source/configure" --enable-archs=i386,x86_64 > "$c/configure.log" 2>&1 || { echo FAILED-CONFIGURE > "$c/phase"; tail -20 "$c/configure.log"; exit 1; }
echo BUILDING > "$c/phase"
make -j8 > "$c/build.log" 2>&1 || { echo FAILED-BUILD > "$c/phase"; grep -E ' error |Error ' "$c/build.log" | head -20; exit 1; }
echo BUILT > "$c/phase"
echo 0 > "$c/exit"
echo newcand-done
