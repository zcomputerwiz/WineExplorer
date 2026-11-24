#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: tests/uwp/alpc/token-probe.c as a packaged application (AppContainer) on the Wine build of candidate u12, in a fresh prefix:
# its own token, and the dwm.exe server check sequence against an ordinary process that granted S-1-15-2-1 the access.
# usage: token-run.sh      (token-probe.c and applaunch.exe are in claude/)
# the workspace of the lab guest (override with UWP_WS)
UWP_WS=${UWP_WS:-/home/debian/uwp-u1-20261111}
root=${UWP_WS}
b=$root/build/candidates/u12/build
run=$root/build/candidates/u12/token-run
c=$root/claude
x86_64-w64-mingw32-gcc -O2 -o $c/token-probe.exe $c/token-probe.c -ladvapi32 || exit 1
rm -rf "$run"; mkdir -p "$run/pkg"; cd "$run"
cp $c/token-probe.exe pkg/probe.exe
cat > pkg/AppxManifest.xml <<'EOF'
<?xml version="1.0" encoding="utf-8"?>
<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
         xmlns:uap="http://schemas.microsoft.com/appx/manifest/uap/windows10"
         xmlns:rescap="http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities"
         IgnorableNamespaces="uap rescap">
  <Identity Name="WineExplorer.TokenProbe" Publisher="CN=WineExplorer Token Probe" Version="1.0.0.0" ProcessorArchitecture="x64" />
  <Properties><DisplayName>Token probe</DisplayName><PublisherDisplayName>WineExplorer</PublisherDisplayName><Logo>logo.png</Logo></Properties>
  <Dependencies><TargetDeviceFamily Name="Windows.Desktop" MinVersion="10.0.17763.0" MaxVersionTested="10.0.26100.0" /></Dependencies>
  <Resources><Resource Language="en-us" /></Resources>
  <Applications>
    <Application Id="Probe" Executable="probe.exe" EntryPoint="Windows.FullTrustApplication">
      <uap:VisualElements DisplayName="Token probe" Description="Token probe" BackgroundColor="transparent" Square150x150Logo="logo.png" Square44x44Logo="logo.png" />
    </Application>
  </Applications>
  <Capabilities><rescap:Capability Name="runFullTrust" /></Capabilities>
</Package>
EOF
: > pkg/logo.png
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
python3 zipdir.py pkg tokenprobe.appx
export WINEPREFIX="$run/prefix" WINESERVER="$b/server/wineserver" WINEDEBUG=-all DISPLAY=${DISPLAY:-:99}
WINEDLLOVERRIDES=mscoree,mshtml= "$b/wine" wineboot -i > boot.log 2>&1; "$b/server/wineserver" -w
"$b/wine" reg add 'HKCU\Software\WineExplorer\Shell' /v UwpCompatibility /t REG_DWORD /d 1 /f > reg.log 2>&1
"$b/wine" wexappx install --development "Z:$run/tokenprobe.appx" > install.log 2>&1; tr -d '\r' < install.log | head -5
family=$(tr -d '\r' < install.log | sed -n 's/^Application: \(.*\)!Probe$/\1/p')
echo "family $family"
rm -f pid.txt
( "$b/wine" $c/token-probe.exe target Z:$run/pid.txt Z:$run/target.txt > /dev/null 2>&1 & )
for i in $(seq 1 30); do [ -s pid.txt ] && break; sleep 1; done
echo "--- target (an ordinary process)"; tr -d '\r' < target.txt
echo "--- packaged client (AppContainer)"
timeout 60 "$b/wine" $c/applaunch.exe "$family!Probe" "client Z:$run/pid.txt Z:$run/client.txt" > launch.log 2>&1
echo "launcher exit $?"; sleep 2
tr -d '\r' < client.txt 2>/dev/null || { echo "(no client output)"; tr -d '\r' < launch.log | tail -5; }
"$b/server/wineserver" -k
