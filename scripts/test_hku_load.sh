#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Root, CLEAN wexbuild only. Two root-protected WoW64 builds are prerequisites.
# Lab server: WEX_E1S13_HKU_TEST + WEX_E1S10_TOKEN_TEST + WEX_E1S6_TRANSPORT_TEST;
# force rebuild registry.o/token.o/e1s_profile.o/wex_profile_storage.o/file.o/fd.o.
# Normal builds must contain none of these defines. See E1_S13_HKU_LOAD.md.
set -euo pipefail
export WEX_E1S13_HARNESS=1
exec bash "$(dirname "${BASH_SOURCE[0]}")/test_e1s10_token.sh" "$@"
