#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root, CLEAN wexbuild only. Two root-protected WoW64 builds are prerequisites.
# Lab server: WEX_E1S13_HKU_TEST + WEX_E1S10_TOKEN_TEST + WEX_E1S6_TRANSPORT_TEST;
# S14 adds stable leases, durable save/unload, generation proxies and recovery.
# The harness also forces process.o and both userenv DLLs. Crash injection is
# present ONLY in this lab build via WEX_E1S14_CRASH_TEST, never normal builds.
# Normal builds must contain none of these defines. See E1_S13_HKU_LOAD.md.
set -euo pipefail
export WEX_E1S13_HARNESS=1
exec bash "$(dirname "${BASH_SOURCE[0]}")/test_e1s10_token.sh" "$@"
