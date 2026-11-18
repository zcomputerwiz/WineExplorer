#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# A test ISO of one commit, built in the Wine build distro (wexbuild; docs/TOOLING.md), as root:
#
#   build-test-iso.sh REV NAME            ->  /mnt/d/WSL/wex-iso/NAME.iso (WEX_ISO_OUT to change the folder)
#
# REV is a commit of the checkout that holds this script (a branch, a tag or a hash; it need not be checked out), so
# the working tree's edits and its own `wine` checkout are never used:
#   1. git archive of REV, synced into the build clone /root/WineExplorer (WEX_BUILD_CLONE);
#   2. the clone's wine at REV's gitlink (fetched from the checkout's .git/modules/wine), assembled
#      (scripts/assemble.sh), configured with the build's own options and built (WoW64, build/wow64), unless the
#      last build was of the same Wine commit and the same assembled sources (src/, scripts/assemble.*): then the
#      build is kept as it is, which saves about 17 minutes (WEX_FORCE_WINE_BUILD=1 builds anyway);
#   3. wexcomp and vintner at REV's gitlinks; installer/build-iso.sh --clean.
# The clone must have been set up once (build/wow64 configured: docs/TOOLING.md, wexbuild). The last line is
# "ISO DONE" when the ISO is in place.
set -uo pipefail
export DEBIAN_FRONTEND=noninteractive
REV=${1:?usage: build-test-iso.sh REV NAME}
NAME=${2:?usage: build-test-iso.sh REV NAME}
[[ "$NAME" =~ ^[a-zA-Z0-9_-]+$ ]] || { echo "invalid NAME"; exit 2; }
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
DST=${WEX_BUILD_CLONE:-/root/WineExplorer}
OUTDIR=${WEX_ISO_OUT:-/mnt/d/WSL/wex-iso}
EXPORT=/root/wex-test-iso-src
STAMP=$DST/build/wow64/.wex-test-iso-stamp
G=(git -c safe.directory='*')
W=(git -c safe.directory='*' -C "$DST/wine")

[ -d "$DST/build/wow64" ] || { echo "no $DST/build/wow64: set up the build clone first (docs/TOOLING.md)"; exit 1; }
rev_of() { "${G[@]}" -C "$REPO" rev-parse "$REV:$1"; }
WREV=$(rev_of wine) && CREV=$(rev_of wexcomp) && VREV=$(rev_of vintner) || { echo "cannot read the gitlinks"; exit 1; }
# what the Wine build depends on besides Wine itself: the sources the assembly links in
INPUTS="$WREV $(rev_of src) $(rev_of scripts/assemble.py) $(rev_of scripts/assemble.sh)" || exit 1
echo "=== $(date +%T) $("${G[@]}" -C "$REPO" log --oneline -1 "$REV"): wine $WREV, wexcomp $CREV, vintner $VREV"

echo "=== $(date +%T) export and sync"
rm -rf "$EXPORT" && mkdir -p "$EXPORT"
"${G[@]}" -C "$REPO" archive "$REV" | tar -x -C "$EXPORT" || exit 1
rsync -rlpgoD --checksum --delete --exclude=/.git --exclude=/wine --exclude=/build --exclude=/wexcomp --exclude=/vintner \
    --exclude=/references --exclude=/archives \
    --exclude=/installer/live-build/config/packages.chroot --exclude='*.iso' --exclude='*.vdi' "$EXPORT/" "$DST/" || exit 1

echo "=== $(date +%T) wine"
if [ "${WEX_FORCE_WINE_BUILD:-0}" != 1 ] && [ -f "$STAMP" ] && [ "$(cat "$STAMP")" = "$INPUTS" ] \
        && [ "$("${W[@]}" rev-parse HEAD)" = "$WREV" ]; then
    echo "wine: the build of $WREV with these sources is current; not rebuilt"
