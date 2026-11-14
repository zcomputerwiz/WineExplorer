#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# package the U1.3 fixture and push it to the guest as claude/u13fix
set -euo pipefail
P=/c/Users/Owner/AppData/Local/Temp/claude/f10dd599a76cdd8a/cc6dd05f-7e73-49f2-be6b-aa4119777252/scratchpad
S=/d/DeepThought/Projects/WineExplorer/tests/uwp
rm -rf "$P/u13fix" && mkdir -p "$P/u13fix/u1_3/x64" "$P/u13fix/u1_3/x86"
cp $S/u1_3/build/x64/{u1fw.dll,depapp.exe} "$P/u13fix/u1_3/x64/"; cp $S/u1_3/build/x86/{u1fw.dll,depapp.exe} "$P/u13fix/u1_3/x86/"
cp $S/u1_3/{FrameworkManifest.xml,AppManifest.xml,expected.txt,run-wine-deps.sh} "$P/u13fix/u1_3/"
(cd "$P/u13fix" && sha256sum u1_3/x64/* u1_3/x86/* > SHA256SUMS && tar -cf ../u13fix.tar .)
cd "$P" && bash lab.sh push u13fix.tar
bash lab.sh ssh 'cd /home/debian/uwp-u1-20261111/claude && rm -rf u13fix && mkdir u13fix && tar -xf u13fix.tar -C u13fix && (cd u13fix && sha256sum -c --quiet SHA256SUMS && echo fixture-ok)'
