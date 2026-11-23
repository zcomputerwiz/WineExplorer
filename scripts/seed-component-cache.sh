#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Test harness helper: puts pinned component payloads from a host-side cache into a mounted test image, so a test run
# does not download them again. Usage: seed-component-cache.sh IMAGE_ROOT [HOST_CACHE]
#
# HOST_CACHE (default /mnt/d/WSL/wex-iso/component-cache, or $WEX_COMPONENT_HOST_CACHE) holds VERB/FILE, for example
# powershell/PowerShell-7.7.0-win-x64.zip. The files land in IMAGE_ROOT/var/cache/winetricks/VERB/ root-owned, the
# directory 0700 and the file 0600, where wex-components.py's cache_pinned_download looks. The backend still checks
# the size and SHA-256 against its pin before using a file, so a stale or wrong file in the host cache is refused
# there ("Corrupt ... cache; nothing installed"). The terms still have to be accepted: only the download is skipped.
set -eu
root=${1:?usage: seed-component-cache.sh IMAGE_ROOT [HOST_CACHE]}
host=${2:-${WEX_COMPONENT_HOST_CACHE:-/mnt/d/WSL/wex-iso/component-cache}}
[ -d "$root/var" ] || { echo "not an image root: $root" >&2; exit 1; }
[ -d "$host" ] || { echo "no host cache at $host (nothing seeded)" >&2; exit 0; }
for dir in "$host"/*/; do
    [ -d "$dir" ] || continue
    verb=$(basename "$dir")
    target="$root/var/cache/winetricks/$verb"
    install -d -m 700 -o root -g root "$root/var/cache/winetricks" "$target"
    for file in "$dir"*; do
        [ -f "$file" ] || continue
        install -m 600 -o root -g root "$file" "$target/$(basename "$file")"
        echo "seeded $verb/$(basename "$file")"
    done
done
