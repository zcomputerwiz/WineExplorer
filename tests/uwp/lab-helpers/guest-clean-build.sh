#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Clean both-architecture build of a source tree that is verified, file by file, against the blob hashes of a
# commit; then the packaged fixture and the regression run.
# usage: guest-clean-build.sh NAME SOURCE_CANDIDATE SHA_LIST
set -euo pipefail
root=/home/debian/uwp-u1-20261111
name=$1
origin="$root/build/candidates/$2/source"
list=$3
candidate="$root/build/candidates/$name"
test "$(cat "$root/owner")" = codex-uwp-u1-20261111
mkdir -- "$candidate"
trap 'status=$?; printf "%s\n" "$status" > "$candidate/exit"; if ((status != 0)); then echo FAILED > "$candidate/phase"; fi' EXIT
echo VERIFYING > "$candidate/phase"
cd "$origin"
# every file of the commit is present with the commit's content
bad=0
sha256sum -c --quiet "$list" > "$candidate/verify.log" 2>&1 || bad=1
# and nothing else is in the tree
find . -type f -o -type l | sed 's|^\./||' | sort > "$candidate/present.txt"
cut -c67- "$list" | sort > "$candidate/expected.txt"
comm -23 "$candidate/present.txt" "$candidate/expected.txt" > "$candidate/extra.txt"
if [ -s "$candidate/extra.txt" ]; then echo "EXTRA $(wc -l < "$candidate/extra.txt") files" >> "$candidate/verify.log"; fi
test "$bad" = 0
echo COPYING > "$candidate/phase"
mkdir -- "$candidate/source" "$candidate/build"
tar -C "$origin" -cf - -T "$candidate/expected.txt" | tar -C "$candidate/source" -xf -
cd "$candidate/build"
echo CONFIGURING > "$candidate/phase"
"$candidate/source/configure" --enable-archs=i386,x86_64 > "$candidate/configure.log" 2>&1
echo BUILDING > "$candidate/phase"
make -j8 > "$candidate/build.log" 2>&1
echo BUILT > "$candidate/phase"
echo 0 > "$candidate/exit"
trap - EXIT
bash "$root/claude/guest-u1-run.sh" "$name" u1-run-1 > "$candidate/u1-run.out" 2>&1 || true
JOBS=8 bash "$root/claude/guest-regress.sh" "$name" regress-1 > "$candidate/regress.out" 2>&1 || true
echo DONE > "$candidate/done"
