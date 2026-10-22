#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# No installed-runtime fallback, guessed display, or success on missing tests.
set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
exec python3 -B "$SCRIPT_DIR/check-token-security.py" "$@"
