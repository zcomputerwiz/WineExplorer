#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# UWP application frameworks on Wine (x64): in a fresh prefix standing in for the machine root, a package that depends
# on the five framework packages of the uwp-frameworks component is refused while they are missing; the component's
# install steps put them into C:\Program Files\WindowsApps and the machine catalog; the package then installs with
# wexappx, its program is activated through the U1.0 launcher and its checks are compared with expected.txt; finally the
# package and the frameworks are removed and nothing is left.
#
#   run-wine-frameworks.sh WINE_BUILD_DIR U1_0_DIR FRAMEWORKS_DIR COMPONENTS_PY DOWNLOADS RESULTS_DIR
#
# FRAMEWORKS_DIR holds AppxManifest.xml, expected.txt, lab-frameworks.py and x64/fwprobe.exe; DOWNLOADS keeps the
# pinned containers between runs; RESULTS_DIR must not exist.
set -euo pipefail
build=$(readlink -f "$1")
u10=$(readlink -f "$2")
fw=$(readlink -f "$3")
components=$(readlink -f "$4")
downloads=$(readlink -m "$5")
results=$6
test -x "$build/wine" && test -x "$build/server/wineserver"
mkdir -- "$results"
results=$(readlink -f "$results")
export WINESERVER="$build/server/wineserver"
export WINEDEBUG="${WINEDEBUG:--all}" WINEDLLOVERRIDES=mscoree,mshtml=
overall=0

note() { printf '%s\n' "$*" | tee -a "$results/summary.txt"; }
wine_run() { local log=$1; shift; timeout --kill-after=10s 180s "$build/wine" "$@" > "$log" 2>&1; }

out="$results/x64"
mkdir -- "$out"
export WINEPREFIX="$out/prefix"
mkdir -- "$WINEPREFIX"
wine_run "$out/wineboot.log" wineboot -i || { note "FAIL wineboot"; exit 1; }
wine_run "$out/reg.log" reg add 'HKCU\Software\WineExplorer\Shell' /v UwpCompatibility /t REG_DWORD /d 1 /f
mkdir -p "$WINEPREFIX/drive_c/u1out"
"$WINESERVER" -w

# the probe package
stage=$(mktemp -d)
cp "$fw/AppxManifest.xml" "$fw/x64/fwprobe.exe" "$stage/"
head -c 100 /dev/zero > "$stage/logo.png"; head -c 100 /dev/zero > "$stage/small.png"
(cd "$stage" && python3 -c 'import sys,zipfile,os
z=zipfile.ZipFile(sys.argv[1],"w",zipfile.ZIP_DEFLATED)
for f in sorted(os.listdir(".")):
    z.write(f,f)
z.close()' "$out/probe.appx")
rm -rf "$stage"

status=0
wine_run "$out/probe-no-frameworks.log" wexappx install --development "Z:$out/probe.appx" || status=$?
if [ "$status" != 0 ] && grep -q 'not installed' "$out/probe-no-frameworks.log"; then
    note "PASS the package is refused while its frameworks are missing"
else note "FAIL the package without its frameworks (exit $status)"; overall=1; fi

if python3 "$fw/lab-frameworks.py" install "$components" "$WINEPREFIX" "$downloads" > "$out/frameworks-install.log" 2>&1; then
    note "PASS the component installs the frameworks ($(grep -c '^installed' "$out/frameworks-install.log") packages)"
else note "FAIL the component install"; tail -5 "$out/frameworks-install.log" >> "$results/summary.txt"; exit 1; fi

wine_run "$out/probe-install.log" wexappx install --development "Z:$out/probe.appx" || { note "FAIL probe install"; cat "$out/probe-install.log" >> "$results/summary.txt"; exit 1; }
full=$(tr -d '\r' < "$out/probe-install.log" | sed -n 's/^Installed //p')
family=$(tr -d '\r' < "$out/probe-install.log" | sed -n 's/^Application: \(.*\)!Probe$/\1/p')
note "PASS the package installs against the machine's frameworks ($full)"

output="$WINEPREFIX/drive_c/u1out/fwprobe.txt"
wine_run "$out/launch.log" "$u10/x64/launcher.exe" "$family!Probe" positive 'C:\u1out\fwprobe.txt' "$full" || true
for i in $(seq 30); do [ -f "$output" ] && grep -q '^exit' "$output" && break; sleep 1; done
if [ ! -f "$output" ]; then note "FAIL the probe produced no output"; overall=1
else
    cp "$output" "$out/fwprobe.txt"
    tr -d '\r' < "$out/fwprobe.txt" | grep -E '^(PASS|FAIL) ' | LC_ALL=C sort > "$out/checks.txt" || true
    grep -v -e '^#' -e '^$' "$fw/expected.txt" | LC_ALL=C sort > "$out/expected.txt"
    if diff -u "$out/expected.txt" "$out/checks.txt" > "$out/checks.diff"; then note "PASS the probe (all $(wc -l < "$out/expected.txt") checks)"
    else note "FAIL the probe"; cat "$out/checks.diff" >> "$results/summary.txt"; overall=1; fi
    tr -d '\r' < "$out/fwprobe.txt" | grep '^INFO' >> "$results/observations.txt" || true
fi

wine_run "$out/probe-uninstall.log" wexappx uninstall "$full" || { note "FAIL probe uninstall"; overall=1; }
python3 "$fw/lab-frameworks.py" uninstall "$components" "$WINEPREFIX" > "$out/frameworks-uninstall.log" 2>&1 || { note "FAIL frameworks uninstall"; overall=1; }
left=$(ls -A "$WINEPREFIX/drive_c/Program Files/WindowsApps" "$WINEPREFIX/drive_c/ProgramData/WineExplorer/appx-catalog" "$WINEPREFIX/appx-catalog" 2>/dev/null | grep -v -e '^$' -e ':$' || true)
if [ -z "$left" ]; then note "PASS the package and the frameworks uninstall and leave nothing"
else note "FAIL leftovers after uninstall: $left"; overall=1; fi

timeout --kill-after=5s 30s "$WINESERVER" -k > /dev/null 2>&1 || true
note "overall=$overall"
exit "$overall"
