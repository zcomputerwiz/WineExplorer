#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# U1.1 on Wine: the MSVC-built UWP view probe, registered in a fresh prefix and activated by its application user model
# ids through the U1.0 launcher, for both architectures; the unpackaged control run directly.
#
#   run-wine-lifecycle.sh WINE_BUILD_DIR U1_0_DIR U1_1_DIR RESULTS_DIR
#
# U1_0_DIR holds register-wine.py and x64/ x86/ with launcher.exe; U1_1_DIR holds AppxManifest.xml, expected-*.txt
# and x64/ x86/ with view.exe. RESULTS_DIR must not exist; each architecture gets its own fresh prefix.
# The probe writes %LOCALAPPDATA%\view-MODE.txt; its PASS and FAIL lines are compared, sorted, with expected-MODE.txt.
set -euo pipefail
build=$(readlink -f "$1")
u10=$(readlink -f "$2")
u11=$(readlink -f "$3")
results=$4
test -x "$build/wine" && test -x "$build/server/wineserver"
test ! -e /etc/wineexplorer
mkdir -- "$results"
results=$(readlink -f "$results")
register="$u10/register-wine.py"
export WINESERVER="$build/server/wineserver"
export WINEDEBUG="${WINEDEBUG:--all}" WINEDLLOVERRIDES=mscoree,mshtml=
key='HKCU\Software\WineExplorer\Shell'
overall=0

note() { printf '%s\n' "$*" | tee -a "$results/summary.txt"; }
wine_run() { local log=$1; shift; timeout --kill-after=10s 180s "$build/wine" "$@" > "$log" 2>&1; }
checks() { tr -d '\r' < "$1" | grep -E '^(PASS|FAIL) ' | LC_ALL=C sort; }
expected() { grep -v -e '^#' -e '^$' "$1" | LC_ALL=C sort; }

for arch in x64 x86; do
    out="$results/$arch"
    mkdir -- "$out"
    export WINEPREFIX="$out/prefix"
    mkdir -- "$WINEPREFIX"
    if ! wine_run "$out/wineboot.log" wineboot -i; then note "$arch FAIL wineboot"; overall=1; continue; fi
    appdata="$WINEPREFIX/drive_c/users/$USER/AppData/Local"
    mkdir -p "$appdata" "$WINEPREFIX/drive_c/u1out"
    wine_run "$out/reg.log" reg add "$key" /v UwpCompatibility /t REG_DWORD /d 1 /f

    # the unpackaged control (Windows ends such a process with a fast fail, so it is only run here)
    status=0
    wine_run "$out/unpackaged.log" "$u11/$arch/view.exe" unpackaged 'C:\u1out\unpackaged.txt' none || status=$?
    checks "$WINEPREFIX/drive_c/u1out/unpackaged.txt" > "$out/unpackaged-checks.txt" || true
    expected "$u11/expected-unpackaged.txt" > "$out/expected-unpackaged.txt"
    if [ "$status" = 0 ] && diff -u "$out/expected-unpackaged.txt" "$out/unpackaged-checks.txt" > "$out/unpackaged.diff"; then
        note "$arch PASS unpackaged control ($(tr -d '\r' < "$WINEPREFIX/drive_c/u1out/unpackaged.txt" | grep HRESULT))"
    else
        note "$arch FAIL unpackaged control (exit $status)"; overall=1
    fi

    # the packaged applications
    python3 "$register" stage lifecycle "$arch" "$u11/$arch" "$out/stage" --name WineExplorer.U1View --manifest "$u11/AppxManifest.xml"
    names=$(python3 "$register" register "$WINEPREFIX" "$out/stage")
    IFS=$'\t' read -r full family root <<< "$names"
    for spec in View:positive ViewReturn:return ViewSta:sta; do
        app=${spec%%:*}; mode=${spec#*:}
        file="$appdata/view-$mode.txt"
        rm -f "$file"
        status=0
        wine_run "$out/$mode.log" "$u10/$arch/launcher.exe" "$family!$app" "$mode" unused "$full" || status=$?
        if [ ! -f "$file" ]; then note "$arch FAIL $mode (no output, launcher exit $status)"; overall=1; continue; fi
        cp "$file" "$out/$mode.txt"
        checks "$out/$mode.txt" > "$out/$mode-checks.txt" || true
        expected "$u11/expected-$mode.txt" > "$out/expected-$mode.txt"
        if diff -u "$out/expected-$mode.txt" "$out/$mode-checks.txt" > "$out/$mode.diff"; then
            note "$arch PASS $mode (all $(wc -l < "$out/expected-$mode.txt") checks, launcher exit $status)"
        else
            note "$arch FAIL $mode (launcher exit $status)"; head -30 "$out/$mode.diff" >> "$results/summary.txt"; overall=1
        fi
        grep -E '^(INFO|TRACE)' "$out/$mode.txt" | tr -d '\r' | sed "s/^/$arch $mode /" >> "$results/observations.txt" || true
    done
    python3 "$register" unregister "$WINEPREFIX" "$full"

    killed=0; waited=0
    timeout --kill-after=5s 30s "$WINESERVER" -k > /dev/null 2>&1 || killed=$?
    timeout --kill-after=5s 30s "$WINESERVER" -w > /dev/null 2>&1 || waited=$?
    note "$arch cleanup kill=$killed wait=$waited"
    if (( (killed != 0 && killed != 1) || waited != 0 )); then overall=1; fi
done
note "overall=$overall"
exit "$overall"
