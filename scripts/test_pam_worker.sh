#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Qwen: ROOT IN wexbuild OR A THROWAWAY VM ONLY. NEVER shared lab guest VM B.
# Does not install PAM services or touch S2 accounts/configuration.
set +x
set -euo pipefail
finish() { status=$?; if [ "$status" -eq 0 ]; then echo 'E1S7_PAM_WORKER OK'; else echo 'E1S7_PAM_WORKER FAIL'; fi; }
trap finish EXIT
[ "$(id -u)" -eq 0 ] || { echo 'FAIL: root required' >&2; exit 1; }
[ "$#" -eq 2 ] && [ "$1" = --disposable-guest ] || {
    echo 'usage: bash scripts/test_pam_worker.sh --disposable-guest wex-e1s-test' >&2; exit 1;
}
SERVICE="$2"
[[ "$SERVICE" =~ ^wex-e1s-test[-a-zA-Z0-9_]*$ ]] || { echo 'FAIL: test service name' >&2; exit 1; }
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname -- "$SCRIPT_DIR")"
# No lock-file creation/truncation through a pre-existing symlink. flock the
# root-owned /run directory inode instead (nonblocking, conservative scope).
exec 9</run
flock -n 9 || { echo 'FAIL: /run fixture lock busy' >&2; exit 1; }
FIXTURE="$(mktemp -d /run/wex-pam-test-XXXXXXXX)"
LOCK_UID= EXPIRE_UID=
cleanup()
{
    status=$?
    trap - EXIT
    # Delete only our exact newly created identities, never an S2 account.
    if [ -n "$LOCK_UID" ]; then
        if [ "$(id -u wexs7_locked 2>/dev/null || true)" = "$LOCK_UID" ]; then userdel wexs7_locked || status=1; else status=1; fi
    fi
    if [ -n "$EXPIRE_UID" ]; then
        if [ "$(id -u wexs7_expired 2>/dev/null || true)" = "$EXPIRE_UID" ]; then userdel wexs7_expired || status=1; else status=1; fi
    fi
    # mktemp produced this absolute direct child of /run; no computed parents.
    case "$FIXTURE" in /run/wex-pam-test-????????) rm -rf -- "$FIXTURE" ;; *) status=1 ;; esac
    if [ "$status" -eq 0 ]; then echo 'E1S7_PAM_WORKER OK'; else echo 'E1S7_PAM_WORKER FAIL'; fi
    exit "$status"
}
trap cleanup EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
for name in wexs7_locked wexs7_expired; do
    if getent passwd "$name" >/dev/null; then fail "foreign fixture account $name"; fi
done
# Read S2's root-only file in the client; passwords never enter shell arguments.
[ "$(stat -c '%u:%g:%a' /root/e1s-fixtures-passwords)" = 0:0:600 ] || fail 'S2 password file metadata'
for name in bob alice carol eve; do getent passwd "$name" >/dev/null || fail "missing S2 fixture $name"; done
getent group wexsys >/dev/null || fail 'missing S2 wexsys group'
getent group sudo >/dev/null || fail 'missing S2 sudo group'
command -v journalctl >/dev/null || fail 'journal secret scan unavailable'

cc -O2 -Wall -Wextra -Werror -o "$FIXTURE/disabled" "$REPO/tools/e1s7/pam-worker.c"
cc -O2 -Wall -Wextra -Werror -DWEX_E1S7_LAB \
    -o "$FIXTURE/fixed-worker" "$REPO/tools/e1s7/pam-worker.c" -lpam -lrt
cc -O2 -Wall -Wextra -Werror -DWEX_E1S7_LAB -DWEX_E1S7_TEST \
    -o "$FIXTURE/worker" "$REPO/tools/e1s7/pam-worker.c" -lpam -lrt
cc -O2 -Wall -Wextra -Werror -fPIC -shared \
    -o "$FIXTURE/pam-lab.so" "$REPO/tools/e1s7/pam-lab-module.c" -lpam
cc -O2 -Wall -Wextra -Werror -o "$FIXTURE/accounts" "$REPO/tools/wex-accounts/wex-accounts.c"
for binary in disabled fixed-worker worker pam-lab.so accounts; do
    [ -f "$FIXTURE/$binary" ] || fail "missing build artifact $binary"
done
command -v strings >/dev/null || fail 'strings required for production exclusion checks'
strings "$FIXTURE/disabled" > "$FIXTURE/disabled.strings"
strings "$FIXTURE/fixed-worker" > "$FIXTURE/fixed.strings"
strings "$FIXTURE/worker" > "$FIXTURE/test.strings"
! grep -qF E1S7PAM "$FIXTURE/disabled.strings" || fail 'disabled worker contains PAM protocol'
for marker in --test-service --test-confdir --test-account-dir --test-raw-conversation; do
    ! grep -qF -- "$marker" "$FIXTURE/fixed.strings" || fail 'fixed worker contains test overrides'
    grep -qF -- "$marker" "$FIXTURE/test.strings" || fail 'test worker lacks override positive control'
done
if "$FIXTURE/disabled"; then fail 'disabled worker accepted work';
else [ "$?" -eq 1 ] || fail 'disabled worker did not exit 1'; fi
if "$FIXTURE/fixed-worker" --test-service "$SERVICE"; then fail 'fixed worker accepted test override';
else [ "$?" -eq 1 ] || fail 'fixed worker did not exit 1'; fi
if "$FIXTURE/fixed-worker" --test-raw-conversation 1; then fail 'fixed worker accepted raw conversation';
else [ "$?" -eq 1 ] || fail 'fixed worker raw override did not exit 1'; fi

useradd -M -U -s /bin/sh wexs7_locked
LOCK_UID="$(id -u wexs7_locked)"
useradd -M -U -s /bin/sh wexs7_expired
EXPIRE_UID="$(id -u wexs7_expired)"
# Client prepares only our new fixtures, private pam.d and S4 configuration;
# real S2 NSS identities and passwords are retained. No Wine build or run.
python3 "$REPO/tools/e1s7/test-client.py" "$FIXTURE" "$SERVICE" "$REPO/tools/e1s7"
