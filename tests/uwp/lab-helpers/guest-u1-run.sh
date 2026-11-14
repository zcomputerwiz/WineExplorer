#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Run the packaged U1.0 fixture against a built candidate. usage: guest-u1-run.sh CANDIDATE RUN_NAME
set -euo pipefail
root=/home/debian/uwp-u1-20261111
candidate="$root/build/candidates/$1"
run="$candidate/$2"
fixture="$root/claude/fixture"
test "$(cat "$root/owner")" = codex-uwp-u1-20261111
test "$(cat "$candidate/phase")" = BUILT
test "$(cat "$candidate/exit")" = 0
test ! -e "$run"
x86_64-w64-mingw32-gcc -O2 -Wall -municode -o "$fixture/x64/context-negatives.exe" "$fixture/context-negatives.c" -lntdll
i686-w64-mingw32-gcc -O2 -Wall -municode -o "$fixture/x86/context-negatives.exe" "$fixture/context-negatives.c" -lntdll
status=0
WINEDEBUG="${WINEDEBUG:--all}" bash "$fixture/run-wine-packaged.sh" "$candidate/build" "$fixture" "$run" || status=$?
echo "$status" > "$run/exit"
exit "$status"
