#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Prepare the public fork before project source and private assets are synced.
set -euo pipefail
source_root=$(realpath -m "$1")
wanted=$2
[[ "$wanted" =~ ^[0-9a-f]{40}$ ]]
test "$source_root" != /
mkdir -p "$source_root/build"
wine_root="$source_root/wine"
if [ -d "$wine_root" ] && [ ! -L "$wine_root" ]; then
    actual_top=$(git -C "$wine_root" rev-parse --show-toplevel 2>/dev/null || true)
    actual_head=$(git -C "$wine_root" rev-parse HEAD 2>/dev/null || true)
    if [ "$actual_top" = "$wine_root" ] && [ "$actual_head" = "$wanted" ]; then
        echo WINE_SOURCE_CURRENT
        exit 0
    fi
fi
# Clone first: a network failure leaves the current source available.
next=$(mktemp -d "$source_root/build/wine-source-next.XXXXXX")
git clone --quiet --depth 1 --single-branch --branch wineexplorer/11.19 \
    https://github.com/zcomputerwiz/wine.git "$next"
if [ "$(git -C "$next" rev-parse HEAD)" != "$wanted" ]; then
    git -C "$next" fetch --quiet --depth 1 origin "$wanted"
    git -C "$next" checkout --quiet --detach "$wanted"
fi
test "$(git -C "$next" rev-parse HEAD)" = "$wanted"
saved=$(mktemp -d "$source_root/build/wine-source-backup.XXXXXX")
if [ -e "$wine_root" ]; then
    test ! -L "$wine_root"
    test "$(realpath "$wine_root")" = "$source_root/wine"
    if [ -d "$source_root/src" ]; then cp -a "$source_root/src" "$saved/project-src"; fi
    mv "$wine_root" "$saved/wine"
fi
mv "$next" "$wine_root"
echo "WINE_SOURCE_PREPARED $wanted"
