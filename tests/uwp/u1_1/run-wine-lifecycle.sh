#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# U1.1 on Wine: the MSVC-built view probe, registered in a fresh prefix and activated by its application user model
# id through the U1.0 launcher, for both architectures; the unpackaged control run directly.
#
#   run-wine-lifecycle.sh WINE_BUILD_DIR U1_0_DIR U1_1_DIR RESULTS_DIR
#
# U1_0_DIR holds register-wine.py and x64/ x86/ with launcher.exe; U1_1_DIR holds AppxManifest.xml, expected*.txt
# and x64/ x86/ with view.exe. RESULTS_DIR must not exist; each architecture gets its own fresh prefix.
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

for arch in x64 x86; do
    out="$results/$arch"
    mkdir -- "$out"
    export WINEPREFIX="$out/prefix"
    mkdir -- "$WINEPREFIX"
    if ! wine_run "$out/wineboot.log" wineboot -i; then note "$arch FAIL wineboot"; overall=1; continue; fi
    mkdir -- "$WINEPREFIX/drive_c/u1out"
    wine_run "$out/reg.log" reg add "$key" /v UwpCompatibility /t REG_DWORD /d 1 /f

    # the unpackaged control
    status=0
    wine_run "$out/unpackaged.log" "$u11/$arch/view.exe" unpackaged 'C:\u1out\unpackaged.txt' none || status=$?
    tr -d '\r' < "$WINEPREFIX/drive_c/u1out/unpackaged.txt" > "$out/unpackaged.txt" 2> /dev/null || true
    grep -v -e '^#' -e '^$' "$u11/expected-unpackaged.txt" > "$out/expected-unpackaged.txt"
    grep -E '^(PASS|FAIL)' "$out/unpackaged.txt" > "$out/unpackaged-checks.txt" || true
    if [ "$status" = 0 ] && diff -u "$out/expected-unpackaged.txt" "$out/unpackaged-checks.txt" > "$out/unpackaged.diff"; then
        note "$arch PASS unpackaged control ($(grep HRESULT "$out/unpackaged.txt" | tr -d '\r'))"
    else
        note "$arch FAIL unpackaged control (exit $status)"; overall=1
    fi

    # the packaged run
    python3 "$register" stage lifecycle "$arch" "$u11/$arch" "$out/stage" --name WineExplorer.U1View --manifest "$u11/AppxManifest.xml"
    names=$(python3 "$register" register "$WINEPREFIX" "$out/stage")
    IFS=$'\t' read -r full family root <<< "$names"
    status=0
    wine_run "$out/packaged.log" "$u10/$arch/launcher.exe" "$family!View" positive 'C:\u1out\view.txt' "$full" || status=$?
    python3 "$register" unregister "$WINEPREFIX" "$full"
    note "$arch launcher exit $status"
    if [ -f "$WINEPREFIX/drive_c/u1out/view.txt" ]; then
        tr -d '\r' < "$WINEPREFIX/drive_c/u1out/view.txt" > "$out/view.txt"
        grep -E '^(PASS|FAIL)' "$out/view.txt" > "$out/view-checks.txt" || true
        grep -v -e '^#' -e '^$' "$u11/expected.txt" > "$out/expected.txt"
        if [ "$status" = 0 ] && diff -u "$out/expected.txt" "$out/view-checks.txt" > "$out/view.diff"; then
            note "$arch PASS lifecycle (all $(wc -l < "$out/expected.txt") checks)"
        else
            note "$arch FAIL lifecycle"; head -40 "$out/view.diff" >> "$results/summary.txt"; overall=1
        fi
        grep '^INFO' "$out/view.txt" | sed "s/^/$arch /" >> "$results/info.txt"
    else
        note "$arch FAIL lifecycle (no output)"; overall=1
    fi

    killed=0; waited=0
    timeout --kill-after=5s 30s "$WINESERVER" -k > /dev/null 2>&1 || killed=$?
    timeout --kill-after=5s 30s "$WINESERVER" -w > /dev/null 2>&1 || waited=$?
    note "$arch cleanup kill=$killed wait=$waited"
    if (( (killed != 0 && killed != 1) || waited != 0 )); then overall=1; fi
done
note "overall=$overall"
exit "$overall"
