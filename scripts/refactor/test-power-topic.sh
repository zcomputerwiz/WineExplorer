#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Run the same ntdll info suite with and without a synthetic system battery.
set -euo pipefail
root="${WEX_REFACTOR_ROOT:-$HOME/wex-refactor}"
build="${WEX_TEST_BUILD:?Set WEX_TEST_BUILD}"
result=0
for mode in battery none; do
    rm -f "$build/dlls/ntdll/tests/x86_64-windows/info.ok"
    bash "$root/power-fixture.sh" "$mode" "$@" || result=$?
done
exit "$result"
