#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# U1.3 dependency test on a built candidate. usage: guest-u13-run.sh CANDIDATE RUN_NAME
set -euo pipefail
root=/home/debian/uwp-u1-20261111
candidate="$root/build/candidates/$1"
test "$(cat "$candidate/phase")" = BUILT
bash "$root/claude/u13fix/u1_3/run-wine-deps.sh" "$candidate/build" "$root/claude/u11fix/u1_0" "$root/claude/u13fix/u1_3" "$candidate/$2" || exit $?
