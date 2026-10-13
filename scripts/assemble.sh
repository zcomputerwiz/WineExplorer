#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Connect project modules before configuring Wine. No resource overlays are read.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
wine_root="${1:-$root/wine}"
wine_root="$(realpath "$wine_root")"
test -f "$wine_root/configure.ac"
for kind in dlls programs; do
    for source in "$root/src/$kind/"*; do
        [ -f "$source/Makefile.in" ] || continue
        target="$wine_root/$kind/$(basename "$source")"
        if [ -L "$target" ]; then
            test "$(realpath "$target")" = "$(realpath "$source")" || {
                echo "Unexpected module link: $target" >&2; exit 1;
            }
        elif [ -e "$target" ]; then
            echo "Module destination already exists: $target" >&2
            exit 1
        else
            ln -s "$(realpath --relative-to="$(dirname "$target")" "$source")" "$target"
        fi
    done
done
for source in "$root/src/include/"*; do
    [ -f "$source" ] || continue
    target="$wine_root/include/$(basename "$source")"
    if [ -L "$target" ]; then
        test "$(realpath "$target")" = "$(realpath "$source")" || exit 1
    elif [ -e "$target" ]; then
        echo "Header destination already exists: $target" >&2
        exit 1
    else
        ln -s "$(realpath --relative-to="$(dirname "$target")" "$source")" "$target"
    fi
done
# Retained configure.ac contains project entries; configure is generated upstream output.
(cd "$wine_root" && autoconf -f)
