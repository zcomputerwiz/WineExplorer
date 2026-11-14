#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# package the U1.1 fixture and push it to the guest as claude/u11fix
set -euo pipefail
P=/c/Users/Owner/AppData/Local/Temp/claude/f10dd599a76cdd8a/cc6dd05f-7e73-49f2-be6b-aa4119777252/scratchpad
S=/d/DeepThought/Projects/WineExplorer/tests/uwp
rm -rf "$P/u11fix" && mkdir -p "$P/u11fix/u1_0/x64" "$P/u11fix/u1_0/x86" "$P/u11fix/u1_1/x64" "$P/u11fix/u1_1/x86"
cp $S/u1_0/register-wine.py "$P/u11fix/u1_0/"; cp $S/u1_0/build/x64/launcher.exe "$P/u11fix/u1_0/x64/"; cp $S/u1_0/build/x86/launcher.exe "$P/u11fix/u1_0/x86/"
cp $S/u1_1/build/x64/view.exe "$P/u11fix/u1_1/x64/"; cp $S/u1_1/build/x86/view.exe "$P/u11fix/u1_1/x86/"
cp $S/u1_1/{inject.c,expected-present.txt,AppxManifest.xml,expected-positive.txt,expected-return.txt,expected-sta.txt,expected-unpackaged.txt,expected-pkgapi.txt,expected-appdata-1.txt,expected-appdata-2.txt,expected-storage.txt,run-wine-lifecycle.sh} "$P/u11fix/u1_1/"
(cd "$P/u11fix" && sha256sum u1_0/x64/* u1_0/x86/* u1_1/x64/* u1_1/x86/* > SHA256SUMS && tar -cf ../u11fix.tar .)
cd "$P" && bash lab.sh push u11fix.tar
bash lab.sh ssh 'cd /home/debian/uwp-u1-20261111/claude && rm -rf u11fix && mkdir u11fix && tar -xf u11fix.tar -C u11fix && (cd u11fix && sha256sum -c --quiet SHA256SUMS && echo fixture-ok)'
