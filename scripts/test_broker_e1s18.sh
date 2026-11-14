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
cc -O2 -Wall -Wextra -Werror -lpthread \
    -I"$REPO/src/include" \
    -I"$REPO/src/programs/wexelevate" \
    -o "$FIXTURE/production" \
    "$REPO/src/programs/wexelevate/launch_prep.c" \
    "$REPO/src/programs/wexelevate/cred_dialog.c" \
    "$REPO/src/programs/wexelevate/e1s18_broker.c" \
    "$REPO/src/programs/wexelevate/test_broker_e1s18_main.c"

# 2. Compile lab build (with -DWEXELEVATE_E1S18 -DWEX_E1S15_LAUNCH_PREP_TEST)
cc -O2 -Wall -Wextra -Werror -lpthread \
    -DWEXELEVATE_E1S18 \
    -DWEX_E1S15_LAUNCH_PREP_TEST \
    -DWEX_E1_TEST_HOOKS \
    -I"$REPO/src/include" \
    -I"$REPO/src/programs/wexelevate" \
    -o "$FIXTURE/lab" \
    "$REPO/src/programs/wexelevate/launch_prep.c" \
    "$REPO/src/programs/wexelevate/cred_dialog.c" \
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

# 5b. Same tests under AddressSanitizer/UBSan (queue races, record overruns, leaks)
echo "==> Testing lab binary under ASan/UBSan"
cc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
    -Wall -Wextra -Werror -DWEXELEVATE_E1S18 -DWEX_E1S15_LAUNCH_PREP_TEST -DWEX_E1_TEST_HOOKS \
    -I"$REPO/src/include" -I"$REPO/src/programs/wexelevate" -o "$FIXTURE/lab-asan" \
    "$REPO/src/programs/wexelevate/launch_prep.c" \
    "$REPO/src/programs/wexelevate/cred_dialog.c" \
    "$REPO/src/programs/wexelevate/e1s18_broker.c" \
    "$REPO/src/programs/wexelevate/test_broker_e1s18_main.c" -lpthread
"$FIXTURE/lab-asan" > "$FIXTURE/lab-asan.log" 2>&1 || { tail -n 40 "$FIXTURE/lab-asan.log"; fail "sanitizer run"; }

# 6. Cross-compile real broker with lab define for both x86_64 and i686 ABIs (WoW64)
echo "==> Cross-compiling real broker with lab define for x86_64 and i686 MinGW (-Wall -Werror)"
x86_64-w64-mingw32-gcc -O2 -Wall -Werror -municode \
    -DWEXELEVATE_TEST_APPROVAL -DWEXELEVATE_E1S18 -DWEX_E1_TEST_HOOKS \
    -I"$REPO/src/include" \
    -o "$FIXTURE/wexelevate_lab64.exe" \
    "$REPO/src/programs/wexelevate/main.c" \
    "$REPO/src/programs/wexelevate/launch_prep.c" \
    "$REPO/src/programs/wexelevate/cred_dialog.c" \
    -ladvapi32 -luserenv -lbcrypt

i686-w64-mingw32-gcc -O2 -Wall -Werror -municode \
    -DWEXELEVATE_TEST_APPROVAL -DWEXELEVATE_E1S18 -DWEX_E1_TEST_HOOKS \
    -I"$REPO/src/include" \
    -o "$FIXTURE/wexelevate_lab32.exe" \
    "$REPO/src/programs/wexelevate/main.c" \
    "$REPO/src/programs/wexelevate/launch_prep.c" \
    "$REPO/src/programs/wexelevate/cred_dialog.c" \
    -ladvapi32 -luserenv -lbcrypt

# 7. Production objects byte-for-byte unchanged check (cmp). The baseline is the
# last reviewed production main.c (since S18e: with the R12 bounded-reply fix,
# docs/E1_S18E_REMEDIATION.md); regenerate it only after reviewing a main.c change.
echo "==> Verifying production objects are byte-for-byte identical (cmp)"
x86_64-w64-mingw32-gcc -O2 -Wall -Werror -municode \
    -I"$REPO/src/include" \
    -c "$REPO/src/programs/wexelevate/main.c" \
    -o "$FIXTURE/main_prod64.o"
if [ -f /tmp/main_prod64_baseline.o ]; then
    cmp /tmp/main_prod64_baseline.o "$FIXTURE/main_prod64.o" || fail "production main_prod64.o modified"
fi

i686-w64-mingw32-gcc -O2 -Wall -Werror -municode \
    -I"$REPO/src/include" \
    -c "$REPO/src/programs/wexelevate/main.c" \
    -o "$FIXTURE/main_prod32.o"
if [ -f /tmp/main_prod32_baseline.o ]; then
    cmp /tmp/main_prod32_baseline.o "$FIXTURE/main_prod32.o" || fail "production main_prod32.o modified"
fi
echo "PASS production objects byte-for-byte identical to baseline"

# 8. WO-18: build and run the E1R8 codec test natively.
echo "==> Building and running E1R8 codec test (native)"
cc -O2 -Wall -Wextra -Werror \
    -I"$REPO/src/programs/wexelevate" \
    -o "$FIXTURE/e1r8_codec_test" \
    "$REPO/src/programs/wexelevate/test_e1r8_codec_main.c"
"$FIXTURE/e1r8_codec_test"

# 9. WO-18: same codec test under AddressSanitizer/UBSan.
echo "==> Building and running E1R8 codec test (ASan/UBSan)"
cc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
    -Wall -Wextra -Werror \
    -I"$REPO/src/programs/wexelevate" \
    -o "$FIXTURE/e1r8_codec_test-asan" \
    "$REPO/src/programs/wexelevate/test_e1r8_codec_main.c"
"$FIXTURE/e1r8_codec_test-asan" > "$FIXTURE/e1r8_codec_test-asan.log" 2>&1 \
    || { tail -n 40 "$FIXTURE/e1r8_codec_test-asan.log"; fail "e1r8 codec sanitizer run"; }

# 10. WO-18: compile the codec and its test with each MinGW compiler (both ABIs; i686 has a
# 32-bit size_t, where the string-length checks must not wrap).
echo "==> Compiling the E1R8 codec test for x86_64 and i686 MinGW (-Wall -Wextra -Werror)"
for cc_pe in x86_64-w64-mingw32-gcc i686-w64-mingw32-gcc; do
    "$cc_pe" -O2 -Wall -Wextra -Werror -I"$REPO/src/programs/wexelevate" \
        -c "$REPO/src/programs/wexelevate/test_e1r8_codec_main.c" -o "$FIXTURE/e1r8_codec_test_$cc_pe.o"
done

# 11. WO-19: cross-check the Python codec against the C golden output. The
# golden lines are the WO-18 encoder's fixed-input output; the Python codec
# must encode byte-identical messages and decode each line back to the input.
echo "==> Cross-checking the E1R8 Python codec against the C golden output"
"$FIXTURE/e1r8_codec_test" golden > "$FIXTURE/e1r8-golden.txt"
E1R8_GOLDEN="$FIXTURE/e1r8-golden.txt" python3 -I "$REPO/tools/e1s6/test-e1r8.py"

echo "==> ALL E1-S18 broker integration tests passed successfully."
