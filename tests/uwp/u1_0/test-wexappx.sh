#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# End-to-end check of wexappx.exe: real archives of the U1.0 fixture are installed, activated by their
# application user model id and uninstalled, for both architectures; malformed archives are refused and leave
# nothing behind.
#
#   test-wexappx.sh WINE_BUILD_DIR FIXTURE_DIR RUN_DIR
#
# FIXTURE_DIR holds this directory's scripts, wexappx.exe and x64/ and x86/ with the MSVC-built fixture.
# RUN_DIR must not exist; its fresh prefix is the only one touched.
set -uo pipefail
build=$(readlink -f "$1")
fixture=$(readlink -f "$2")
run=$3
test -x "$build/wine" || exit 1
test ! -e /etc/wineexplorer || exit 1
test ! -e "$run" || exit 1
mkdir -- "$run"
run=$(readlink -f "$run")
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver"
export WINEDEBUG="${WINEDEBUG:--all}" WINEDLLOVERRIDES=mscoree,mshtml=
mkdir -- "$WINEPREFIX"
overall=0
note() { printf '%s\n' "$*" | tee -a "$run/summary.txt"; }
w() { local log=$1; shift; timeout --kill-after=10s 120s "$build/wine" "$@" > "$log" 2>&1; }
check() { if [ "$1" = 0 ]; then note "PASS $2"; else note "FAIL $2"; overall=1; fi; }
store="$WINEPREFIX/drive_c/users/$USER/AppData/Local/WineExplorer/WindowsApps"
catalog="$WINEPREFIX/appx-catalog"
leftovers() { { ls -A "$store" 2>/dev/null; ls -A "$catalog" 2>/dev/null; } | wc -l; }

w "$run/wineboot.log" wineboot -i || { note "FAIL wineboot"; exit 1; }
mkdir -- "$WINEPREFIX/drive_c/u1out" "$run/pkg"
cp "$fixture/wexappx.exe" "$WINEPREFIX/drive_c/windows/system32/wexappx.exe"

