#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Check that 32-bit Windows programs (custom Win32 and Wine's 32-bit syswow64 notepad)
# and a 64-bit program run and draw windows on the installed desktop session.
#
# Usage: check-wow64.sh VM_NAME PASSWORD_FILE [STEPS_DIR]
set -euo pipefail

VM=${1:?usage: check-wow64.sh VM_NAME PASSWORD_FILE [STEPS_DIR]}
PWFILE=${2:?missing password file}
HERE=$(cd "$(dirname "$0")" && pwd)
STEPS=${3:-${VMTEST_DIR:-/tmp/vmtest}/$VM/steps}
V="python3 $HERE/../../scripts/vmtest.py"

mkdir -p "$STEPS"
TMPBUILD=$(mktemp -d /tmp/wextest.XXXXXX)
trap 'rm -rf -- "$TMPBUILD"' EXIT

echo "=== Building test32.exe (i686-w64-mingw32-gcc) and test64.exe (x86_64-w64-mingw32-gcc) ==="
i686-w64-mingw32-gcc -mwindows -O2 -s -o "$TMPBUILD/test32.exe" "$HERE/test-program.c"
x86_64-w64-mingw32-gcc -mwindows -O2 -s -o "$TMPBUILD/test64.exe" "$HERE/test-program.c"

echo "=== Staging test executables into VM ==="
stage_file() {
    local src=$1 dst=$2
    $V sh "$VM" root "$PWFILE" "rm -f $dst.b64 $dst"
    split -b 16384 "$src" "$TMPBUILD/chunk_"
    for chunk in "$TMPBUILD/chunk_"*; do
        local b64
        b64=$(base64 -w0 < "$chunk")
        $V sh "$VM" root "$PWFILE" "echo -n '$b64' >> $dst.b64"
    done
    rm -f "$TMPBUILD/chunk_"*
    $V sh "$VM" root "$PWFILE" "base64 -d $dst.b64 > $dst && chmod 777 $dst && rm -f $dst.b64"
}

stage_file "$TMPBUILD/test32.exe" /tmp/test32.exe
stage_file "$TMPBUILD/test64.exe" /tmp/test64.exe

# Discover desktop user, active DISPLAY, and XAUTHORITY from running wine session
$V sh "$VM" root "$PWFILE" 'BOB_PID=$(pgrep -f "explorer.exe.*/desktop" | tail -1); USER=$(stat -c "%U" /proc/$BOB_PID); DISP=$(grep -z "^DISPLAY=" /proc/$BOB_PID/environ | tr -d "\0" | cut -d= -f2-); XAUTH=$(grep -z "^XAUTHORITY=" /proc/$BOB_PID/environ | tr -d "\0" | cut -d= -f2-); echo "Desktop user: $USER on $DISP"; echo "$USER" > /tmp/desktop_user.txt; echo "$DISP" > /tmp/desktop_disp.txt; echo "$XAUTH" > /tmp/desktop_xauth.txt'

echo "=== 1. Running 32-bit test program on desktop ==="
$V sh "$VM" root "$PWFILE" 'USER=$(cat /tmp/desktop_user.txt); DISP=$(cat /tmp/desktop_disp.txt); XAUTH=$(cat /tmp/desktop_xauth.txt); su "$USER" -c "DISPLAY=$DISP XAUTHORITY=$XAUTH WINEDLLOVERRIDES=\"mscoree,mshtml=\" wine /tmp/test32.exe >/dev/null 2>&1 &"'
$V wait "$VM" "Win32 Test Program" --timeout 45
$V shot "$VM" "$STEPS/18-test32.png"
echo "PASS: Win32 test program window drawn"

echo "=== 2. Running Wine 32-bit notepad from syswow64 ==="
$V sh "$VM" root "$PWFILE" 'USER=$(cat /tmp/desktop_user.txt); DISP=$(cat /tmp/desktop_disp.txt); XAUTH=$(cat /tmp/desktop_xauth.txt); su "$USER" -c "DISPLAY=$DISP XAUTHORITY=$XAUTH WINEDLLOVERRIDES=\"mscoree,mshtml=\" wine C:/windows/syswow64/notepad.exe >/dev/null 2>&1 &"'
$V wait "$VM" "Notepad" --timeout 45
$V shot "$VM" "$STEPS/19-notepad32.png"
echo "PASS: 32-bit syswow64 notepad window drawn"

echo "=== 3. Running 64-bit test program ==="
$V sh "$VM" root "$PWFILE" 'USER=$(cat /tmp/desktop_user.txt); DISP=$(cat /tmp/desktop_disp.txt); XAUTH=$(cat /tmp/desktop_xauth.txt); su "$USER" -c "DISPLAY=$DISP XAUTHORITY=$XAUTH WINEDLLOVERRIDES=\"mscoree,mshtml=\" wine /tmp/test64.exe >/dev/null 2>&1 &"'
$V wait "$VM" "Win64 Test Program" --timeout 45
$V shot "$VM" "$STEPS/20-test64.png"
echo "PASS: Win64 test program window drawn"

echo "=== Cleaning up running test processes ==="
$V sh "$VM" root "$PWFILE" 'pkill -f test32.exe || true; pkill -f notepad.exe || true; pkill -f test64.exe || true'

echo "PASS: All WoW64 32-bit and 64-bit program checks succeeded."
