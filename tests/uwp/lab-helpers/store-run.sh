#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: the Store app (lab copy, never in a repository) on Wine: fresh prefix, the uwp-xaml (XamlShim 0.2.0) and
# uwp-frameworks components through their install code, the package through wexappx, launch, trace, screenshots.
# usage: calc-run.sh [WINEDEBUG]
root=/home/debian/uwp-u1-20261111
build=$root/build/candidates/u11-2/build
c=$root/claude
run=$root/build/candidates/u11-2/store-run
rm -rf "$run"; mkdir -p "$run"; chmod 0755 "$run"; cd "$run"   # the component insists on a parent that nobody else can write to
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver" WINEDEBUG=-all
WINEDLLOVERRIDES=mscoree,mshtml= "$build/wine" wineboot -i > boot.log 2>&1; "$build/server/wineserver" -w
"$build/wine" reg add 'HKCU\Software\WineExplorer\Shell' /v UwpCompatibility /t REG_DWORD /d 1 /f > reg.log 2>&1
"$build/server/wineserver" -w
# RELAY_FROM=CalcViewModel.dll with WINEDEBUG +relay: only the calls that module makes
[ -n "$RELAY_FROM" ] && "$build/wine" reg add 'HKCU\Software\Wine\Debug' /v RelayFromInclude /t REG_SZ /d "$RELAY_FROM" /f >> reg.log 2>&1
"$build/server/wineserver" -w
python3 $c/e2e-component.py $c/fw/wex-components.py $c/${XAMLSHIM_ZIP:-xamlshim-0.3.3-win-x64.zip} "$WINEPREFIX" "$build/wine" > summary 2>&1
python3 $c/fw/lab-frameworks.py install $c/fw/wex-components.py "$WINEPREFIX" $c/fw-downloads >> summary 2>&1
(cd $c/store && python3 -c 'import sys,zipfile,os,urllib.parse
src=sys.argv[1]; z=zipfile.ZipFile(sys.argv[2],"w",zipfile.ZIP_DEFLATED)
for base,dirs,files in os.walk(src):
    dirs.sort()
    for f in sorted(files):
        full=os.path.join(base,f); rel=os.path.relpath(full,src).replace(os.sep,"/")
        z.write(full,"/".join(urllib.parse.quote(p) for p in rel.split("/")))
z.close()' Microsoft.WindowsStore_22610.1401.10.0_x64__8wekyb3d8bbwe "$run/store.appx")
"$build/wine" wexappx install --development "Z:$run/store.appx" > install.log 2>&1
echo "wexappx exit $?" >> summary; tr -d '\r' < install.log >> summary
full=$(tr -d '\r' < install.log | sed -n 's/^Installed //p')
family=$(tr -d '\r' < install.log | sed -n 's/^Application: \(.*\)!App$/\1/p')
if [ -n "$full" ]; then
  export WINEDEBUG="${1:-err+all,fixme-all}" XAMLSHIM_TRACE=1 XAMLSHIM_CALLS=1
  ( sleep 20; xwd -root -silent -display "$DISPLAY" | convert xwd:- store-20s.png; sleep 25; xwd -root -silent -display "$DISPLAY" | convert xwd:- store-45s.png ) &
  # launched as from the Start menu: no arguments (the Calculator reads non-empty arguments as a mode number)
  timeout --kill-after=10s 60s "$build/wine" $c/applaunch.exe "$family!App" > launch.log 2>&1
  echo "launcher exit $?" >> summary
  sleep 50
  shimlog=$(find "$WINEPREFIX/drive_c/users" -name xamlshim.txt 2>/dev/null | head -1)
  if [ -n "$shimlog" ]; then echo "--- shim (first 80)" >> summary; grep -v ': QI ' "$shimlog" | head -80 >> summary; echo "QI misses: $(grep -c ': QI ' "$shimlog")" >> summary; cp "$shimlog" shim.txt; fi
fi
"$build/server/wineserver" -k
echo "--- err lines" >> summary
grep -aE 'err:|Unhandled|exception|unimplemented|fail' launch.log | sed 's/^[0-9a-f]*://' | sort | uniq -c | sort -rn | head -40 >> summary
echo done >> summary
