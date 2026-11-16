#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# wexgpo-wine-addons.sh REPO - Wine Mono and Wine Gecko where Wine finds them on a build machine (docs/GPO_LAB.md,
# "The build machine"): /usr/share/wine/{mono,gecko}, which a build tree's Wine (its own prefix) searches besides
# its prefix's share/wine, and /usr/local/share/wine/{mono,gecko} for a Wine installed in /usr/local. Without them the machine root's system-group wineboot --init opens the Mono download dialog, which
# nobody answers, and every start that updates the machine root waits the launcher's 5 minutes
# (dlls/ntdll/unix/env.c); an installed system has them from the ISO. The packages, URLs and SHA-256 sums are
# installer/runtime-assets.json's; a copy in /var/cache/wineexplorer/downloads (package-wine.sh's cache) is used
# when its sum matches, else the package is downloaded from dl.winehq.org into that cache first. As root.
set -euo pipefail
REPO=${1:?repository}
CACHE=/var/cache/wineexplorer/downloads
mkdir -p "$CACHE" /usr/share/wine/mono /usr/share/wine/gecko /usr/local/share/wine/mono /usr/local/share/wine/gecko
python3 -I - "$REPO/installer/runtime-assets.json" <<'PY' > /tmp/wine-addons.list
import json, sys
text = open(sys.argv[1], encoding='utf-8').read()
data = json.loads(text)
items = data if isinstance(data, list) else next(v for v in data.values() if isinstance(v, list))
for name, url, sha in items:
    if name.startswith(('wine-mono-', 'wine-gecko-')) and name.endswith('.msi'):
        print(name, url, sha)
PY
[ -s /tmp/wine-addons.list ] || { echo "no Mono/Gecko entries in runtime-assets.json"; exit 1; }
while read -r name url sha; do
    if ! echo "$sha  $CACHE/$name" | sha256sum -c --status 2>/dev/null; then
        curl -fsSL -o "$CACHE/$name.part" "$url"
        echo "$sha  $CACHE/$name.part" | sha256sum -c --status || { rm -f "$CACHE/$name.part"; echo "checksum mismatch: $name"; exit 1; }
        mv "$CACHE/$name.part" "$CACHE/$name"
    fi
    case $name in wine-mono-*) kind=mono ;; *) kind=gecko ;; esac
    for dest in /usr/share/wine/$kind /usr/local/share/wine/$kind; do install -m 0644 "$CACHE/$name" "$dest/$name"; done
    echo "installed $name in /usr/share/wine/$kind and /usr/local/share/wine/$kind"
done < /tmp/wine-addons.list
rm -f /tmp/wine-addons.list
