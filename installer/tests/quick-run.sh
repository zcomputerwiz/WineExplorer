#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Quick iteration (WSL Debian, as root): test one commit on an already installed disk without building an ISO or
# installing again. Only the files that differ from the installed disk are copied.
#
#   quick-run.sh REV NAME BASE [--start]
#
#   REV   commit of the checkout (as for build-test-iso.sh)
#   NAME  the new test VM (an overlay of BASE; $VMTEST_DIR/NAME)
#   BASE  an installed disk, e.g. b10-bios (run-chain.sh's install keeps NAME-bios and NAME-uefi); it is not changed
#   --start  boot NAME (headless) when done
#
# 1. wexbuild: quick-stage.sh builds Wine incrementally and stages the installed files (Wine, session, compositor);
# 2. here: an overlay of BASE is mounted, the staged files' checksums are compared with it, and the changed files
#    are streamed from wexbuild into it (tar through wsl.exe).
# Not done, unlike the ISO: the packages' maintainer scripts (postinst: enabling units, rewriting configuration),
# files that the new version no longer has are left, and packages other than Wine, the session and the
# compositor (Vintner Boot, Debian's) are as BASE has them. When the packaging or a unit changes, build an ISO.
set -uo pipefail
REV=${1:?usage: quick-run.sh REV NAME BASE [--start]}
NAME=${2:?usage: quick-run.sh REV NAME BASE [--start]}
BASE=${3:?usage: quick-run.sh REV NAME BASE [--start]}
START=${4:-}
HERE=$(cd "$(dirname "$0")" && pwd)
export VMTEST_DIR=${VMTEST_DIR:-/var/tmp/vmtest-gpt}
. "$HERE/nbd-connect.sh"
STATE=$VMTEST_DIR/$NAME
MNT=$STATE/root
W=(wsl.exe -d wexbuild -u root --)
WORK=/var/tmp/qs/$NAME
SCRIPTS=${WEX_REPO_WSL:-/mnt/d/Projects/WineExplorer}/installer/tests

umountit() { mountpoint -q "$MNT" && umount "$MNT"; [ -n "${NBD:-}" ] && qemu-nbd --disconnect "$NBD" >/dev/null; NBD=; }
trap umountit EXIT

t0=$(date +%s)
echo "=== $(date +%T) build and stage in wexbuild"
"${W[@]}" bash "$SCRIPTS/quick-stage.sh" "$REV" "$NAME" 2>&1 | tr -d '\r' | tail -n 8
mkdir -p "$WORK"
"${W[@]}" cat /root/qs/"$NAME"/manifest | tr -d '\r' > "$WORK/new.manifest"
[ -s "$WORK/new.manifest" ] || { echo "no manifest: staging failed"; exit 1; }

echo "=== $(date +%T) overlay of $BASE, compare"
python3 "$HERE/../../scripts/vmtest.py" stop "$NAME" > /dev/null 2>&1
rm -rf "$STATE" && mkdir -p "$STATE"
[ -f "$VMTEST_DIR/$BASE/disk.qcow2" ] || { echo "no installed disk $VMTEST_DIR/$BASE"; exit 1; }
qemu-img create -q -f qcow2 -b "$VMTEST_DIR/$BASE/disk.qcow2" -F qcow2 "$STATE/disk.qcow2" || exit 1
# keep the other files of the base VM state (firmware variables, ports) the way vmtest.py expects them
for f in "$VMTEST_DIR/$BASE"/*; do
    case "$f" in */disk.qcow2|*/root|*.pid|*.sock|*.log) ;; *) cp -a "$f" "$STATE/" ;; esac
done
nbd_connect rw "$STATE/disk.qcow2" || exit 1
udevadm settle; sleep 1
root=
for p in "$NBD"p*; do [ "$(blkid -o value -s TYPE "$p" 2>/dev/null)" = ext4 ] && root=$p; done
[ -n "$root" ] || { echo "no ext4 partition on $NBD"; exit 1; }
mkdir -p "$MNT" && mount "$root" "$MNT" || exit 1

# the files of the new manifest as the disk has them
{
    grep -v '^L:' "$WORK/new.manifest" | sed 's#^[0-9a-f]*  /##' | (cd "$MNT" && xargs -d '\n' -r sha256sum 2>/dev/null) | sed 's#  #  /#'
    grep '^L:' "$WORK/new.manifest" | sed 's#^L:[^ ]*  ##' | while IFS= read -r p; do
        [ -L "$MNT$p" ] && printf 'L:%s  %s\n' "$(readlink "$MNT$p")" "$p"
    done
} | sort -k2 > "$WORK/base.manifest"
comm -13 <(sort "$WORK/base.manifest") <(sort "$WORK/new.manifest") | sed 's#^[^ ]*  /##' > "$WORK/changed.list"
n=$(wc -l < "$WORK/changed.list")
echo "$n of $(wc -l < "$WORK/new.manifest") files differ from $BASE"

if [ "$n" -gt 0 ]; then
    echo "=== $(date +%T) copy the changed files"
    "${W[@]}" tar -C /root/qs/"$NAME"/all -c --no-recursion -T - < "$WORK/changed.list" | tar -C "$MNT" -x -p --numeric-owner || exit 1
fi
sync
umountit
trap - EXIT
echo "ready: VM $NAME in $(( $(date +%s) - t0 )) s ($n files changed)"
if [ "$START" = --start ]; then
    python3 "$HERE/../../scripts/vmtest.py" start "$NAME" > /dev/null && echo "started $NAME"
fi
