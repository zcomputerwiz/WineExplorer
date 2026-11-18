#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# wex-batch.sh REV NAME [STAGE...] - the checks before a merge, side by side (docs/TOOLING.md, "Batches"):
#
#   - the full suite of this worktree (scripts/lab/wex-run.sh full), which must be at REV, and meanwhile
#   - a test ISO from REV (installer/tests/build-test-iso.sh, in a WSL-native clone of the shared object store),
#     the probes, and the VM chain on it (installer/tests/run-chain.sh: install, then the STAGEs, default all).
#
# Both start at once; their two Wine compiles take turns through scripts/lab/wex-make.sh (the second is mostly ccache
# hits). The suite takes about 50 minutes, the ISO and the chain about as long with 3 VMs at once, so a batch takes
# about an hour instead of the two of running them one after the other.
#
# Run as root in wexbuild: wsl.exe -d wexbuild -u root --exec bash /mnt/d/.../<worktree>/scripts/lab/wex-batch.sh
# REV NAME [STAGE...]. REV is a full commit id. Environment: WEX_BATCH_LANES (VMs at once, default 3),
# WEX_BATCH_FWS (default "bios uefi"); WEX_SESSION0 / WEX_ELEVATION pass through to the packaging (default on).
# Output: one line per part and its result; logs in /var/tmp/wex-batch/NAME-{suite,iso,chain}.log. The exit status is
# 0 when both parts passed.
set -u
REV=${1:?usage: wex-batch.sh REV NAME [STAGE...]}
NAME=${2:?usage: wex-batch.sh REV NAME [STAGE...]}
shift 2
[[ "$REV" =~ ^[0-9a-f]{40}$ ]] || { echo "REV must be a full commit id"; exit 2; }
[[ "$NAME" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "invalid NAME"; exit 2; }
D=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
G=(git -c safe.directory='*')
LOGS=/var/tmp/wex-batch
mkdir -p "$LOGS"
T0=$SECONDS
part() { echo "$(date +%T) $*"; }

# The suite runs the worktree as it is: it must be REV, with nothing changed. A git worktree's .git file names a
# Windows path (D:/...): translated for WSL, as wex-run.sh does.
if [ -f "$D/.git" ]; then
    GD=$(sed -n 's/^gitdir: //p' "$D/.git" | tr -d '\r' | sed 's#^\([A-Za-z]\):#/mnt/\L\1#')
    WG=("${G[@]}" --git-dir="$GD" --work-tree="$D")
else
    WG=("${G[@]}" -C "$D")
fi
head=$("${WG[@]}" rev-parse HEAD 2>/dev/null)
[ "$head" = "$REV" ] || { echo "the worktree is at ${head:-?}, not at $REV"; exit 2; }
[ -z "$("${WG[@]}" status --porcelain --untracked-files=no --ignore-submodules=dirty 2>/dev/null)" ] ||
    { echo "the worktree has changes; commit them (the suite runs the worktree, the ISO REV)"; exit 2; }

part "suite: wex-run.sh full"
( bash "$D/scripts/lab/wex-run.sh" full > "$LOGS/$NAME-suite.log" 2>&1 ) &
suite_pid=$!

image() {
    local SRC=/root/wex-isosrc SHARED=/mnt/d/DeepThought/Projects/WineExplorer/.git REPO ISO a cc
    [ -d "$SRC/.git" ] || "${G[@]}" clone -q --no-checkout "$SHARED" "$SRC" || return 1
    "${G[@]}" -C "$SRC" fetch -q "$SHARED" "$REV" && "${G[@]}" -C "$SRC" checkout -q -f "$REV" || return 1
    "${G[@]}" -C "$SRC" config submodule.wine.url "$SHARED/modules/wine"
    "${G[@]}" -C "$SRC" config submodule.wexcomp.url "$SHARED/modules/wexcomp"
    "${G[@]}" -C "$SRC" config submodule.vintner.url /root/WineExplorer/vintner   # the build clone has vintner
    "${G[@]}" -C "$SRC" -c protocol.file.allow=always submodule update -q --init wine wexcomp vintner || return 1
    part "image: ISO of $("${G[@]}" -C "$SRC" log --oneline -1 | cut -c1-70)"
    bash "$SRC/installer/tests/build-test-iso.sh" "$REV" "$NAME" > "$LOGS/$NAME-iso.log" 2>&1 || return 1
    ISO=/mnt/d/WSL/wex-iso/$NAME.iso
    [ -f "$ISO" ] || return 1
    REPO=$SRC
    mkdir -p /var/tmp/wex-probes
    for a in 32 64; do
        cc=x86_64-w64-mingw32-gcc; [ $a = 32 ] && cc=i686-w64-mingw32-gcc
        $cc -O2 -o "/var/tmp/wex-probes/safeboot-probe$a.exe" "$REPO/installer/tests/safeboot-probe.c" -ladvapi32 -luser32 &&
            $cc -O2 -o "/var/tmp/wex-probes/display-probe$a.exe" "$REPO/installer/tests/display-probe.c" -luser32 -lgdi32 ||
            return 1
    done
    part "image: chain on ${WEX_BATCH_FWS:-bios uefi}, ${WEX_BATCH_LANES:-3} at once: ${*:-all stages}"
    VMTEST_DIR=/var/tmp/vmtest-gpt WEX_TEST_PROBES=/var/tmp/wex-probes bash "$REPO/installer/tests/run-chain.sh" \
        -j "${WEX_BATCH_LANES:-3}" --fws "${WEX_BATCH_FWS:-bios uefi}" --iso "$ISO" "$NAME" "$@" \
        > "$LOGS/$NAME-chain.log" 2>&1
}
image "$@" &
image_pid=$!

wait "$suite_pid"; suite=$?
part "suite: $(grep -E '^(ALL PASS|ALL FAILED)' "$LOGS/$NAME-suite.log" | tail -1 || echo "status $suite")"
wait "$image_pid"; chain=$?
summary=/var/tmp/wex-chain/$NAME/summary.txt
if [ -f "$summary" ]; then
    grep -E 'PASS|FAIL' "$summary" | sed 's/^/  /'
else
    tail -n 5 "$LOGS/$NAME-iso.log" "$LOGS/$NAME-chain.log" 2>/dev/null | sed 's/^/  /'
fi
part "image: $([ "$chain" = 0 ] && echo PASS || echo "FAILED ($chain)")"
if [ "$suite" = 0 ] && [ "$chain" = 0 ]; then
    echo "ALL PASS batch $NAME ($(( (SECONDS - T0 + 30) / 60 )) min)"
    exit 0
fi
echo "ALL FAILED batch $NAME ($(( (SECONDS - T0 + 30) / 60 )) min)"
exit 1
