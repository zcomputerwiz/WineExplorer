#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Test harness proving test runner isolation and sentinel preservation (R7)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CHECK_RUNNER="$SCRIPT_DIR/check-token-security.sh"

echo "=== R7 Runner Isolation & Sentinel Preservation Test ==="

SENTINEL_DIR="$(mktemp -d /tmp/wex-sentinel.XXXXXX)"
SENTINEL_LOG="$SENTINEL_DIR/sentinel.log"
SENTINEL_HEARTBEAT="$SENTINEL_DIR/heartbeat"

cleanup_sentinel() {
    echo "Cleaning up sentinel fixture..."
    if [ -n "${SENTINEL_PID:-}" ] && kill -0 "$SENTINEL_PID" 2>/dev/null; then
        kill "$SENTINEL_PID" 2>/dev/null || true
    fi
    if [ -n "${HEARTBEAT_PID:-}" ] && kill -0 "$HEARTBEAT_PID" 2>/dev/null; then
        kill "$HEARTBEAT_PID" 2>/dev/null || true
    fi
    rm -rf "$SENTINEL_DIR"
}
trap cleanup_sentinel EXIT INT TERM

# Start a background sentinel process outside runner ownership
# A long-running sleep with unique arguments
sleep 3600 </dev/null >/dev/null 2>&1 &
SENTINEL_PID=$!
SENTINEL_START="$(ps -p "$SENTINEL_PID" -o lstart= 2>/dev/null || true)"

# Background heartbeat writer
(
    while true; do
        date +%s > "$SENTINEL_HEARTBEAT"
        sleep 0.2
    done
) </dev/null >/dev/null 2>&1 &
HEARTBEAT_PID=$!

echo "Sentinel started: PID=$SENTINEL_PID, Start='$SENTINEL_START'"

assert_sentinel_alive() {
    local phase="$1"
    if ! kill -0 "$SENTINEL_PID" 2>/dev/null; then
        echo "FAIL [$phase]: Sentinel PID $SENTINEL_PID was terminated!" >&2
        exit 1
    fi
    local cur_start
    cur_start="$(ps -p "$SENTINEL_PID" -o lstart= 2>/dev/null || true)"
    if [ "$cur_start" != "$SENTINEL_START" ]; then
        echo "FAIL [$phase]: Sentinel was restarted! (was '$SENTINEL_START', now '$cur_start')" >&2
        exit 1
    fi
    if [ ! -f "$SENTINEL_HEARTBEAT" ]; then
        echo "FAIL [$phase]: Sentinel heartbeat missing!" >&2
        exit 1
    fi
    local last_hb cur_time
    last_hb="$(cat "$SENTINEL_HEARTBEAT")"
    cur_time="$(date +%s)"
    if [ $((cur_time - last_hb)) -gt 3 ]; then
        echo "FAIL [$phase]: Sentinel heartbeat stalled ($((cur_time - last_hb))s ago)!" >&2
        exit 1
    fi
    echo "PASS [$phase]: Sentinel PID $SENTINEL_PID alive, identity unchanged, heartbeat active."
}

# 1. Test normal execution of runner
echo "--- Scenario 1: Normal Runner Execution ---"
"$CHECK_RUNNER" --dry-run
assert_sentinel_alive "Normal Exit"

# 2. Test early failure of runner
echo "--- Scenario 2: Early Failure in Runner ---"
set +e
"$CHECK_RUNNER" --simulate-failure
RUNNER_EXIT=$?
set -e
if [ "$RUNNER_EXIT" -eq 0 ]; then
    echo "Expected failure exit code, got 0" >&2
    exit 1
fi
assert_sentinel_alive "Early Failure"

# 3. Test runner timeout cancellation
echo "--- Scenario 3: Runner Timeout Cancellation ---"
set +e
timeout 2s "$CHECK_RUNNER" --simulate-hang
set -e
assert_sentinel_alive "Timeout Cancellation"

# 4. Test signal cancellation (SIGTERM)
echo "--- Scenario 4: Runner Signal Cancellation (SIGTERM) ---"
"$CHECK_RUNNER" --simulate-hang &
TARGET_PID=$!
sleep 1
kill -TERM "$TARGET_PID" 2>/dev/null || true
wait "$TARGET_PID" 2>/dev/null || true
assert_sentinel_alive "Signal Cancellation"

echo "=== All Runner Isolation Scenarios PASSED ==="
