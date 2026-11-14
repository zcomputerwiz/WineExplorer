#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# U1.1 lifecycle plus the presentation, input and package API probes on Wine: once under a virtual X display (with
# 3D), once with no display at all (a view without a window server must carry on).
# usage: guest-u11-present.sh CANDIDATE RUN_NAME
set -euo pipefail
root=/home/debian/uwp-u1-20261111
candidate="$root/build/candidates/$1"
run="$candidate/$2"
fix="$root/claude/u11fix"
test "$(cat "$root/owner")" = codex-uwp-u1-20261111
test "$(cat "$candidate/phase")" = BUILT
status=0
PRESENT=1 xvfb-run -a -s "-screen 0 1920x1080x24" bash "$fix/u1_1/run-wine-lifecycle.sh" "$candidate/build" "$fix/u1_0" "$fix/u1_1" "$run" || status=$?
env -u DISPLAY -u WAYLAND_DISPLAY bash "$fix/u1_1/run-wine-lifecycle.sh" "$candidate/build" "$fix/u1_0" "$fix/u1_1" "$run-nodisplay" || status=$?
echo "$status" > "$run/exit"
exit "$status"
