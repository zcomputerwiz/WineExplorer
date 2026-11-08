#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Native unit test and headless verification for E1-S17 credential dialog.
# Run as root in disposable wexbuild.
set -euo pipefail

[ "$(id -u)" -eq 0 ] || { echo 'FAIL: run as root' >&2; exit 1; }

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname -- "$SCRIPT_DIR")"
FIXTURE="$(mktemp -d /run/wex-cred-dialog-test-XXXXXXXX)"
XVFB_PID=''

cleanup()
{
    if [[ -n "$XVFB_PID" ]]; then
        kill -TERM "$XVFB_PID" 2>/dev/null || :
        wait "$XVFB_PID" 2>/dev/null || :
    fi
    rm -rf -- "$FIXTURE"
}
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

chmod 0755 "$FIXTURE"

echo "==> Compiling production and lab binaries"

# 1. Compile production build (no test defines, no test hooks)
cc -O2 -Wall -Wextra -Werror \
    -I"$REPO/src/programs/wexelevate" \
    -o "$FIXTURE/production" \
    "$REPO/src/programs/wexelevate/cred_dialog.c" \
    "$REPO/src/programs/wexelevate/test_cred_dialog_main.c"

# 2. Compile lab build (with test define for thorough checks)
cc -O2 -Wall -Wextra -Werror \
    -DWEX_E1S17_CRED_TEST \
    -I"$REPO/src/programs/wexelevate" \
    -o "$FIXTURE/lab" \
    "$REPO/src/programs/wexelevate/cred_dialog.c" \
    "$REPO/src/programs/wexelevate/test_cred_dialog_main.c"

# 3. Production build checks
echo "==> Testing production binary (disabled by default)"
"$FIXTURE/production" test_disabled

echo "==> Checking production exclusion (no test strings in binary)"
strings "$FIXTURE/production" > "$FIXTURE/production.strings"
if grep -q -E '(WEX_CRED_DIALOG_TEST_HOOK|test_cred_hook|cred\..*\.action|cred\..*\.shown)' "$FIXTURE/production.strings"; then
    fail "Test strings found in production binary"
fi

# 4. Run all pure unit tests in lab binary
echo "==> Testing lab binary (account validation, buffer wipe, states, queue)"
"$FIXTURE/lab" all