# real archives of the three fixture variants, per architecture (deflated, plus the usual package files)
for arch in x64 x86; do
  for case in positive missing_class missing_dll; do
    stage="$run/pkg/$arch-$case"
    python3 "$fixture/register-wine.py" stage "$case" "$arch" "$fixture/$arch" "$stage"
    printf '<?xml version="1.0"?><Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types"/>' > "$stage/[Content_Types].xml"
    mkdir "$stage/Assets"; printf 'asset' > "$stage/Assets/note.txt"
    (cd "$stage" && python3 -c 'import sys,zipfile,os
z=zipfile.ZipFile(sys.argv[1],"w",zipfile.ZIP_DEFLATED)
for base,dirs,files in os.walk("."):
    for f in sorted(files):
        p=os.path.relpath(os.path.join(base,f),".")
        z.write(p,p.replace(os.sep,"/").replace("[","%5B").replace("]","%5D"))
z.close()' "$run/pkg/$arch-$case.appx")
  done
done

# the gate is off: nothing is installed
w "$run/gate-off.log" wexappx install --development "Z:$run/pkg/x64-positive.appx"; rc=$?
[ "$rc" != 0 ] && [ "$(leftovers)" = 0 ]; check $? "install refused while the gate is off"
w "$run/reg.log" reg add 'HKCU\Software\WineExplorer\Shell' /v UwpCompatibility /t REG_DWORD /d 1 /f

# --development is required
w "$run/no-flag.log" wexappx install "Z:$run/pkg/x64-positive.appx"; rc=$?
[ "$rc" != 0 ] && [ "$(leftovers)" = 0 ]; check $? "install refused without --development"

for arch in x64 x86; do
  : > "$run/$arch-actual.txt"; : > "$run/$arch-hresults.txt"
  for case in positive missing_class missing_dll; do
    w "$run/$arch-$case-install.log" wexappx install --development "Z:$run/pkg/$arch-$case.appx"; rc=$?
    full=$(tr -d '\r' < "$run/$arch-$case-install.log" | sed -n 's/^Installed //p')
    aumid=$(tr -d '\r' < "$run/$arch-$case-install.log" | sed -n 's/^Application: //p')
    if [ "$rc" != 0 ] || [ -z "$full" ] || [ -z "$aumid" ]; then note "FAIL $arch $case install (exit $rc)"; overall=1; continue; fi
    # a second install of the same package is refused and changes nothing
    w "$run/$arch-$case-again.log" wexappx install --development "Z:$run/pkg/$arch-$case.appx"; [ $? != 0 ]; check $? "$arch $case second install refused"
    w "$run/$arch-$case-list.log" wexappx list; tr -d '\r' < "$run/$arch-$case-list.log" | grep -q -x -F "$full"; check $? "$arch $case listed"
    [ -f "$store/$full/[Content_Types].xml" ] && [ -f "$store/$full/Assets/note.txt" ]; check $? "$arch $case files extracted"
    w "$run/$arch-$case-launch.log" "$fixture/$arch/launcher.exe" "$aumid" "$case" "C:\\u1out\\$arch-$case.txt" "$full"; rc=$?
    text="$WINEPREFIX/drive_c/u1out/$arch-$case.txt"
    if [ -f "$text" ]; then
      tr -d '\r' < "$text" > "$run/$arch-$case.txt"
      if [ "$case" = positive ]; then cat "$run/$arch-$case.txt" >> "$run/$arch-actual.txt"
      elif [ "$rc" = 0 ] && grep -q -x 'PASS negative_activation' "$run/$arch-$case.txt"; then
        echo "PASS $case" >> "$run/$arch-actual.txt"; grep -E "^HRESULT $case " "$run/$arch-$case.txt" >> "$run/$arch-hresults.txt"
      else echo "FAIL $case" >> "$run/$arch-actual.txt"; fi
    else echo "FAIL $case (no output, exit $rc)" >> "$run/$arch-actual.txt"; fi
    w "$run/$arch-$case-uninstall.log" wexappx uninstall "$full"; rc=$?
    [ "$rc" = 0 ] && [ "$(leftovers)" = 0 ]; check $? "$arch $case uninstall leaves nothing"
    w "$run/$arch-$case-gone.log" "$fixture/$arch/launcher.exe" "$aumid" "$case" 'C:\u1out\never.txt' "$full"
    grep -q 'ActivateApplication: 0x80270254' "$run/$arch-$case-gone.log"; check $? "$arch $case not activatable after uninstall"
  done
  grep -v -e '^#' -e '^$' -e unpackaged "$fixture/expected.txt" > "$run/expected.txt"
  diff -u "$run/expected.txt" "$run/$arch-actual.txt" > "$run/$arch-diff.txt"; check $? "$arch installed fixture: nine packaged checks"
  grep -v unpackaged "$fixture/observations.txt" | diff -u - "$run/$arch-hresults.txt" > "$run/$arch-diff-hr.txt"; check $? "$arch installed fixture: HRESULTs match Windows"
done

# malformed archives: refused, nothing left behind
bad="$run/bad"; mkdir "$bad"
python3 - "$run/pkg/x64-positive" "$bad" <<'EOF'
import os, sys, zipfile, struct
stage, out = sys.argv[1], sys.argv[2]
files = {}
for base, dirs, names in os.walk(stage):
    for n in names:
        p = os.path.join(base, n)
        files[os.path.relpath(p, stage).replace(os.sep, '/')] = open(p, 'rb').read()
def make(name, extra=(), drop=(), manifest=None, method=zipfile.ZIP_DEFLATED):
    z = zipfile.ZipFile(os.path.join(out, name + '.appx'), 'w', method)
    for k, v in files.items():
        if k in drop: continue
        if k == 'AppxManifest.xml' and manifest is not None: v = manifest
        z.writestr(k.replace('[', '%5B').replace(']', '%5D'), v)
    for k, v in extra: z.writestr(k, v)
    z.close()
m = files['AppxManifest.xml'].decode()
make('traversal', extra=[('../escape.txt', b'x')])
make('absolute', extra=[('/abs.txt', b'x')])
make('drive', extra=[('C:/drive.txt', b'x')])
make('backslash', extra=[('dir\\file.txt', b'x')])
make('encoded-separator', extra=[('dir%2Ffile.txt', b'x')])
make('stream', extra=[('probe.exe:stream', b'x')])
make('duplicate-case', extra=[('PROBE.EXE', b'x')])
make('file-and-directory', extra=[('probe.exe/inner.txt', b'x')])
make('dot-component', extra=[('a/./b.txt', b'x')])
make('trailing-dot', extra=[('name./b.txt', b'x')])
make('no-manifest', drop=('AppxManifest.xml',))
make('no-executable', drop=('probe.exe',))
make('dependency', manifest=m.replace('<Dependencies>', '<Dependencies><PackageDependency Name="Other.Framework" MinVersion="1.0.0.0" Publisher="CN=X" />').encode())
make('bad-version', manifest=m.replace('Version="1.0.0.0"', 'Version="1.0.0"').encode())
make('bad-app-id', manifest=m.replace('Id="Probe"', 'Id="..\\\\Probe"').encode())
make('exe-traversal', manifest=m.replace('Executable="probe.exe"', 'Executable="..\\\\probe.exe"').encode())
make('not-xml', manifest=b'<Package><Identity')
make('dtd', manifest=m.replace('<Package', '<!DOCTYPE Package [<!ENTITY x "y">]><Package', 1).encode())
make('bomb', extra=[('zeros.bin', b'\0' * (64 * 1024 * 1024))])
# a member whose data does not match its checksum
good = open(os.path.join(out, 'no-executable.appx'), 'rb').read()
make('crc')
data = bytearray(open(os.path.join(out, 'crc.appx'), 'rb').read())
pos = data.find(b'PK\x01\x02')
struct.pack_into('<I', data, pos + 16, 0x12345678)
open(os.path.join(out, 'crc.appx'), 'wb').write(data)
open(os.path.join(out, 'truncated.appx'), 'wb').write(good[:len(good) // 2])
open(os.path.join(out, 'garbage.appx'), 'wb').write(b'not an archive at all' * 10)
EOF
for f in "$bad"/*.appx; do
  name=$(basename "$f" .appx)
  w "$run/bad-$name.log" wexappx install --development "Z:$f"; rc=$?
  [ "$rc" != 0 ] && [ "$(leftovers)" = 0 ] && [ ! -e "$WINEPREFIX/drive_c/users/$USER/AppData/Local/WineExplorer/escape.txt" ]
  check $? "malformed archive refused: $name ($(tr -d '\r' < "$run/bad-$name.log" | head -1 | cut -c1-90))"
done
# uninstall takes a full name only
w "$run/uninstall-traversal.log" wexappx uninstall '..\..\..\Desktop'; [ $? != 0 ]; check $? "uninstall refuses a path"

timeout --kill-after=5s 30s "$WINESERVER" -k > /dev/null 2>&1; killed=$?
timeout --kill-after=5s 30s "$WINESERVER" -w > /dev/null 2>&1; waited=$?
note "cleanup kill=$killed wait=$waited"
note "overall=$overall"
echo "$overall" > "$run/exit"
exit "$overall"
