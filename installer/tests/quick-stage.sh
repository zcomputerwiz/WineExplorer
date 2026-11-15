#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Quick iteration, step 1 (in wexbuild, as root): build one commit's Wine incrementally and stage what the ISO
# would install, without building the ISO:
#
#   quick-stage.sh REV NAME      ->  /root/qs/NAME/all (the installed files), /root/qs/NAME/manifest
#
# It runs build-test-iso.sh up to the point where Wine is built (WEX_STAGE_ONLY=1: the Wine build is skipped when
# the last one was of the same Wine and sources), then package-wine.sh and package-wexcomp.sh leave their trees
# (WEX_STAGE_DIR) instead of .deb files. The manifest lists `SHA-256  /path` per file and `L:TARGET  /path` per
# symbolic link. quick-run.sh (WSL Debian) compares it with an installed disk and copies only the differences.
set -uo pipefail
REV=${1:?usage: quick-stage.sh REV NAME}
NAME=${2:?usage: quick-stage.sh REV NAME}
HERE=$(cd "$(dirname "$0")" && pwd)
DST=${WEX_BUILD_CLONE:-/root/WineExplorer}
ST=/root/qs/$NAME

WEX_STAGE_ONLY=1 bash "$HERE/build-test-iso.sh" "$REV" "$NAME" || exit 1

echo "=== $(date +%T) staging"
rm -rf "$ST" && mkdir -p "$ST/stage"
cd "$DST" || exit 1
WEX_STAGE_DIR=$ST/stage bash installer/package-wine.sh > "$ST/package-wine.log" 2>&1 \
    || { tail -n 30 "$ST/package-wine.log"; exit 1; }
mkdir -p "$ST/debs"
OUT_DIR=$ST/debs bash installer/package-wexcomp.sh > "$ST/package-wexcomp.log" 2>&1 \
    || { tail -n 30 "$ST/package-wexcomp.log"; exit 1; }
dpkg-deb -x "$ST"/debs/wexcomp_*.deb "$ST/stage/wexcomp"

# one tree of everything (hard links; the later package wins a clash)
mkdir -p "$ST/all"
for p in wine session wexcomp; do cp -alf "$ST/stage/$p/." "$ST/all/"; done

cd "$ST/all" || exit 1
{
    find . -type f -print0 | xargs -0 -r sha256sum | sed 's#  \./#  /#'
    find . -type l -printf 'L:%l  /%P\n'
} | sort -k2 > "$ST/manifest"
echo "staged $(wc -l < "$ST/manifest") files, $(du -sh "$ST/all" | cut -f1)"
echo "STAGE DONE"
