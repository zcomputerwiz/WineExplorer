#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Compile test skins and run fuzz / unit tests for startskin loader
#
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
TEST_DIR="/tmp/test_startskin_run"
LOG_DIR="$REPO_DIR/docs/start-skins/logs"
LOG_FILE="$LOG_DIR/k1_fuzz_tests.log"

mkdir -p "$TEST_DIR" "$LOG_DIR"
cd "$TEST_DIR"

echo "=== Preparing test fuzz files ==="

# 1. 0-byte file
touch test_fuzz_zero.skin

# 2. Truncated corrupt PE
printf "MZ\x90\x00\x03\x00\x00\x00\x04\x00\x00\x00\xff\xff\x00\x00" > test_fuzz_bad_pe.skin

# 3. Valid DLL with no resources
cat << 'EOF' > dummy.c
#include <windows.h>
BOOL WINAPI DllMain(HINSTANCE h, DWORD r, LPVOID p) { return TRUE; }
EOF
x86_64-w64-mingw32-gcc -shared -o test_fuzz_no_skin.skin dummy.c -nostdlib -Wl,--entry=DllMain

# 4. Generate small test bitmap helper
cat << 'EOF' > make_bmp.py
import sys, struct

def create_bmp(filename, width, height, bpp=24):
    row_bytes = ((width * bpp + 31) // 32) * 4
    pixel_array_size = row_bytes * height
    file_size = 54 + pixel_array_size
    header = struct.pack('<2sIHHI', b'BM', file_size, 0, 0, 54)
    info = struct.pack('<IIIHHIIIIII', 40, width, height, 1, bpp, 0, pixel_array_size, 2835, 2835, 0, 0)
    with open(filename, 'wb') as f:
        f.write(header)
        f.write(info)
        f.write(b'\x80\x90\xA0' * (pixel_array_size // 3))
        if pixel_array_size % 3:
            f.write(b'\x00' * (pixel_array_size % 3))

if __name__ == '__main__':
    create_bmp('bmp1.bmp', 5, 24, 24)
    create_bmp('bmp2.bmp', 8, 36, 32)
EOF
python3 make_bmp.py

# 5. Missing bitmap skin
cat << 'EOF' > missing_bmp.rc
1 SKIN "missing_desc.txt"
EOF
cat << 'EOF' > missing_desc.txt
About=Missing Bitmap Skin
Version=2
Main_bitmap=9999
Main_bitmap_slices_X=2,1,2
Main_bitmap_slices_Y=2,20,2
EOF
x86_64-w64-mingw32-windres -o missing_bmp.o missing_bmp.rc
x86_64-w64-mingw32-gcc -shared -o test_fuzz_missing_bmp.skin dummy.c missing_bmp.o -nostdlib -Wl,--entry=DllMain

# 6. Garbage description skin
cat << 'EOF' > garbage.rc
1 SKIN "garbage_desc.txt"
EOF
python3 -c 'import sys; sys.stdout.buffer.write(bytes(range(1, 255)) * 10)' > garbage_desc.txt
x86_64-w64-mingw32-windres -o garbage.o garbage.rc
x86_64-w64-mingw32-gcc -shared -o test_fuzz_garbage.skin dummy.c garbage.o -nostdlib -Wl,--entry=DllMain

# 7. Valid test skin with variations and High-DPI support
cat << 'EOF' > valid.rc
1 BITMAP "bmp1.bmp"
2 BITMAP "bmp2.bmp"
1 SKIN   "valid_desc.txt"
3 SKIN   "dark_desc.txt"
EOF
cat << 'EOF' > valid_desc.txt
About=Test Valid Skin
Version=2
Variation1=2,Silver
Variation2=3,Dark
Main_background=#123456
Main_bitmap=1
Main_bitmap_slices_X=2,1,2
Main_bitmap_slices_Y=2,20,2

[HIGH_DPI]
Main_bitmap=2
Main_bitmap_slices_X=3,2,3
Main_bitmap_slices_Y=3,30,3

[TRUE]
Main_thin_frame=1
EOF
cat << 'EOF' > dark_desc.txt
Main_background=#222222
EOF
x86_64-w64-mingw32-windres -o valid.o valid.rc
x86_64-w64-mingw32-gcc -shared -o test_valid.skin dummy.c valid.o -nostdlib -Wl,--entry=DllMain

echo "=== Compiling test harness ==="
x86_64-w64-mingw32-gcc -O2 -g -o test_startskin_fuzz.exe \
    "$REPO_DIR/installer/tests/test_startskin_fuzz.c" \
    "$REPO_DIR/src/dlls/wexshell/startskin.c" \
    -I"$REPO_DIR/src/dlls/wexshell" \
    -lgdi32 -luser32 -ladvapi32 -lmsimg32 -lshlwapi

echo "=== Running test_startskin_fuzz.exe under Wine ==="
WINEDEBUG=-all /usr/local/bin/wine ./test_startskin_fuzz.exe | tee "$LOG_FILE"

echo "=== Fuzz test suite completed successfully ==="
