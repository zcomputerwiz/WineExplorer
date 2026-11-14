#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Fork the built merged candidate, apply a source delta, rebuild both architectures.
# usage: guest-u1r2-build.sh CANDIDATE_NAME DELTA_TAR [ORIGIN_NAME]
set -euo pipefail
root=/home/debian/uwp-u1-20261111
name=$1
delta=$2
origin="$root/build/candidates/${3:-merged-a524eaf3}"
candidate="$root/build/candidates/$name"
test "$(id -un)" = debian
test "$(cat "$root/owner")" = codex-uwp-u1-20261111
test "$(cat "$origin/phase")" = BUILT
test "$(cat "$origin/exit")" = 0
test -f "$delta"
if [ ! -d "$candidate" ]; then
    mkdir -- "$candidate"
    trap 'status=$?; printf "%s\n" "$status" > "$candidate/exit"; if ((status != 0)); then echo FAILED > "$candidate/phase"; fi' EXIT
    echo COPYING > "$candidate/phase"
    cp -a --reflink=auto -- "$origin/source" "$candidate/source"
    cp -a --reflink=auto -- "$origin/build" "$candidate/build"
    printf 'Copied from %s\n' "$origin" > "$candidate/origin"
    echo CONFIGURING > "$candidate/phase"
    first=1
else
    trap 'status=$?; printf "%s\n" "$status" > "$candidate/exit"; if ((status != 0)); then echo FAILED > "$candidate/phase"; fi' EXIT
    first=0
fi
rm -f "$candidate/exit"
echo APPLYING > "$candidate/phase"
sha256sum "$delta" >> "$candidate/deltas.log"
tar -xf "$delta" -C "$candidate/source"
if [ -f "$delta.deleted" ]; then
    while IFS= read -r path; do
        case "$path" in /*|*..*) exit 1;; esac
        rm -f -- "$candidate/source/$path"
    done < "$delta.deleted"
fi
cd "$candidate/build"
if [ "$first" = 1 ] || [ -n "${RECONFIGURE:-}" ]; then
    echo CONFIGURING > "$candidate/phase"
    "$candidate/source/configure" --enable-archs=i386,x86_64 > "$candidate/configure.log" 2>&1
fi
echo BUILDING > "$candidate/phase"
make -j"${JOBS:-6}" > "$candidate/build.log" 2>&1
echo BUILT > "$candidate/phase"
