#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# wex-suite-only.sh SECTIONS - the GPO worktree's build, then only the named suite sections (WEX_SUITE_ONLY; section and
# group names, comma separated: docs/TOOLING.md, "Sections of the system tier suites"), as root in wexgpo.
# Log /var/tmp/wex-suite-only.log; the suite's own log is /var/tmp/wex-run/normal.log. PARTIAL, never a merge check.
T=${WEX_GPO_TREE:-/mnt/d/DeepThought/Projects/WineExplorer-gpo}
cd "$T" || exit 1
WEX_SUITE_ONLY=${1:?sections} bash scripts/lab/wex-run.sh normal > /var/tmp/wex-suite-only.log 2>&1
echo EXIT=$? >> /var/tmp/wex-suite-only.log
cat /var/tmp/wex-suite-only.log
grep -E '^(FAIL|SECTION|=== sysrun)' /var/tmp/wex-run/normal.log | grep -v SKIP | head -40
