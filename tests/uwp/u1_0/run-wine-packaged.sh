#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# U1.0 packaged run on Wine: the unchanged MSVC-built fixture, registered in a fresh prefix and activated by
# its application user model id through launcher.exe (IApplicationActivationManager), for both architectures.
#
#   run-wine-packaged.sh WINE_BUILD_DIR FIXTURE_DIR RESULTS_DIR
#
# FIXTURE_DIR holds this directory's scripts and x64/ and x86/ with probe.exe, component.dll and launcher.exe.
# RESULTS_DIR must not exist. Each architecture gets its own fresh prefix under it; only that prefix's
# wineserver is stopped.
set -euo pipefail
build=$(readlink -f "$1")
fixture=$(readlink -f "$2")
results=$3
test -x "$build/wine" && test -x "$build/server/wineserver"
test ! -e /etc/wineexplorer
mkdir -- "$results"
results=$(readlink -f "$results")
register="$fixture/register-wine.py"
export WINESERVER="$build/server/wineserver"
export WINEDEBUG="${WINEDEBUG:--all}" WINEDLLOVERRIDES=mscoree,mshtml=
key='HKCU\Software\WineExplorer\Shell'
overall=0

note() { printf '%s\n' "$*" | tee -a "$results/summary.txt"; }

wine_run() {  # wine_run LOG command...: bounded, returns the exit code
    local log=$1; shift
    local status=0
    timeout --kill-after=10s 120s "$build/wine" "$@" > "$log" 2>&1 || status=$?
    return "$status"
}

stop_server() {
    local killed=0 waited=0
    timeout --kill-after=5s 30s "$WINESERVER" -k > /dev/null 2>&1 || killed=$?
    timeout --kill-after=5s 30s "$WINESERVER" -w > /dev/null 2>&1 || waited=$?
    note "$1 cleanup kill=$killed wait=$waited"
    if (( (killed != 0 && killed != 1) || waited != 0 )); then overall=1; fi
}

