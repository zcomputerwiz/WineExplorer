#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Run on the build VM using patches exported from the host topic worktree.
# No installed Wine runtime is modified.
set -euo pipefail
if [ "$#" -ne 4 ]; then
    echo "Usage: $0 topic module test 'module: Commit summary.'" >&2
    exit 2
fi
topic="$1"
module="$2"
test_name="$3"
summary="$4"
root="${WEX_REFACTOR_ROOT:-$HOME/wex-refactor}"
source="$root/upstream"
build="$root/build"
input="$root/topics/$topic"
output="$root/results/$topic"
mkdir -p "$root"
exec 9> "$root/test-runner.lock"
flock -n 9 || { echo 'Another topic runner holds the checkout/build lock.' >&2; exit 1; }
mkdir -p "$output" "$root/prefixes"
printf '%s\n' WAITING_FOR_BASE_BUILD > "$output/status"
for ((attempt=0; attempt<720; ++attempt)); do
    if [ -f "$root/base-build.status" ]; then break; fi
    sleep 5
done
test -f "$root/base-build.status"
test "$(git -C "$source" rev-parse master)" = 081042ede6a165a36bf9988350dd584b440d7a3d
test -z "$(git -C "$source" status --porcelain)"
if git -C "$source" show-ref --quiet "refs/heads/upstream/$topic"; then
    test "$(git -C "$source" rev-parse "upstream/$topic")" = "$(git -C "$source" rev-parse master)"
    git -C "$source" switch "upstream/$topic"
else
    git -C "$source" switch -c "upstream/$topic" master
fi
git -C "$source" apply --check "$input/tests.patch"
git -C "$source" apply "$input/tests.patch"
printf '%s\n' BUILDING_REGRESSION > "$output/status"
make -C "$build" -j8 "dlls/$module/tests/all" > "$output/test-build.log" 2>&1
export WINEPREFIX="$root/prefixes/$topic"
export WINESERVER="$build/server/wineserver"
export WINELOADER="$build/loader/wine"
export WINEDEBUG=-all
export WINEDLLOVERRIDES='mscoree,mshtml='
export WINETEST_INTERACTIVE=0
export WINETEST_DEBUG=1
run_test()
{
    local phase="$1"
    local target="dlls/$module/tests/x86_64-windows/$test_name.ok"
    rm -f "$build/$target"
    if [ -f "$input/test-wrapper.sh" ]; then
        WEX_TEST_BUILD="$build" timeout 600 bash "$input/test-wrapper.sh" \
            xvfb-run -a -s '-screen 0 1280x1024x24' make -C "$build" "$target" \
            > "$output/$phase.log" 2>&1 9>&-
        return
    fi
    timeout 600 xvfb-run -a -s '-screen 0 1280x1024x24' \
        make -C "$build" "$target" > "$output/$phase.log" 2>&1 9>&-
}
printf '%s\n' RUNNING_UNPATCHED_REGRESSION > "$output/status"
set +e
run_test before
before=$?
"$WINESERVER" -k
set -e
printf '%s\n' "$before" > "$output/before.exit"
if [ "$before" -eq 0 ] || [ "$before" -eq 124 ]; then
    printf '%s\n' REGRESSION_NOT_CONFIRMED > "$output/status"
    exit 1
fi
failure_pattern="$(tr -d '\r\n' < "$input/failure-pattern")"
if ! grep -E "Test failed:.*($failure_pattern)" "$output/before.log"; then
    printf '%s\n' EXPECTED_REGRESSION_NOT_FOUND > "$output/status"
    exit 1
fi
git -C "$source" apply --check "$input/fix.patch"
git -C "$source" apply "$input/fix.patch"
printf '%s\n' BUILDING_FIX > "$output/status"
targets=("dlls/$module/all" "dlls/$module/tests/all")
if [ -f "$input/build-targets" ]; then
    while IFS= read -r target; do
        target="${target%$'\r'}"
        [[ "$target" =~ ^dlls/[[:alnum:]_.-]+/all$ ]] || exit 2
        targets+=("$target")
    done < "$input/build-targets"
fi
if [ "$module" = comctl32 ]; then targets+=("dlls/comctl32_v6/all"); fi
make -C "$build" -j8 "${targets[@]}" > "$output/fix-build.log" 2>&1
printf '%s\n' RUNNING_FIXED_REGRESSION > "$output/status"
set +e
run_test after
after=$?
"$WINESERVER" -k
set -e
printf '%s\n' "$after" > "$output/after.exit"
if [ "$after" -ne 0 ]; then
    printf '%s\n' FIX_TEST_FAILED > "$output/status"
    exit 1
fi
git -C "$source" diff --check
git -C "$source" add -A
# Use the user's Git identity; final WineHQ authorship/sign-off is manual.
# Never add generated co-author trailers.
git -C "$source" -c user.name="${WEX_GIT_NAME:?Set WEX_GIT_NAME}" \
    -c user.email="${WEX_GIT_EMAIL:?Set WEX_GIT_EMAIL}" \
    commit -m "$summary"
git -C "$source" rev-parse HEAD > "$output/commit"
printf '%s\n' TESTS_PASSED > "$output/status"
