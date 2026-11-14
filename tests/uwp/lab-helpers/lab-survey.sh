#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Read-only survey of the owned UWP guest workspace.
W=/home/debian/uwp-u1-20261111
cd "$W" || exit 1
C="$W/build/candidates/merged-a524eaf3"
for f in phase exit; do echo "-- $f: $(cat "$C/$f" 2>/dev/null)"; done
echo "-- build.log tail"; tail -3 "$C/build.log" | cut -c1-200
echo "-- build errors"; grep -n -E "error:|Error [0-9]|\*\*\*" "$C/build.log" | head -20
echo "== running"; pgrep -a -f "make -j" | head
echo "== scripts"
for f in guest-merged-build.sh guest-fork-candidate.sh guest-r4-clean-tests.sh guest-rpc-tests.sh; do echo "---- $f"; cat "$W/$f"; done
df -h / "$W/build" | cat
