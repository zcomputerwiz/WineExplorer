#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# wex-run-split.sh [MACHINE_A [MACHINE_B]] - a full run of this worktree on two build machines at once
# (docs/TOOLING.md, "wex-run"): "normal" (normal suite, broker test) on MACHINE_A and "labt1" (T1, lab suite) on
# MACHINE_B, each with its own sync and build; about half of a "full" run on one machine. Defaults: wexgpo and
# wexgpo2 (docs/GPO_LAB.md, "The build machine"). Run on the Windows host (Git Bash) or inside WSL; it starts
# both through wsl.exe and prints both machines' stage lines, then one ALL PASS or FAILED line.
set -u
A=${1:-wexgpo}
B=${2:-wexgpo2}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
# the script's path as WSL sees it: /mnt/<drive>/... from Git Bash's /<drive>/..., unchanged inside WSL
case $HERE in
    /mnt/*) RUN=$HERE/wex-run.sh ;;
    /[a-zA-Z]/*) RUN=/mnt/$(printf '%s' "${HERE:1:1}" | tr 'A-Z' 'a-z')${HERE:2}/wex-run.sh ;;
    *) echo "cannot tell the WSL path of $HERE"; exit 2 ;;
esac
OUT=$(mktemp -d)
MSYS_NO_PATHCONV=1 wsl.exe -d "$A" -u root --exec bash "$RUN" normal > "$OUT/a" 2>&1 &
PA=$!
MSYS_NO_PATHCONV=1 wsl.exe -d "$B" -u root --exec bash "$RUN" labt1 > "$OUT/b" 2>&1 &
PB=$!
wait "$PA"; RA=$?
wait "$PB"; RB=$?
sed "s/^/$A: /" "$OUT/a"
sed "s/^/$B: /" "$OUT/b"
rm -rf "$OUT"
if [ "$RA" = 0 ] && [ "$RB" = 0 ]; then echo "ALL PASS full (split over $A and $B)"; exit 0; fi
echo "FAILED full (split over $A and $B)"
exit 1
