#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# U1.3 on Wine: a package that depends on a framework package. Real archives are made from the MSVC-built fixture,
# installed with wexappx in a fresh prefix per architecture: the dependent package is refused without its framework
# (and leaves nothing behind), both are installed, the dependent program is activated through the U1.0 launcher and its
# checks are compared with expected.txt (it loads the framework's library by name and asks for its package graph),
# then both are uninstalled.
#
#   run-wine-deps.sh WINE_BUILD_DIR U1_0_DIR U1_3_DIR RESULTS_DIR
#
# U1_0_DIR holds x64/ x86/ with launcher.exe; U1_3_DIR holds FrameworkManifest.xml, AppManifest.xml, expected.txt and
# x64/ x86/ with u1fw.dll and depapp.exe. RESULTS_DIR must not exist.
set -euo pipefail
build=$(readlink -f "$1")
u10=$(readlink -f "$2")
u13=$(readlink -f "$3")
results=$4
test -x "$build/wine" && test -x "$build/server/wineserver"
mkdir -- "$results"
results=$(readlink -f "$results")
export WINESERVER="$build/server/wineserver"
export WINEDEBUG="${WINEDEBUG:--all}" WINEDLLOVERRIDES=mscoree,mshtml=
key='HKCU\Software\WineExplorer\Shell'
publisher='CN=WineExplorer U1 Oracle'
overall=0

note() { printf '%s\n' "$*" | tee -a "$results/summary.txt"; }
wine_run() { local log=$1; shift; timeout --kill-after=10s 180s "$build/wine" "$@" > "$log" 2>&1; }
expected() { grep -v -e '^#' -e '^$' "$1" | LC_ALL=C sort; }

