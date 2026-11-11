#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
set -euo pipefail

# The control tool verifies the exact owned QEMU PID/start time/disk, and SSH
# uses the pinned host key. Arbitrary endpoint overrides are not supported.
exec python3 "$(dirname "$0")/owned-lab.py" ssh "$@"
