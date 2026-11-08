#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Native C unit test for launch preparation function (E1-S15).
# Run as root in disposable wexbuild.
set -euo pipefail

[ "$(id -u)" -eq 0 ] || { echo 'FAIL: run as root' >&2; exit 1; }

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname -- "$SCRIPT_DIR")"
FIXTURE="$(mktemp -d /run/wex-launch-prep-test-XXXXXXXX)"

cleanup()
{
    rm -rf -- "$FIXTURE"
}
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

chmod 0755 "$FIXTURE"

# Compile production (disabled by default) and lab builds
cc -O2 -Wall -Wextra -Werror -I"$REPO/src/programs/wexelevate" \
    -o "$FIXTURE/production" \
    "$REPO/src/programs/wexelevate/launch_prep.c" \
    "$REPO/src/programs/wexelevate/test_launch_prep_main.c"

cc -O2 -Wall -Wextra -Werror -DWEX_E1S15_LAUNCH_PREP_TEST \
    -I"$REPO/src/programs/wexelevate" \
    -o "$FIXTURE/lab" \
    "$REPO/src/programs/wexelevate/launch_prep.c" \
    "$REPO/src/programs/wexelevate/test_launch_prep_main.c"

BOB_UID=62001
BOB_GID=62001

PREFIX="$FIXTURE/prefix"
mkdir -m 0700 "$PREFIX"
mkdir -p "$PREFIX/drive_c/windows/system32"
mkdir -p "$PREFIX/drive_c/Program Files"

PROFILE="$PREFIX/drive_c/users/.e1s/S-1-5-21-11-22-33-1001/00000000000000010000000000000002-alice"
mkdir -p "$PROFILE/AppData/Local/Temp"
mkdir -p "$PROFILE/AppData/Roaming"

# Set up an external secret directory and symlink target for traversal check
mkdir -m 0700 "$FIXTURE/secret"
ln -s "$FIXTURE/secret" "$PROFILE/AppData/symlink_escape"

chown -R "$BOB_UID:$BOB_GID" "$PREFIX"

echo "==> Testing production binary (disabled by default)"
"$FIXTURE/production" test_disabled "$PREFIX" "$PROFILE" "$BOB_UID" "$BOB_GID"

echo "==> Testing lab binary (all tests)"
"$FIXTURE/lab" all "$PREFIX" "$PROFILE" "$BOB_UID" "$BOB_GID"

echo "==> ALL E1-S15 launch prep tests passed successfully."
