#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the base prefix for quick Calculator runs (calc-quick.sh): a fresh prefix with the uwp-xaml component from a
# release zip, the framework packages and the Calculator package installed, as calc-run.sh prepares it, kept in
# quick-base/ (calc-run.sh deletes calc-*). Make it again after a Wine change that registers new classes, or use
# WINEBOOT_UPDATE=1 with calc-quick.sh.
# usage: calc-prep.sh XAMLSHIM_ZIP
root=/home/debian/uwp-u1-20261111
build=$root/build/candidates/u11-2/build
c=$root/claude
base=$root/build/candidates/u11-2/quick-base
zip=${1:?usage: calc-prep.sh XAMLSHIM_ZIP}
start=$(date +%s)
rm -rf "$base"; mkdir -p "$base"; cd "$base"
export WINEPREFIX="$base/prefix" WINESERVER="$build/server/wineserver" WINEDEBUG=-all
WINEDLLOVERRIDES=mscoree,mshtml= "$build/wine" wineboot -i > boot.log 2>&1; "$build/server/wineserver" -w
"$build/wine" reg add 'HKCU\Software\WineExplorer\Shell' /v UwpCompatibility /t REG_DWORD /d 1 /f > reg.log 2>&1
"$build/server/wineserver" -w
python3 $c/e2e-component.py $c/fw/wex-components.py "$c/$zip" "$WINEPREFIX" "$build/wine" > prep.log 2>&1
python3 $c/fw/lab-frameworks.py install $c/fw/wex-components.py "$WINEPREFIX" $c/fw-downloads >> prep.log 2>&1
(cd $c/calc && python3 -c 'import sys,zipfile,os,urllib.parse
src=sys.argv[1]; z=zipfile.ZipFile(sys.argv[2],"w",zipfile.ZIP_DEFLATED)
for base,dirs,files in os.walk(src):
    dirs.sort()
    for f in sorted(files):
        full=os.path.join(base,f); rel=os.path.relpath(full,src).replace(os.sep,"/")
        z.write(full,"/".join(urllib.parse.quote(p) for p in rel.split("/")))
z.close()' Microsoft.WindowsCalculator_11.2607.0.0_x64__8wekyb3d8bbwe "$base/calc.appx")
"$build/wine" wexappx install --development "Z:$base/calc.appx" > install.log 2>&1
echo "wexappx exit $?" >> prep.log
"$build/server/wineserver" -w
"$build/server/wineserver" -k
family=$(tr -d '\r' < install.log | sed -n 's/^Application: \(.*\)!App$/\1/p')
cp "$WINEPREFIX/drive_c/Program Files/WineExplorer/Xaml/x64/classes.txt" "$base/classes.txt"
printf 'family=%s\nzip=%s\n' "$family" "$zip" > "$base/info"
du -sh "$WINEPREFIX" | cut -f1 > "$base/size"
echo "prep $(( $(date +%s) - start )) s, prefix $(cat "$base/size"), family $family" | tee -a prep.log
[ -n "$family" ] && echo prep-ok || echo prep-failed
