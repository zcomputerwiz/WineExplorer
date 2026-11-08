#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Native C unit test for E1-S18 Broker Wine-Only Integration.
# Run as root in disposable wexbuild.
set -euo pipefail

[ "$(id -u)" -eq 0 ] || { echo 'FAIL: run as root' >&2; exit 1; }

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(dirname -- "$SCRIPT_DIR")"
FIXTURE="$(mktemp -d /run/wex-broker-e1s18-test-XXXXXXXX)"

cleanup()
{
    rm -rf -- "$FIXTURE"
}
trap cleanup EXIT

fail() { echo "FAIL: $*" >&2; exit 1; }

chmod 0755 "$FIXTURE"

echo "==> Compiling production and lab binaries"

# 1. Compile production build (no WEXELEVATE_E1S18)
cc -O2 -Wall -Wextra -Werror \
    -I"$REPO/src/include" \
    -I"$REPO/src/programs/wexelevate" \
    -o "$FIXTURE/production" \
    "$REPO/src/programs/wexelevate/launch_prep.c" \
    "$REPO/src/programs/wexelevate/e1s18_broker.c" \
    "$REPO/src/programs/wexelevate/test_broker_e1s18_main.c"

# 2. Compile lab build (with -DWEXELEVATE_E1S18 -DWEX_E1S15_LAUNCH_PREP_TEST)
cc -O2 -Wall -Wextra -Werror \
    -DWEXELEVATE_E1S18 \
    -DWEX_E1S15_LAUNCH_PREP_TEST \
    -I"$REPO/src/include" \
    -I"$REPO/src/programs/wexelevate" \
    -o "$FIXTURE/lab" \
    "$REPO/src/programs/wexelevate/launch_prep.c" \
    "$REPO/src/programs/wexelevate/e1s18_broker.c" \
    "$REPO/src/programs/wexelevate/test_broker_e1s18_main.c"

# 3. Production build checks: disabled by default
echo "==> Testing production binary (disabled by default)"
"$FIXTURE/production" test_disabled

# 4. Production exclusion check: ensure no test symbols or gates leaked into source tree
echo "==> Checking production exclusion (no test defines in build files)"
! grep -rn 'WEXELEVATE_E1S18' "$REPO/src" "$REPO/installer" "$REPO/tools" \
    --include=Makefile.in --include='*.mk' --include='*.inf' \
    --include='*.sh' --include='*.py' >/dev/null 2>&1 || fail "a production build file names WEXELEVATE_E1S18"

# 5. Run all pure unit tests in lab binary
echo "==> Testing lab binary (all stages, matrix, single resume, ordering)"
"$FIXTURE/lab"

# 6. Cross-compile real broker with lab define for both x86_64 and i686 ABIs (WoW64)
echo "==> Cross-compiling real broker with lab define for x86_64 and i686 MinGW (-Wall -Werror)"
x86_64-w64-mingw32-gcc -O2 -Wall -Werror -municode \
    -DWEXELEVATE_TEST_APPROVAL -DWEXELEVATE_E1S18 \
    -I"$REPO/src/include" \
    -o "$FIXTURE/wexelevate_lab64.exe" \
    "$REPO/src/programs/wexelevate/main.c" \
    "$REPO/src/programs/wexelevate/launch_prep.c" \
    -ladvapi32 -luserenv -lbcrypt

i686-w64-mingw32-gcc -O2 -Wall -Werror -municode \
    -DWEXELEVATE_TEST_APPROVAL -DWEXELEVATE_E1S18 \
    -I"$REPO/src/include" \
    -o "$FIXTURE/wexelevate_lab32.exe" \
    "$REPO/src/programs/wexelevate/main.c" \
    "$REPO/src/programs/wexelevate/launch_prep.c" \
    -ladvapi32 -luserenv -lbcrypt

echo "==> ALL E1-S18 broker integration tests passed successfully."
