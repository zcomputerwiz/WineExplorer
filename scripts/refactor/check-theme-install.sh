#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Execute only the session's theme-copy block against a disposable prefix.
set -euo pipefail
project=$(cd "$(dirname "$0")/../.." && pwd)
fixture=$(mktemp -d)
trap 'rm -rf -- "$fixture"' EXIT
export WINEPREFIX="$fixture/prefix" WEX_THEME_DATA_DIR="$project/src/resources/themes"
target="$WINEPREFIX/drive_c/windows/Resources/Themes"
mkdir -p "$target"
printf 'user custom theme\n' > "$target/Classic.theme"
sed -n '/^# Install project-authored theme definitions/,/^# --- Winlogon:/p' \
    "$project/scripts/wine-explorer-session.sh" > "$fixture/copy.sh"
bash "$fixture/copy.sh"
grep -q '^user custom theme$' "$target/Classic.theme"
cmp "$target/Luna.theme" "$project/src/resources/themes/Luna.theme"
cmp "$target/Royale.theme" "$project/src/resources/themes/Royale.theme"
bash "$fixture/copy.sh"
grep -q '^user custom theme$' "$target/Classic.theme"
echo "PASS theme definitions installed without replacing user theme; repeat is safe"
