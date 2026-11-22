#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: a new candidate (both architectures) built from a git archive of the Wine worktree (wine-src.tgz in claude/), so that
# whole files of the worktree can be copied into it later. The log is logs/newcand-NAME.log, the phase file the candidate's phase.
# usage: newcand-build.sh NAME
root=/home/debian/uwp-u1-20261111
name=$1
c=$root/build/candidates/$name
exec > $root/claude/logs/newcand-$name.log 2>&1
set -x
rm -rf "$c"
mkdir -p "$c/source" "$c/build"
echo EXTRACTING > "$c/phase"
tar -C "$c/source" -xzf $root/claude/wine-src.tgz
cd "$c/build"
echo CONFIGURING > "$c/phase"
"$c/source/configure" --enable-archs=i386,x86_64 > "$c/configure.log" 2>&1 || { echo FAILED-CONFIGURE > "$c/phase"; exit 1; }
echo BUILDING > "$c/phase"
make -j8 > "$c/build.log" 2>&1 || { echo FAILED-BUILD > "$c/phase"; tail -30 "$c/build.log"; exit 1; }
echo BUILT > "$c/phase"
echo 0 > "$c/exit"
echo newcand-done
