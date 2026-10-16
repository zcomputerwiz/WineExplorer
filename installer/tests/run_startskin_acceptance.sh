#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Complete acceptance test runner for Open-Shell Start menu skins in WineExplorer (Phase K4)
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
LOG_DIR="$REPO_DIR/docs/start-skins/logs"
SHOT_DIR="$REPO_DIR/docs/start-skins/screenshots"
LOG_FILE="$LOG_DIR/k4_acceptance.log"
TMP_DIR="/tmp/wex_acceptance_skins"

mkdir -p "$LOG_DIR" "$SHOT_DIR" "$TMP_DIR"
cd "$REPO_DIR"

echo "=== WineExplorer Start Menu Skins Acceptance Runner ===" | tee "$LOG_FILE"
date | tee -a "$LOG_FILE"

# 1. Build third-party community skin (SmokedGlass) into /tmp/
echo -e "\n=== 1. Building third-party community skin (SmokedGlass) ===" | tee -a "$LOG_FILE"
SMOKED_SRC="/root/Open-Shell-Menu/Src/Skins/SmokedGlass"
SETUP_DIR="/root/Open-Shell-Menu/Src/Setup"
if [ -d "$SMOKED_SRC" ]; then
    rm -rf "$TMP_DIR/smoked"
    mkdir -p "$TMP_DIR/smoked"
    sed -e 's|\.\.\\\.\.\\Setup\\OpenShell\.ico|/root/Open-Shell-Menu/Src/Setup/OpenShell.ico|g' \
        -e 's|\\|/|g' \
        "$SMOKED_SRC/SmokedGlass.rc" > "$TMP_DIR/smoked/SmokedGlass.rc"
    (
        cd "$SMOKED_SRC"
        x86_64-w64-mingw32-windres -I . -I "$SMOKED_SRC" -I "$SETUP_DIR" -i "$TMP_DIR/smoked/SmokedGlass.rc" -o "$TMP_DIR/smoked/SmokedGlass.o"
        x86_64-w64-mingw32-gcc -shared -nostdlib -Wl,-e,0 "$TMP_DIR/smoked/SmokedGlass.o" -o "/tmp/SmokedGlass.skin"
    )
    ls -lh "/tmp/SmokedGlass.skin" | tee -a "$LOG_FILE"
else
    echo "WARNING: SmokedGlass source not found, skipping build" | tee -a "$LOG_FILE"
fi

# 2. Build fuzz test cases in /tmp
echo -e "\n=== 2. Preparing fuzz fallback skins ===" | tee -a "$LOG_FILE"
touch /tmp/test_fuzz_zero.skin
printf "MZ\x90\x00\x03\x00\x00\x00\x04\x00\x00\x00\xff\xff\x00\x00" > /tmp/test_fuzz_bad_pe.skin

cat << 'EOF' > "$TMP_DIR/dummy.c"
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p) { return TRUE; }
EOF
x86_64-w64-mingw32-gcc -shared -o /tmp/test_fuzz_no_skin.skin "$TMP_DIR/dummy.c" -nostdlib -Wl,--entry=DllMain

cat << 'EOF' > "$TMP_DIR/missing_bmp.rc"
1 SKIN "missing_desc.txt"
EOF
cat << 'EOF' > "$TMP_DIR/missing_desc.txt"
About=Missing Bitmap Skin
Version=2
Main_bitmap=9999
Main_bitmap_slices_X=2,1,2
Main_bitmap_slices_Y=2,20,2
EOF
(
    cd "$TMP_DIR"
    x86_64-w64-mingw32-windres -o missing_bmp.o missing_bmp.rc
    x86_64-w64-mingw32-gcc -shared -o /tmp/test_fuzz_missing_bmp.skin dummy.c missing_bmp.o -nostdlib -Wl,--entry=DllMain
)

cat << 'EOF' > "$TMP_DIR/garbage.rc"
1 SKIN "garbage_desc.txt"
EOF
python3 -c 'import sys; sys.stdout.buffer.write(bytes(range(1, 255)) * 10)' > "$TMP_DIR/garbage_desc.txt"
(
    cd "$TMP_DIR"
    x86_64-w64-mingw32-windres -o garbage.o garbage.rc
    x86_64-w64-mingw32-gcc -shared -o /tmp/test_fuzz_garbage.skin dummy.c garbage.o -nostdlib -Wl,--entry=DllMain
)

# 3. Compile acceptance test executable
echo -e "\n=== 3. Compiling acceptance test executable ===" | tee -a "$LOG_FILE"
x86_64-w64-mingw32-gcc -O2 -g -o "$REPO_DIR/installer/tests/test_startskin_acceptance.exe" \
    "$REPO_DIR/installer/tests/test_startskin_acceptance.c" \
    "$REPO_DIR/src/dlls/wexshell/startskin.c" \
    -I"$REPO_DIR/src/dlls/wexshell" \
    -lgdi32 -luser32 -ladvapi32 -lmsimg32 -lshlwapi

# 4. Start Xvfb display
echo -e "\n=== 4. Starting Xvfb display session ===" | tee -a "$LOG_FILE"
XVFB_DISPLAY=":98"
pkill -f "Xvfb $XVFB_DISPLAY" || true
Xvfb "$XVFB_DISPLAY" -screen 0 1280x1024x24 >/dev/null 2>&1 &
XVFB_PID=$!
sleep 1

# 5. Run acceptance test under Wine
echo -e "\n=== 5. Running acceptance tests under Wine ===" | tee -a "$LOG_FILE"
(
    DISPLAY="$XVFB_DISPLAY" WINEDEBUG=-all /usr/local/bin/wine \
        "$REPO_DIR/installer/tests/test_startskin_acceptance.exe" "$SHOT_DIR"
) 2>&1 | tee -a "$LOG_FILE"

# Stop Xvfb
kill "$XVFB_PID" 2>/dev/null || true

# 6. Convert captured bitmaps to PNG screenshots
echo -e "\n=== 6. Converting screenshots to PNG ===" | tee -a "$LOG_FILE"
for bmp in "$SHOT_DIR"/*.bmp; do
    if [ -f "$bmp" ]; then
        png="${bmp%.bmp}.png"
        convert "$bmp" "$png"
        rm -f "$bmp"
        echo "Saved screenshot: $(basename "$png")" | tee -a "$LOG_FILE"
    fi
done

if [ -f "/tmp/SmokedGlass_classic_96.bmp" ]; then
    convert "/tmp/SmokedGlass_classic_96.bmp" "/tmp/SmokedGlass_classic_96.png"
    rm -f "/tmp/SmokedGlass_classic_96.bmp"
    echo "Saved local third-party screenshot: /tmp/SmokedGlass_classic_96.png" | tee -a "$LOG_FILE"
fi

rm -rf "$TMP_DIR"

echo -e "\n=== Start Menu Skins Acceptance Test Finished Successfully ===" | tee -a "$LOG_FILE"