else
    rm -f "$STAMP"
    "${W[@]}" fetch -q "$REPO/.git/modules/wine" refs/remotes/origin/development || exit 1
    if ! "${W[@]}" cat-file -e "$WREV^{commit}"; then
        "${W[@]}" fetch -q "$REPO/.git/modules/wine" "$WREV" 2>/dev/null
        "${W[@]}" cat-file -e "$WREV^{commit}" || { echo "the gitlink $WREV is not in the checkout's wine repository"; exit 1; }
    fi
    "${W[@]}" checkout -q -- . && "${W[@]}" checkout -q --detach "$WREV" || { echo "cannot check out $WREV"; exit 1; }
    echo "wine at $("${W[@]}" log --oneline -1)"
    # the reset put back the stock modules that the assembly replaces; its saved copies of them (from the previous
    # Wine) are stale, and assemble.py refuses to overwrite them
    rm -rf "$DST/build/assembly-stock"
    bash "$DST/scripts/assemble.sh" "$DST/wine" > /root/assemble-test-iso.log 2>&1 \
        || { tail -n 30 /root/assemble-test-iso.log; exit 1; }
    cd "$DST/build/wow64" || exit 1
    { ./config.status --recheck && ./config.status; } > /root/configure-test-iso.log 2>&1 \
        || { tail -n 30 /root/configure-test-iso.log; exit 1; }
    start=$(date +%s)
    bash "$REPO/scripts/lab/wex-make.sh" -j"$(nproc)" > /root/make-test-iso.log 2>&1 \
        || { grep -E 'error:|Error [0-9]' /root/make-test-iso.log | head -n 30; exit 1; }
    echo "wine built in $(( ($(date +%s) - start) / 60 )) min"
    echo "$INPUTS" > "$STAMP"
fi

echo "=== $(date +%T) wexcomp and vintner"
for sub in wexcomp:$CREV vintner:$VREV; do
    name=${sub%%:*} rev=${sub#*:}
    if [ ! -d "$DST/$name/.git" ] && [ ! -f "$DST/$name/.git" ]; then
        rm -rf "${DST:?}/$name"
        "${G[@]}" clone -q "$REPO/$name" "$DST/$name" || exit 1
    fi
    "${G[@]}" -C "$DST/$name" fetch -q "$REPO/.git/modules/$name" "$rev" 2>/dev/null \
        || "${G[@]}" -C "$DST/$name" fetch -q "$REPO/$name" "$rev" 2>/dev/null || true
    "${G[@]}" -C "$DST/$name" checkout -q --detach "$rev" || { echo "cannot check out $name $rev"; exit 1; }
    [ -f "$DST/$name/.gitmodules" ] && "${G[@]}" -C "$DST/$name" submodule update -q --init --recursive
    echo "$name at $("${G[@]}" -C "$DST/$name" log --oneline -1)"
done

if [ "${WEX_STAGE_ONLY:-0}" = 1 ]; then    # quick-stage.sh: the sources are in place and Wine is built; stop here
    echo "STAGE READY"
    exit 0
fi

echo "=== $(date +%T) build-iso.sh --clean"
mkdir -p /root/out "$OUTDIR"
start=$(date +%s)
cd "$DST" || exit 1
WEX_LB_CACHE=/root/lb-cache WEX_FAST_SQUASH=${WEX_FAST_SQUASH:-1} WEX_BUILD_TIMES=1 \
    WORK_DIR=/root/iso-build OUT_ISO=/root/out/$NAME.iso bash installer/build-iso.sh --clean > "/root/iso-build-$NAME.log" 2>&1 \
    || { tail -n 60 "/root/iso-build-$NAME.log"; echo "ISO BUILD FAILED"; exit 1; }
echo "built in $(( ($(date +%s) - start) / 60 )) min"
cp -f "/root/out/$NAME.iso" "$OUTDIR/$NAME.iso" || exit 1
grep -E 'vintner-(core|platform)_|wexcomp_|wineexplorer(-session)?_' "/root/iso-build-$NAME.log" | head -n 6
rm -f /root/out/*.iso
rm -rf /root/iso-build "$EXPORT"
ls -la "$OUTDIR/$NAME.iso"
df -h / | tail -n 1
echo "ISO DONE"
