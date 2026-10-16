#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Complete acceptance test runner for Open-Shell Start menu skins in WineExplorer (Phase K4)
# Captures real Start menu in the real shell (explorer.exe /desktop=Shell /shell)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
LOG_DIR="$REPO_DIR/docs/start-skins/logs"
SHOT_DIR="$REPO_DIR/docs/start-skins/screenshots"
LOG_FILE="$LOG_DIR/k4_acceptance.log"

mkdir -p "$LOG_DIR" "$SHOT_DIR"
cd "$REPO_DIR"

echo "=== WineExplorer Start Menu Skins Acceptance Suite (Real Shell) ===" | tee "$LOG_FILE"
date | tee -a "$LOG_FILE"

# 1. Ensure bundled skins are built
echo -e "\n=== 1. Building bundled open-art skins ===" | tee -a "$LOG_FILE"
bash scripts/build_bundled_skins.sh | tee -a "$LOG_FILE"

# 2. Stage genuine third-party community skin (Catppuccin Mocha by ScottyGrotty)
echo -e "\n=== 2. Staging genuine third-party skin (Catppuccin Mocha) ===" | tee -a "$LOG_FILE"
CATPPUCCIN_DIR="/root/catppuccin-open-shell/skins/mocha"
USER_SKINS="/root/.wine_test_shell/drive_c/users/root/AppData/Roaming/WineExplorer/StartSkins"
if [ -d "$CATPPUCCIN_DIR" ]; then
    mkdir -p "$USER_SKINS"
    cp -f "$CATPPUCCIN_DIR/Catppuccin Mocha.skin" "$USER_SKINS/"
    cp -f "$CATPPUCCIN_DIR/Catppuccin Mocha.skin7" "$USER_SKINS/"
    echo "Staged Catppuccin Mocha into $USER_SKINS" | tee -a "$LOG_FILE"
    ls -lh "$USER_SKINS" | tee -a "$LOG_FILE"
else
    echo "WARNING: /root/catppuccin-open-shell not found" | tee -a "$LOG_FILE"
fi

# 3. Ensure X11 socket directory permissions
sudo mount -o remount,rw /tmp/.X11-unix 2>/dev/null || true
sudo chmod 1777 /tmp/.X11-unix 2>/dev/null || true

# 4. Run real shell screenshot capture and baseline comparison
echo -e "\n=== 3. Running real shell Start menu capture and comparison suite ===" | tee -a "$LOG_FILE"
python3 scripts/capture_all_acceptance.py 2>&1 | tee -a "$LOG_FILE"

echo -e "\n=== Acceptance suite completed successfully ===" | tee -a "$LOG_FILE"
