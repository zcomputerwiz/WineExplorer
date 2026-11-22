#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: a Windows App SDK application (lab copy, never in a repository) on Wine: fresh prefix, the framework components, the
# Windows App Runtime 2.x framework package and the application through wexappx, launch, trace. The packages are in claude/wasdk
# (wasdk.tar). usage: wasdk-run.sh [APP_DIR [WINEDEBUG]]       (APP_DIR defaults to the Notepad package folder)
root=/home/debian/uwp-u1-20261111
build=$root/build/candidates/u11-2/build
c=$root/claude
run=$root/build/candidates/u11-2/wasdk-run
fw=Microsoft.WindowsAppRuntime.2_2.7.0.0_x64__8wekyb3d8bbwe
app=${1:-Microsoft.WindowsNotepad_11.2609.11.0_x64__8wekyb3d8bbwe}
rm -rf "$run"; mkdir -p "$run"; chmod 0755 "$run"; cd "$run"
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver" WINEDEBUG=-all DISPLAY=${DISPLAY:-:99}
WINEDLLOVERRIDES=mscoree,mshtml= "$build/wine" wineboot -i > boot.log 2>&1; "$build/server/wineserver" -w
"$build/wine" reg add 'HKCU\Software\WineExplorer\Shell' /v UwpCompatibility /t REG_DWORD /d 1 /f > reg.log 2>&1
# RELAY_FROM=Microsoft.Internal.FrameworkUdk.dll with WINEDEBUG +relay: only the calls that module makes
[ -n "$RELAY_FROM" ] && "$build/wine" reg add 'HKCU\Software\Wine\Debug' /v RelayFromInclude /t REG_SZ /d "$RELAY_FROM" /f >> reg.log 2>&1
"$build/server/wineserver" -w
python3 $c/fw/lab-frameworks.py install $c/fw/wex-components.py "$WINEPREFIX" $c/fw-downloads > summary 2>&1
cat > zipdir.py <<'PYEOF'
import sys, zipfile, os, urllib.parse
src = sys.argv[1]
z = zipfile.ZipFile(sys.argv[2], "w", zipfile.ZIP_DEFLATED)
for base, dirs, files in os.walk(src):
    dirs.sort()
    for f in sorted(files):
        full = os.path.join(base, f)
        rel = os.path.relpath(full, src).replace(os.sep, "/")
        z.write(full, "/".join(urllib.parse.quote(p) for p in rel.split("/")))
z.close()
PYEOF
for d in "$fw" "$app"; do
  rm -rf "$c/wasdk/$d/microsoft.system.package.metadata"   # added by the system to an installed copy, not in the block map
  (cd $c/wasdk && python3 "$run/zipdir.py" "$d" "$run/$d.appx")
  flag=; [ "$d" = "$fw" ] && flag=--all-users   # frameworks belong to the machine catalog: the server takes dependency classes from there
  "$build/wine" wexappx install --development $flag "Z:$run/$d.appx" > "install-$d.log" 2>&1
  echo "wexappx $d exit $?" >> summary; tr -d '\r' < "install-$d.log" | head -8 >> summary
done
family=$(tr -d '\r' < "install-$app.log" | sed -n 's/^Application: \(.*\)!App$/\1/p')
echo "family $family" >> summary
export WINEDEBUG="${2:-err+all,fixme-all}" XAMLSHIM_TRACE=1
( sleep 25; xwd -root -silent -display "$DISPLAY" | convert xwd:- wasdk-25s.png ) &
timeout --kill-after=10s 50s "$build/wine" $c/applaunch.exe "$family!App" > launch.log 2>&1
echo "launcher exit $?" >> summary
sleep 3
echo "--- err lines" >> summary
grep -aE 'err:|Unhandled|exception|unimplemented|fixme' launch.log | sed 's/^[0-9a-f]*://' | sort | uniq -c | sort -rn | head -40 >> summary
"$build/server/wineserver" -k
echo done >> summary
