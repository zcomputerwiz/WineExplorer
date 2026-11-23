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
    # an X socket folder of this run's own (a shared one let two runs at once meet on display :97)
    XDIR=$(mktemp -d /var/tmp/wex-wam-x.XXXXXX) && chmod 1777 "$XDIR" || exit 1
    exec env WEX_WAM_NS=1 WEX_WAM_XDIR="$XDIR" unshare --mount --propagation private sh -c \
        "mount --bind '$XDIR' /tmp/.X11-unix && exec bash '$0' '$BUILD'"
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
    # a failed run keeps its logs (the prefix is left out: it is large and made again by the next run)
    if [ "$FAILS" != 0 ] || [ -n "${WEX_WAM_KEEP:-}" ]; then
        rm -rf /var/tmp/wex-wam-last; mkdir -p /var/tmp/wex-wam-last
        cp "$WORK"/*.log /var/tmp/wex-wam-last/ 2>/dev/null
        echo "logs kept in /var/tmp/wex-wam-last"
    fi
    rm -rf "$WORK"
    [ -n "${WEX_WAM_XDIR:-}" ] && rm -rf "$WEX_WAM_XDIR"
}
trap cleanup EXIT

Xvfb :97 -screen 0 1024x768x24 -nolisten tcp > "$WORK/xvfb.log" 2>&1 &
X_PID=$!
for _ in $(seq 100); do [ -S /tmp/.X11-unix/X97 ] && break; sleep 0.1; done
[ -S /tmp/.X11-unix/X97 ] || { fail "Xvfb did not start"; cat "$WORK/xvfb.log"; exit 1; }
python3 "$HERE/test_authority.py" "$WORK/port" 2> "$WORK/authority.log" &
AUTH_PID=$!
for _ in $(seq 50); do [ -s "$WORK/port" ] && break; sleep 0.1; done
[ -s "$WORK/port" ] || { echo "FAIL the test authority did not start"; exit 1; }
PORT=$(cat "$WORK/port")

timeout 300 "$BUILD/wine" wineboot -i > "$WORK/wineboot.log" 2>&1 || { fail wineboot; tail -5 "$WORK/wineboot.log"; exit 1; }
# test_broker sets Broker and TestAuthority for its own run (the other tests check the path without a broker)
export WEBCORE_TEST_AUTHORITY="http://127.0.0.1:$PORT/"

CACHE=$HOME/.local/share/wineexplorer/tokenbroker/cache
check_cache()   # arch
{
    if [ -f "$CACHE" ]; then
        mode=$(stat -c %a "$CACHE")
        dmode=$(stat -c %a "$(dirname "$CACHE")")
        # Wine's server gives a user's ACE to the user and group bits (sd_to_mode); others get nothing (B72)
        case $mode in 600|640|660) pass "$1: cache file mode $mode, nothing for others" ;; *) fail "$1: cache file mode $mode" ;; esac
        case $dmode in 700|750|770) pass "$1: cache folder mode $dmode, nothing for others" ;; *) fail "$1: cache folder mode $dmode" ;; esac
        # the test refuses the refresh token of ok@wine.test (oid-ok): its account and tokens of every client id are
        # gone, while the accounts signed in afterwards (msa, org) are cached
        grep -q 'oid-ok' "$CACHE" && fail "$1: the cache still holds the signed-out account's tokens" ||
            pass "$1: cache emptied of the account by the refused refresh"
    else
        fail "$1: no cache file"
    fi
}

T=dlls/windows.security.authentication.web.core/tests
for arch in x86_64 i386; do
    exe=$BUILD/$T/$arch-windows/windows.security.authentication.web.core_test.exe
    [ -f "$exe" ] || { fail "web_core $arch: no test program"; continue; }
    # each architecture starts from no accounts (the account is machine-wide, and the broker keeps its cache)
    "$BUILD/server/wineserver" -k 2>/dev/null
    rm -rf "$HOME/.local/share/wineexplorer/tokenbroker"
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
    check_cache "$arch"   # before the next architecture removes it (the 64-bit cache was never checked)
done
grep -c '"POST /' "$WORK/authority.log" > /dev/null
echo "authority: $(curl -s "http://127.0.0.1:$PORT/stats" 2>/dev/null)"
[ $FAILS = 0 ] && echo "ALL PASS" || echo "FAILED $FAILS"
exit $FAILS