for arch in x64 x86; do
    out="$results/$arch"
    mkdir -- "$out"
    export WINEPREFIX="$out/prefix"
    mkdir -- "$WINEPREFIX"
    trap 'stop_server "$arch (interrupted)"' EXIT
    if ! wine_run "$out/wineboot.log" wineboot -i; then note "$arch FAIL wineboot"; overall=1; stop_server "$arch"; continue; fi
    mkdir -- "$WINEPREFIX/drive_c/u1out"
    bin="$fixture/$arch"
    actual="$out/actual.txt"
    : > "$actual"

    activate() {  # activate CASE: register, activate through the launcher, unregister; prints the exit code
        local case=$1 names full family root status=0
        python3 "$register" stage "$case" "$arch" "$bin" "$out/stage-$case"
        names=$(python3 "$register" register "$WINEPREFIX" "$out/stage-$case")
        IFS=$'\t' read -r full family root <<< "$names"
        wine_run "$out/$case.log" "$bin/launcher.exe" "$family!Probe" "$case" "C:\\u1out\\$case.txt" "$full" || status=$?
        python3 "$register" unregister "$WINEPREFIX" "$full"
        echo "$status"
    }

    # the gate is off by default: activation must fail and start nothing
    status=$(activate positive)
    if [ "$status" != 0 ] && [ ! -e "$WINEPREFIX/drive_c/u1out/positive.txt" ] &&
       grep -q 'ActivateApplication: 0x80070032' "$out/positive.log"; then
        note "$arch PASS gate_default_off"
    else
        note "$arch FAIL gate_default_off (exit $status)"; overall=1
    fi
    rm -rf "$out/stage-positive"; mv "$out/positive.log" "$out/gate-default.log"

    # wrong value and wrong type leave it off
    wine_run "$out/reg.log" reg add "$key" /v UwpCompatibility /t REG_DWORD /d 2 /f
    status=$(activate positive)
    if [ "$status" != 0 ] && [ ! -e "$WINEPREFIX/drive_c/u1out/positive.txt" ]; then note "$arch PASS gate_wrong_value"
    else note "$arch FAIL gate_wrong_value (exit $status)"; overall=1; fi
    rm -rf "$out/stage-positive"; mv "$out/positive.log" "$out/gate-value.log"
    wine_run "$out/reg.log" reg add "$key" /v UwpCompatibility /t REG_SZ /d 1 /f
    status=$(activate positive)
    if [ "$status" != 0 ] && [ ! -e "$WINEPREFIX/drive_c/u1out/positive.txt" ]; then note "$arch PASS gate_wrong_type"
    else note "$arch FAIL gate_wrong_type (exit $status)"; overall=1; fi
    rm -rf "$out/stage-positive"; mv "$out/positive.log" "$out/gate-type.log"

    wine_run "$out/reg.log" reg add "$key" /v UwpCompatibility /t REG_DWORD /d 1 /f

    for case in positive missing_class missing_dll unpackaged; do
        if [ "$case" = unpackaged ]; then
            status=0
            wine_run "$out/$case.log" "$bin/probe.exe" unpackaged 'C:\u1out\unpackaged.txt' unpackaged || status=$?
        else
            status=$(activate "$case")
        fi
        text="$WINEPREFIX/drive_c/u1out/$case.txt"
        if [ ! -f "$text" ]; then
            note "$arch FAIL $case (no output, exit $status)"; echo "FAIL $case" >> "$actual"; overall=1
            continue
        fi
        tr -d '\r' < "$text" > "$out/$case.txt"
        if [ "$case" = positive ]; then
            cat "$out/$case.txt" >> "$actual"
        elif [ "$status" = 0 ] && [ "$(wc -l < "$out/$case.txt")" = 2 ] &&
             grep -q -x 'PASS negative_activation' "$out/$case.txt" &&
             grep -q -E -x "HRESULT $case 0x[0-9A-F]{8}" "$out/$case.txt"; then
            echo "PASS $case" >> "$actual"
            grep -E "^HRESULT $case " "$out/$case.txt" >> "$out/hresults.txt"
        else
            echo "FAIL $case" >> "$actual"
        fi
    done
    grep -v -e '^#' -e '^$' "$fixture/expected.txt" > "$out/expected.txt"
    if diff -u "$out/expected.txt" "$actual" > "$out/diff.txt"; then note "$arch PASS expected.txt (all ten checks)"
    else note "$arch FAIL expected.txt"; cat "$out/diff.txt" >> "$results/summary.txt"; overall=1; fi
    # the HRESULTs observed on Windows 11
    touch "$out/hresults.txt"
    if diff -u "$fixture/observations.txt" "$out/hresults.txt" > "$out/diff-hresults.txt"; then
        note "$arch PASS observations.txt (HRESULTs match Windows)"
    else note "$arch FAIL observations.txt"; cat "$out/diff-hresults.txt" >> "$results/summary.txt"; overall=1; fi

    # the catalog is empty again: activation fails with "not registered"
    status=0
    wine_run "$out/unregistered.log" "$bin/launcher.exe" 'WineExplorer.U1Oracle.Probe_0000000000000!Probe' positive \
        'C:\u1out\never.txt' none || status=$?
    if [ "$status" != 0 ] && grep -q 'ActivateApplication: 0x80270254' "$out/unregistered.log"; then
        note "$arch PASS unregistered_application"
    else
        note "$arch FAIL unregistered_application (exit $status)"; overall=1
    fi

    # what the server itself refuses, without the manager and its gate (context-negatives.c)
    if [ -f "$bin/context-negatives.exe" ]; then
        catalog="$WINEPREFIX/appx-catalog"
        python3 "$register" stage positive "$arch" "$bin" "$out/stage-negatives"
        names=$(python3 "$register" register "$WINEPREFIX" "$out/stage-negatives")
        IFS=$'\t' read -r full family root <<< "$names"
        corrupt="$catalog/WineExplorer.Corrupt_1.0.0.0_${arch}__0000000000000.wexappx"
        printf 'WEXAPPX 1\nbogus line\n' | iconv -f ASCII -t UTF-16LE > "$corrupt"
        status=0
        wine_run "$out/negatives.log" "$bin/context-negatives.exe" "$family!Probe" "$full" \
            "$root\\probe.exe" 'C:\windows\system32\cmd.exe' \
            'WineExplorer.Corrupt_0000000000000!App' || status=$?
        rm -f -- "$corrupt"
        python3 "$register" unregister "$WINEPREFIX" "$full"
        tr -d '\r' < "$out/negatives.log" > "$out/negatives.txt"
        if [ "$status" = 0 ] && ! grep -q '^FAIL' "$out/negatives.txt" && grep -q '^PASS other_image_refused' "$out/negatives.txt"; then
            note "$arch PASS context negatives ($(grep -c '^PASS' "$out/negatives.txt") checks)"
        else
            note "$arch FAIL context negatives (exit $status)"; grep -E '^(FAIL|INFO)' "$out/negatives.txt" >> "$results/summary.txt" || true
            overall=1
        fi
    fi

    trap - EXIT
    stop_server "$arch"
done
note "overall=$overall"
exit "$overall"
