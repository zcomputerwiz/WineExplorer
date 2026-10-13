#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
set -euo pipefail
LD_PRELOAD="${WEX_CLOCK_LIBRARY:?Set WEX_CLOCK_LIBRARY}" \
    exec "${WEX_SERVER_BINARY:?Set WEX_SERVER_BINARY}" "$@"