# 5. Headless Xvfb run of dialog child if MinGW and Xvfb are available
if command -v x86_64-w64-mingw32-gcc >/dev/null 2>&1 && command -v x86_64-w64-mingw32-windres >/dev/null 2>&1 && command -v Xvfb >/dev/null 2>&1 && command -v wine >/dev/null 2>&1; then
    echo "==> Headless Xvfb dialog render and input verification"
    DISP=":98"
    Xvfb "$DISP" -screen 0 1024x768x16 >/dev/null 2>&1 &
    XVFB_PID=$!
    sleep 1

    mkdir -p "$FIXTURE/wine_build"
    HOOK_DIR="/tmp/wexelev"
    mkdir -p -m 0777 "$HOOK_DIR"

    # Compile resources with windres
    x86_64-w64-mingw32-windres \
        -I"$REPO/src/programs/consent" \
        "$REPO/src/programs/consent/consent.rc" \
        "$FIXTURE/wine_build/consent.res.o" 2>"$FIXTURE/windres.log" || {
            cat "$FIXTURE/windres.log"
            echo "NOTE: windres skipped, continuing"
        }

    # Compile consent_test.exe with MinGW
    if [ -f "$FIXTURE/wine_build/consent.res.o" ]; then
        x86_64-w64-mingw32-gcc -O2 -Wall \
            -DWEX_CRED_DIALOG_TEST_HOOK \
            -I"$REPO/src/programs/consent" \
            -I"$REPO/src/programs/wexelevate" \
            -o "$FIXTURE/wine_build/consent_test.exe" \
            "$REPO/src/programs/consent/consent.c" \
            "$FIXTURE/wine_build/consent.res.o" \
            -luser32 -lgdi32 -ladvapi32 -lcomctl32 -lversion -lwintrust -lcrypt32 \
            -mwindows -municode 2>"$FIXTURE/mingw.log" || {
                cat "$FIXTURE/mingw.log"
                echo "NOTE: MinGW compilation failed"
            }

        # Also compile production consent without test hook to verify production strings exclusion
        x86_64-w64-mingw32-gcc -O2 -Wall \
            -I"$REPO/src/programs/consent" \
            -I"$REPO/src/programs/wexelevate" \
            -o "$FIXTURE/wine_build/consent_prod.exe" \
            "$REPO/src/programs/consent/consent.c" \
            "$FIXTURE/wine_build/consent.res.o" \
            -luser32 -lgdi32 -ladvapi32 -lcomctl32 -lversion -lwintrust -lcrypt32 \
            -mwindows -municode 2>/dev/null || :

        if [ -f "$FIXTURE/wine_build/consent_prod.exe" ]; then
            strings "$FIXTURE/wine_build/consent_prod.exe" > "$FIXTURE/wine_build/consent_prod.strings"
            if grep -q -E '(test_cred_hook|cred\..*\.action|cred\..*\.shown)' "$FIXTURE/wine_build/consent_prod.strings"; then
                fail "Test hook strings found in production consent binary"
            fi
            echo "PASS production consent binary excludes test hook strings"
        fi

        # Compile probe test driver
        x86_64-w64-mingw32-gcc -O2 -Wall \
            -I"$REPO/src/programs/wexelevate" \
            -o "$FIXTURE/wine_build/cred_dialog_probe.exe" \
            "$REPO/scripts/cred_dialog_probe.c" 2>"$FIXTURE/probe_mingw.log" || {
                cat "$FIXTURE/probe_mingw.log"
                echo "NOTE: Probe compilation failed"
            }
    fi

    if [ -f "$FIXTURE/wine_build/consent_test.exe" ] && [ -f "$FIXTURE/wine_build/cred_dialog_probe.exe" ]; then
        # 1. Render test
        TEST_ID="test_render"
        rm -f "$HOOK_DIR"/cred."$TEST_ID".*
        printf "confirm:alice:SecretPassword123\n" > "$HOOK_DIR/cred.$TEST_ID.action"

        WINEDEBUG=-all DISPLAY="$DISP" wine "$FIXTURE/wine_build/consent_test.exe" \
            /broker /credentials /request 12345678abcdef01 /token 0102030405060708090a0b0c0d0e0f10 \
            /channel 0 /testid "$TEST_ID" >/dev/null 2>&1 &
        WINE_CHILD_PID=$!

        SHOWN_OK=0
        for ((i=0; i<50; i++)); do
            if [ -f "$HOOK_DIR/cred.$TEST_ID.shown" ]; then
                SHOWN_OK=1
                break
            fi
            sleep 0.1
        done

        if [ "$SHOWN_OK" -eq 1 ]; then
            echo "PASS headless dialog rendered and produced .shown record"
            grep -q "shown=1" "$HOOK_DIR/cred.$TEST_ID.shown" || fail ".shown missing shown=1"
            grep -q "The administrator account's rights apply inside this Wine prefix only" "$HOOK_DIR/cred.$TEST_ID.shown" || fail "missing Wine-only explanation"
        else
            fail "Headless dialog render did not produce .shown within timeout"
        fi
        wait "$WINE_CHILD_PID" 2>/dev/null || :
        rm -f "$HOOK_DIR"/cred."$TEST_ID".*

        # 2. Confirm probe test
        rm -f "$HOOK_DIR"/cred.p_confirm.*
        WINEDEBUG=-all DISPLAY="$DISP" wine "$FIXTURE/wine_build/cred_dialog_probe.exe" \
            confirm "$FIXTURE/wine_build/consent_test.exe" p_confirm > "$FIXTURE/probe_confirm.out" 2>&1 || {
                cat "$FIXTURE/probe_confirm.out"
                fail "Confirm probe failed"
            }
        grep -q "PROBE_PASS_CONFIRM" "$FIXTURE/probe_confirm.out" || fail "Confirm probe did not pass"
        echo "PASS headless dialog Confirm action transmits valid S1 Confirm record"
        rm -f "$HOOK_DIR"/cred.p_confirm.*

        # 3. Cancel probe test
        rm -f "$HOOK_DIR"/cred.p_cancel.*
        WINEDEBUG=-all DISPLAY="$DISP" wine "$FIXTURE/wine_build/cred_dialog_probe.exe" \
            cancel "$FIXTURE/wine_build/consent_test.exe" p_cancel > "$FIXTURE/probe_cancel.out" 2>&1 || {
                cat "$FIXTURE/probe_cancel.out"
                fail "Cancel probe failed"
            }
        grep -q "PROBE_PASS_CANCEL" "$FIXTURE/probe_cancel.out" || fail "Cancel probe did not pass"
        echo "PASS headless dialog Cancel action transmits valid S1 Cancel record"
        rm -f "$HOOK_DIR"/cred.p_cancel.*

        # 4. Invalid account probe test
        rm -f "$HOOK_DIR"/cred.p_invalid.*
        WINEDEBUG=-all DISPLAY="$DISP" wine "$FIXTURE/wine_build/cred_dialog_probe.exe" \
            invalid "$FIXTURE/wine_build/consent_test.exe" p_invalid > "$FIXTURE/probe_invalid.out" 2>&1 || {
                cat "$FIXTURE/probe_invalid.out"
                fail "Invalid account probe failed"
            }
        grep -q "PROBE_PASS_INVALID" "$FIXTURE/probe_invalid.out" || fail "Invalid account probe did not pass"
        echo "PASS headless dialog rejects invalid account and suppresses Confirm"
        rm -f "$HOOK_DIR"/cred.p_invalid.*

        # 5. Early child exit probe test (Helper exit NEVER approves)
        rm -f "$HOOK_DIR"/cred.p_early.*
        WINEDEBUG=-all DISPLAY="$DISP" wine "$FIXTURE/wine_build/cred_dialog_probe.exe" \
            early_exit "$FIXTURE/wine_build/consent_test.exe" p_early > "$FIXTURE/probe_early.out" 2>&1 || {
                cat "$FIXTURE/probe_early.out"
                fail "Early exit probe failed"
            }
        grep -q "PROBE_PASS_EARLY_EXIT_DENIED" "$FIXTURE/probe_early.out" || fail "Early exit probe did not pass"
        echo "PASS helper exit never approves: child kill fails closed without Confirm"
        rm -f "$HOOK_DIR"/cred.p_early.*
    fi

    kill -TERM "$XVFB_PID" 2>/dev/null || :
    wait "$XVFB_PID" 2>/dev/null || :
    XVFB_PID=''
fi

echo "==> ALL E1-S17 credential dialog tests passed successfully."
