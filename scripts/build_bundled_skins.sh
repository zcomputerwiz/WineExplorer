#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Builds open-art Open-Shell Start menu skins into PE resource DLLs (.skin / .skin7)

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
SKINS_SRC="/root/Open-Shell-Menu/Src/Skins"
SETUP_DIR="/root/Open-Shell-Menu/Src/Setup"
DEST_SRC="$ROOT_DIR/src/resources/startskins"
DEST_SYS="/usr/local/share/wineexplorer/startskins"

mkdir -p "$DEST_SRC"
mkdir -p "$DEST_SYS" 2>/dev/null || true

BUILD_TMP="/tmp/wex_skin_build"
rm -rf "$BUILD_TMP"
mkdir -p "$BUILD_TMP"

build_one_skin() {
    local name="$1"
    local ext="$2"
    local src_dir="$SKINS_SRC/$name"
    local rc_src="$src_dir/$name.rc"
    local rc_prep="$BUILD_TMP/$name.rc"
    local obj_file="$BUILD_TMP/$name.o"
    local out_file="$DEST_SRC/$name.$ext"

    if [ ! -d "$src_dir" ]; then
        echo "WARNING: Skin source directory not found: $src_dir"
        return
    fi

    echo "Building skin: $name.$ext..."

    # Normalize resource script paths for mingw windres
    sed -e 's|\.\.\\\.\.\\Setup\\OpenShell\.ico|/root/Open-Shell-Menu/Src/Setup/OpenShell.ico|g' \
        -e 's|\\|/|g' \
        "$rc_src" > "$rc_prep"

    (
        cd "$src_dir"
        x86_64-w64-mingw32-windres -I . -I "$src_dir" -I "$SETUP_DIR" -i "$rc_prep" -o "$obj_file"
        x86_64-w64-mingw32-gcc -shared -nostdlib -Wl,-e,0 "$obj_file" -o "$out_file"
    )

    if [ -w "$DEST_SYS" ]; then
        cp -f "$out_file" "$DEST_SYS/$name.$ext"
    fi

    ls -lh "$out_file"
}

# Build open-art skins (only those with Open-Shell's own artwork per docs/ASSETS.md)
build_one_skin "ClassicSkin"  "skin"
build_one_skin "ClassicSkin7" "skin7"
build_one_skin "Metallic7"    "skin7"
build_one_skin "Midnight7"    "skin7"
build_one_skin "Metro"        "skin"

rm -rf "$BUILD_TMP"
echo "Start menu skins build complete."