# the archive of one package: the rendered manifest, the files, two images
make_package() {
    local manifest=$1 archive=$2 arch=$3 version=$4 name=$5 fwname=$6 fwversion=$7 minversion=$8; shift 8
    local stage; stage=$(mktemp -d)
    sed -e "s|@NAME@|$name|g" -e "s|@FWNAME@|$fwname|g" -e "s|@PUBLISHER@|$publisher|g" -e "s|@VERSION@|$version|g" \
        -e "s|@FWVERSION@|$fwversion|g" -e "s|@FWMINVERSION@|$minversion|g" -e "s|@ARCH@|$arch|g" "$manifest" > "$stage/AppxManifest.xml"
    cp "$@" "$stage/"
    head -c 100 /dev/zero > "$stage/logo.png"; head -c 100 /dev/zero > "$stage/small.png"
    (cd "$stage" && python3 -c 'import sys,zipfile,os
z=zipfile.ZipFile(sys.argv[1],"w",zipfile.ZIP_DEFLATED)
for f in sorted(os.listdir(".")):
    z.write(f,f)
z.close()' "$archive")
    rm -rf "$stage"
}

for arch in x64 x86; do
    out="$results/$arch"
    mkdir -- "$out"
    export WINEPREFIX="$out/prefix"
    mkdir -- "$WINEPREFIX"
    if ! wine_run "$out/wineboot.log" wineboot -i; then note "$arch FAIL wineboot"; overall=1; continue; fi
    wine_run "$out/reg.log" reg add "$key" /v UwpCompatibility /t REG_DWORD /d 1 /f
    mkdir -p "$WINEPREFIX/drive_c/u1out"
    store="$WINEPREFIX/drive_c/users/$USER/AppData/Local/WineExplorer/WindowsApps"
    catalog="$WINEPREFIX/appx-catalog"

    make_package "$u13/FrameworkManifest.xml" "$out/framework.appx" "$arch" 1.0.0.0 WineExplorer.U1Dependent WineExplorer.U1Framework 1.2.0.0 1.0.0.0 "$u13/$arch/u1fw.dll"
    make_package "$u13/AppManifest.xml" "$out/app.appx" "$arch" 1.0.0.0 WineExplorer.U1Dependent WineExplorer.U1Framework 1.2.0.0 1.0.0.0 "$u13/$arch/depapp.exe"
    make_package "$u13/AppManifest.xml" "$out/app-too-new.appx" "$arch" 1.0.0.0 WineExplorer.U1Dependent WineExplorer.U1Framework 1.2.0.0 2.0.0.0 "$u13/$arch/depapp.exe"

    # without the framework: refused, nothing left behind
    status=0
    wine_run "$out/app-no-framework.log" wexappx install --development "Z:$out/app.appx" || status=$?
    if [ "$status" != 0 ] && grep -q 'not installed' "$out/app-no-framework.log" && [ -z "$(ls "$catalog" 2>/dev/null)" ]; then
        note "$arch PASS the dependent package is refused without its framework"
    else note "$arch FAIL the dependent package without its framework (exit $status)"; overall=1; fi

    wine_run "$out/framework-install.log" wexappx install --development "Z:$out/framework.appx" || { note "$arch FAIL framework install"; overall=1; continue; }
    fw_full=$(tr -d '\r' < "$out/framework-install.log" | sed -n 's/^Installed //p')
    # a framework that is too old
    status=0
    wine_run "$out/app-too-new.log" wexappx install --development "Z:$out/app-too-new.appx" || status=$?
    if [ "$status" != 0 ] && grep -q 'not installed' "$out/app-too-new.log"; then note "$arch PASS a framework below the minimum version does not satisfy the dependency"
    else note "$arch FAIL minimum version of the dependency (exit $status)"; overall=1; fi

    wine_run "$out/app-install.log" wexappx install --development "Z:$out/app.appx" || { note "$arch FAIL app install"; overall=1; continue; }
    full=$(tr -d '\r' < "$out/app-install.log" | sed -n 's/^Installed //p')
    family=$(tr -d '\r' < "$out/app-install.log" | sed -n 's/^Application: \(.*\)!Dep$/\1/p')
    if ! grep -q "dependency.$fw_full" <(iconv -f UTF-16LE -t UTF-8 "$catalog/$full.wexappx" | tr '\t' '.'); then
        note "$arch FAIL the catalog record names the framework"; overall=1
    fi
    output="$WINEPREFIX/drive_c/u1out/dep.txt"
    rm -f "$output"
    wine_run "$out/launch.log" "$u10/$arch/launcher.exe" "$family!Dep" positive 'C:\u1out\dep.txt' "$full" || true
    sleep 2
    if [ ! -f "$output" ]; then note "$arch FAIL the dependent program produced no output"; overall=1
    else
        cp "$output" "$out/dep.txt"
        tr -d '\r' < "$out/dep.txt" | grep -E '^(PASS|FAIL) ' | LC_ALL=C sort > "$out/dep-checks.txt" || true
        expected "$u13/expected.txt" > "$out/expected.txt"
        if diff -u "$out/expected.txt" "$out/dep-checks.txt" > "$out/dep.diff"; then note "$arch PASS the dependent program (all $(wc -l < "$out/expected.txt") checks)"
        else note "$arch FAIL the dependent program"; head -20 "$out/dep.diff" >> "$results/summary.txt"; overall=1; fi
        grep -E '^INFO' "$out/dep.txt" | tr -d '\r' | sed "s/^/$arch /" >> "$results/observations.txt" || true
    fi

    wine_run "$out/app-uninstall.log" wexappx uninstall "$full" || { note "$arch FAIL app uninstall"; overall=1; }
    wine_run "$out/framework-uninstall.log" wexappx uninstall "$fw_full" || { note "$arch FAIL framework uninstall"; overall=1; }
    if [ -z "$(ls "$catalog" 2>/dev/null)" ] && [ -z "$(ls "$store" 2>/dev/null)" ]; then note "$arch PASS both packages uninstall and leave nothing"
    else note "$arch FAIL leftovers after uninstall"; overall=1; fi

    killed=0; waited=0
    timeout --kill-after=5s 30s "$WINESERVER" -k > /dev/null 2>&1 || killed=$?
    timeout --kill-after=5s 30s "$WINESERVER" -w > /dev/null 2>&1 || waited=$?
    note "$arch cleanup kill=$killed wait=$waited"
done
note "overall=$overall"
exit "$overall"
