#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root in clean wexbuild or a throwaway VM; NEVER shared lab guest VM B.
# Must have NO installed system role. Never delete installed paths to run this.
# Requires an ALREADY BUILT WoW64 lab runtime, see docs/E1_S6_TRANSPORT.md.
set -euo pipefail
SRC="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export WEX_E1S6_LAB=1
export WEX_C3_ONLY=1
export WEX_C3_NO_OWN=1
if bash "$SRC/scripts/test_sysrun_server.sh"; then
    echo 'E1S6_TRANSPORT OK'
else
    echo 'E1S6_TRANSPORT FAIL' >&2
    exit 1
fi
