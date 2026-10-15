#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Phase S2 Acceptance Test:
# 1. Alice (administrator in sudo) installs program into C:\Program Files
# 2. Bob (limited user) sees and runs it
# 3. Bob cannot write C:\WINDOWS\system32
# 4. Bob cannot write C:\Program Files
# 5. Nothing per-user lands in the machine root
set -euo pipefail

MACHINE_ROOT="/var/lib/wineexplorer/machine"
SKEL_PFX="/etc/skel/.wine"

echo "=== WineExplorer Phase S2 Acceptance Test ==="

# Ensure setup-machine-root has run
/root/WineExplorer/scripts/setup-machine-root.sh

# Ensure alice and bob exist
id alice >/dev/null 2>&1 || useradd -m -s /bin/bash -G sudo alice
id bob >/dev/null 2>&1 || useradd -m -s /bin/bash bob

# Initialize prefixes for alice and bob from /etc/skel/.wine
echo "--- Initializing user prefixes from /etc/skel/.wine ---"
rm -rf /home/alice/.wine /home/bob/.wine
cp -a "$SKEL_PFX" /home/alice/.wine
cp -a "$SKEL_PFX" /home/bob/.wine

# Adjust user-specific paths in user.reg
mkdir -p /home/alice/.wine/drive_c/users/alice
mkdir -p /home/bob/.wine/drive_c/users/bob
chown -R alice:alice /home/alice/.wine
chown -R bob:bob /home/bob/.wine

echo "Alice prefix size: $(du -sh /home/alice/.wine | awk '{print $1}')"
echo "Bob prefix size:   $(du -sh /home/bob/.wine | awk '{print $1}')"

# Snapshot machine root state
MANIFEST_BEFORE=$(mktemp)
find "$MACHINE_ROOT" -type f -exec sha256sum {} + | sort > "$MANIFEST_BEFORE"
FILES_COUNT_BEFORE=$(wc -l < "$MANIFEST_BEFORE")
echo "Machine root files before test: $FILES_COUNT_BEFORE"

# --- Test 1: Alice (Administrator) installs a program into C:\Program Files ---
echo "--- Test 1: Alice installs a program into C:\Program Files ---"
su - alice -c '
    export WINEDLLOVERRIDES="mscoree,mshtml="
    export WINEPREFIX="/home/alice/.wine"
    wine cmd.exe /c mkdir "C:\\Program Files\\AliceApp"
    wine cmd.exe /c copy Z:\\usr\\local\\bin\\check_s1_64.exe "C:\\Program Files\\AliceApp\\app64.exe"
    wine cmd.exe /c copy Z:\\usr\\local\\bin\\check_s1_32.exe "C:\\Program Files\\AliceApp\\app32.exe"
    wineserver -k
'

APP64="$MACHINE_ROOT/drive_c/Program Files/AliceApp/app64.exe"
APP32="$MACHINE_ROOT/drive_c/Program Files/AliceApp/app32.exe"

if [ -f "$APP64" ] && [ -f "$APP32" ]; then
    echo "PASS: Alice installed app64.exe and app32.exe into machine Program Files!"
    ls -l "$MACHINE_ROOT/drive_c/Program Files/AliceApp"
else
    echo "FAIL: Alice failed to install apps into Program Files!"
    exit 1
fi

# --- Test 2: Bob (Limited User) sees and runs the program ---
echo "--- Test 2: Bob (Limited User) sees and runs Alice'\''s program ---"
BOB_OUTPUT_64=$(su - bob -c '
    export WINEDLLOVERRIDES="mscoree,mshtml="
    export WINEPREFIX="/home/bob/.wine"
    wine "C:\\Program Files\\AliceApp\\app64.exe"
    wineserver -k
')
echo "$BOB_OUTPUT_64"
if echo "$BOB_OUTPUT_64" | grep -q "Process running: 64-bit"; then
    echo "PASS: Bob successfully executed 64-bit app from C:\\Program Files!"
else
    echo "FAIL: Bob could not run 64-bit app!"
    exit 1
fi

BOB_OUTPUT_32=$(su - bob -c '
    export WINEDLLOVERRIDES="mscoree,mshtml="
    export WINEPREFIX="/home/bob/.wine"
    wine "C:\\Program Files\\AliceApp\\app32.exe"
    wineserver -k
')
echo "$BOB_OUTPUT_32"
if echo "$BOB_OUTPUT_32" | grep -q "Process running: 32-bit"; then
    echo "PASS: Bob successfully executed 32-bit app from C:\\Program Files!"
else
    echo "FAIL: Bob could not run 32-bit app!"
    exit 1
fi

# --- Test 3: Bob cannot write C:\WINDOWS\system32 ---
echo "--- Test 3: Bob cannot write C:\WINDOWS\system32 ---"
BOB_WRITE_WIN=$(su - bob -c '
    export WINEDLLOVERRIDES="mscoree,mshtml="
    export WINEPREFIX="/home/bob/.wine"
    wine cmd.exe /c copy Z:\\usr\\local\\bin\\check_s1_64.exe "C:\\windows\\system32\\bob_hack.dll" 2>&1 || true
    wineserver -k
')
echo "$BOB_WRITE_WIN"

if [ ! -e "$MACHINE_ROOT/drive_c/windows/system32/bob_hack.dll" ]; then
    echo "PASS: Bob was denied write access to C:\\windows\\system32!"
else
    echo "FAIL: Bob successfully wrote to C:\\windows\\system32!"
    exit 1
fi

# --- Test 4: Bob cannot write C:\Program Files ---
echo "--- Test 4: Bob cannot write C:\Program Files ---"
BOB_WRITE_PF=$(su - bob -c '
    export WINEDLLOVERRIDES="mscoree,mshtml="
    export WINEPREFIX="/home/bob/.wine"
    wine cmd.exe /c mkdir "C:\\Program Files\\BobApp" 2>&1 || true
    wineserver -k
')
echo "$BOB_WRITE_PF"

if [ ! -d "$MACHINE_ROOT/drive_c/Program Files/BobApp" ]; then
    echo "PASS: Bob was denied write access to C:\\Program Files!"
else
    echo "FAIL: Bob successfully created directory in C:\\Program Files!"
    exit 1
fi

# --- Test 5: Nothing per-user lands in machine root ---
echo "--- Test 5: Verify nothing per-user lands in machine root ---"
# Remove AliceApp to compare baseline machine files
rm -rf "$MACHINE_ROOT/drive_c/Program Files/AliceApp"

MANIFEST_AFTER=$(mktemp)
find "$MACHINE_ROOT" -type f -exec sha256sum {} + | sort > "$MANIFEST_AFTER"
DIFF_COUNT=$(diff -u "$MANIFEST_BEFORE" "$MANIFEST_AFTER" | grep -E '^\+[^+]|^\-[^-]' | wc -l || true)

rm -f "$MANIFEST_BEFORE" "$MANIFEST_AFTER"

if [ "$DIFF_COUNT" -eq 0 ]; then
    echo "PASS: Exactly 0 machine root files modified, created, or deleted by user sessions!"
else
    echo "FAIL: $DIFF_COUNT unexpected changes in machine root!"
    exit 1
fi

echo "=== ALL S2 ACCEPTANCE TESTS PASSED PERFECTLY! ==="
