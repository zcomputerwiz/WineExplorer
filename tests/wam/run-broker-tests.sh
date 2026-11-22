#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# run-broker-tests.sh [BUILD_DIR] - the token broker against the local test authority (docs/WAM_BROKER_PLAN.md,
# steps 2 and 3): Wine's web_core conformance tests, 64-bit and 32-bit, with the broker and its test provider on, in a
# prefix and home of their own (the broker's cache goes to $HOME/.local/share/wineexplorer/tokenbroker), then the
# cache file's mode. As root in a build machine (wexbuild); BUILD_DIR is a WoW64 build tree with WineExplorer's
# tokenbroker assembled into it (default /root/WE-wamprov/build/wow64). Prints PASS or FAIL lines and "ALL PASS".
#
# WSLg mounts /tmp/.X11-unix read-only, so Xvfb runs in a private mount namespace with an owned socket directory.
set -u
BUILD=${1:-/root/WE-wamprov/build/wow64}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
if [ -z "${WEX_WAM_NS:-}" ]; then
    install -d -m 1777 /var/tmp/wex-wam-x
    exec env WEX_WAM_NS=1 unshare --mount --propagation private sh -c \
        "mount --bind /var/tmp/wex-wam-x /tmp/.X11-unix && exec bash '$0' '$BUILD'"
fi

WORK=$(mktemp -d /var/tmp/wex-wam-test.XXXXXX)
export WINEPREFIX=$WORK/prefix HOME=$WORK/home WINEDEBUG=${WINEDEBUG:--all} WINEDLLOVERRIDES="mscoree,mshtml="
export DISPLAY=:97
mkdir -p "$HOME"
FAILS=0
pass() { echo "PASS $1"; }
fail() { echo "FAIL $1"; FAILS=$((FAILS + 1)); }
cleanup()
{
    "$BUILD/server/wineserver" -k 2>/dev/null
    [ -n "${AUTH_PID:-}" ] && kill "$AUTH_PID" 2>/dev/null
    [ -n "${X_PID:-}" ] && kill "$X_PID" 2>/dev/null
    rm -rf "$WORK"
}
trap cleanup EXIT

Xvfb :97 -screen 0 1024x768x24 -nolisten tcp > "$WORK/xvfb.log" 2>&1 &
X_PID=$!
python3 "$HERE/test_authority.py" "$WORK/port" 2> "$WORK/authority.log" &
AUTH_PID=$!
for _ in $(seq 50); do [ -s "$WORK/port" ] && break; sleep 0.1; done
[ -s "$WORK/port" ] || { echo "FAIL the test authority did not start"; exit 1; }
PORT=$(cat "$WORK/port")

timeout 300 "$BUILD/wine" wineboot -i > "$WORK/wineboot.log" 2>&1 || { fail wineboot; tail -5 "$WORK/wineboot.log"; exit 1; }
# test_broker sets Broker and TestAuthority for its own run (the other tests check the path without a broker)
export WEBCORE_TEST_AUTHORITY="http://127.0.0.1:$PORT/"

T=dlls/windows.security.authentication.web.core/tests
for arch in x86_64 i386; do
    exe=$BUILD/$T/$arch-windows/windows.security.authentication.web.core_test.exe
    [ -f "$exe" ] || { fail "web_core $arch: no test program"; continue; }
    timeout 300 "$BUILD/wine" "$exe" web_core > "$WORK/web_core-$arch.log" 2>&1
    rc=$?
    summary=$(grep -E 'tests executed' "$WORK/web_core-$arch.log" | tail -1)
    if [ $rc = 0 ] && ! grep -q 'Test failed' "$WORK/web_core-$arch.log" && [ -n "$summary" ]; then
        pass "web_core $arch: $summary"
    else
        fail "web_core $arch (exit $rc): $summary"
        grep -E 'Test failed|Test succeeded inside todo|unhandled' "$WORK/web_core-$arch.log" | head -20
    fi
    grep -q 'skipped: the token broker' "$WORK/web_core-$arch.log" && fail "web_core $arch: the broker tests were skipped"
done

CACHE=$HOME/.local/share/wineexplorer/tokenbroker/cache
if [ -f "$CACHE" ]; then
    mode=$(stat -c %a "$CACHE")
    dmode=$(stat -c %a "$(dirname "$CACHE")")
    # Wine's server gives a user's ACE to the user and group bits (sd_to_mode); others get nothing (B72)
    case $mode in 600|640|660) pass "cache file mode $mode, nothing for others" ;; *) fail "cache file mode $mode" ;; esac
    case $dmode in 700|750|770) pass "cache folder mode $dmode, nothing for others" ;; *) fail "cache folder mode $dmode" ;; esac
    grep -q 'at-' "$CACHE" && fail "the cache still holds the signed-out account's tokens" || pass "cache emptied by the refused refresh"
else
    fail "no cache file"
fi
grep -c '"POST /' "$WORK/authority.log" > /dev/null
echo "authority: $(curl -s "http://127.0.0.1:$PORT/stats" 2>/dev/null)"
[ "${WEX_WAM_KEEP:-}" ] && { cp -r "$WORK" /var/tmp/wex-wam-last; echo "kept in /var/tmp/wex-wam-last"; }
[ $FAILS = 0 ] && echo "ALL PASS" || echo "FAILED $FAILS"
exit $FAILS
