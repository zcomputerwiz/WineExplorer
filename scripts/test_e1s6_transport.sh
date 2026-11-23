#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root in clean wexbuild or a throwaway VM; NEVER shared lab guest VM B.
# Must have NO installed system role. Never delete installed paths to run this.
# Requires an ALREADY BUILT WoW64 lab runtime, see docs/E1_S6_TRANSPORT.md.
#
# The lab suite: test_sysrun_server.sh with WEX_E1S6_LAB=1 runs the lab's own sections (the S6 transport, the R8
# cases, the R8/R9 end-to-end chain) and a smoke set of ordinary sections with the lab build, not the normal suite
# again (docs/TOOLING.md, "Sections of the system tier suites"). WEX_LAB_FULL=1 runs every section on the lab build
# (the lab suite as it was before 2026-11-23); WEX_SUITE_ONLY narrows it like the normal suite.
set -euo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export WEX_E1S6_LAB=1
if bash "$SRC/scripts/test_sysrun_server.sh"; then
    echo 'E1S6_TRANSPORT OK'
else
    echo 'E1S6_TRANSPORT FAIL' >&2
    exit 1
fi
