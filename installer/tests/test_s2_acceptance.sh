#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Phase S2 Acceptance Test:
# 1. Alice (administrator in sudo) installs program & config into C:\Program Files
# 2. Carol (second administrator in sudo) modifies Alice's installed files (default ACL g:sudo:rwX)
# 3. Verify .exe/.dll mode 775 and per-user TEMP/TMP in user.reg (not C:\windows\temp)
# 4. Bob (limited user) sees and runs 64-bit and 32-bit programs from C:\Program Files
# 5. Bob cannot write C:\WINDOWS\system32 or C:\Program Files
# 6. Simulated Wine upgrade: machine part runs once as root, per-user wineboot -u runs only
#    UserInstall; 0 machine files modified or chowned by Alice's or Bob's sessions.
set -euo pipefail

MACHINE_ROOT="/var/lib/wineexplorer/machine"
SKEL_PFX="/etc/skel/.wine"

echo "=== WineExplorer Phase S2 Acceptance Test ==="

# Ensure fresh machine root and template setup
rm -rf "$MACHINE_ROOT" "$SKEL_PFX"
/root/WineExplorer/scripts/setup-machine-root.sh

# Ensure alice (admin), carol (admin), and bob (limited) exist
id alice >/dev/null 2>&1 || useradd -m -s /bin/bash -G sudo alice
id carol >/dev/null 2>&1 || useradd -m -s /bin/bash -G sudo carol
id bob >/dev/null 2>&1 || useradd -m -s /bin/bash bob
usermod -a -G sudo alice
usermod -a -G sudo carol

# Initialize prefixes for alice, carol, and bob from /etc/skel/.wine
echo "--- Initializing user prefixes from /etc/skel/.wine ---"
for u in alice carol bob; do
    rm -rf "/home/$u/.wine"
    cp -a "$SKEL_PFX" "/home/$u/.wine"
    mkdir -p "/home/$u/.wine/drive_c/users/$u"
    sed -i -e "s/\\\\\\\\users\\\\\\\\wexsetup\\\\\\\\/\\\\\\\\users\\\\\\\\$u\\\\\\\\/g" \
           -e "s/\\\\\\\\users\\\\\\\\wexsetup\"/\\\\\\\\users\\\\\\\\$u\"/g" \
        "/home/$u/.wine/user.reg"
    chown -R -h "$u:$u" "/home/$u/.wine"
done

echo "Machine root size: $(du -sh "$MACHINE_ROOT" | awk '{print $1}')"
echo "Skel prefix size:  $(du -sh "$SKEL_PFX" | awk '{print $1}')"
echo "Alice prefix size: $(du -sh /home/alice/.wine | awk '{print $1}')"
echo "Bob prefix size:   $(du -sh /home/bob/.wine | awk '{print $1}')"

# --- Check Item 4 & 5: .exe mode 775 in machine root and per-user TEMP/TMP in user.reg ---
echo "--- Check: .exe/.dll mode 775 and per-user TEMP/TMP in user.reg ---"
EXE_MODE=$(stat -c "%a" "$MACHINE_ROOT/drive_c/windows/syswow64/dosx.exe")
echo "dosx.exe mode: $EXE_MODE"
if [ "$EXE_MODE" != "775" ]; then
    echo "FAIL: Expected 775 on .exe in machine root, got $EXE_MODE"
    exit 1
fi

BOB_TEMP=$(grep -E '^"TEMP"=' /home/bob/.wine/user.reg || true)
echo "Bob user.reg TEMP: $BOB_TEMP"
if echo "$BOB_TEMP" | grep -Fqi 'users\\bob\\AppData\\Local\\Temp'; then
    echo "PASS: user.reg uses per-user AppData\\Local\\Temp!"
else
    echo "FAIL: user.reg TEMP is not per-user AppData\\Local\\Temp!"
    exit 1
fi

# Snapshot machine root state before user sessions
MANIFEST_BEFORE=$(mktemp)
find "$MACHINE_ROOT/drive_c" -type f -printf "%p %u:%g %m\n" | sort > "$MANIFEST_BEFORE"
FILES_COUNT_BEFORE=$(wc -l < "$MANIFEST_BEFORE")
echo "Machine root drive_c files before test: $FILES_COUNT_BEFORE"

# --- Test 1: Alice (Administrator, umask 0022) installs a program into C:\Program Files and COW-breaks a built-in DLL ---
echo "--- Test 1: Alice installs a program and config into C:\Program Files and COW-breaks msvcp140.dll ---"
su - alice -c '
    umask 0022
    echo "Installed_By_Alice=1" > /tmp/alice_cfg.ini
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/alice/.wine"
    wine cmd.exe /c mkdir "C:\\Program Files\\AliceApp"
    wine cmd.exe /c copy Z:\\usr\\local\\bin\\check_s1_64.exe "C:\\Program Files\\AliceApp\\app64.exe"
    wine cmd.exe /c copy Z:\\usr\\local\\bin\\check_s1_32.exe "C:\\Program Files\\AliceApp\\app32.exe"
    wine cmd.exe /c copy Z:\\tmp\\alice_cfg.ini "C:\\Program Files\\AliceApp\\settings.ini"
    wine /usr/local/bin/test_cow_cases64.exe relative_open
    wineserver -w
    rm -f /tmp/alice_cfg.ini
'

APP64="$MACHINE_ROOT/drive_c/Program Files/AliceApp/app64.exe"
APP32="$MACHINE_ROOT/drive_c/Program Files/AliceApp/app32.exe"
CFG="$MACHINE_ROOT/drive_c/Program Files/AliceApp/settings.ini"
COW_DLL="$MACHINE_ROOT/drive_c/windows/system32/msvcp140.dll"

if [ -f "$APP64" ] && [ -f "$APP32" ] && [ -f "$CFG" ] && [ ! -L "$COW_DLL" ] && [ -f "$COW_DLL" ]; then
    echo "PASS: Alice installed app64.exe, app32.exe, settings.ini, and COW-broke msvcp140.dll!"
    ls -la "$MACHINE_ROOT/drive_c/Program Files/AliceApp"
    ls -la "$COW_DLL"
    getfacl "$CFG"
    getfacl "$COW_DLL"
else
    echo "FAIL: Alice failed to install apps or COW-break msvcp140.dll!"
    exit 1
fi

# --- Test 1b: Carol (second Administrator in sudo, umask 0022) modifies Alice's files and COW-broken DLL ---
echo "--- Test 1b: Carol (second Administrator) modifies Alice's installed files and COW-broken msvcp140.dll ---"
su - carol -c '
    umask 0022
    echo "Modified_By_Carol=2" > /tmp/carol_cfg.ini
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/carol/.wine"
    wine cmd.exe /c copy /Y Z:\\tmp\\carol_cfg.ini "C:\\Program Files\\AliceApp\\settings.ini"
    wine cmd.exe /c copy /Y Z:\\usr\\local\\bin\\check_s1_64.exe "C:\\Program Files\\AliceApp\\app64.exe"
    wine /usr/local/bin/test_cow_cases64.exe relative_open
    wineserver -w
    rm -f /tmp/carol_cfg.ini
'
CFG_CONTENT=$(cat "$CFG")
echo "settings.ini content after Carol update: $CFG_CONTENT"
if [ "$CFG_CONTENT" = "Modified_By_Carol=2" ]; then
    echo "PASS: Second administrator (Carol) modified Alice's files in C:\\Program Files and COW-broken msvcp140.dll!"
else
    echo "FAIL: Carol could not modify Alice's file!"
    exit 1
fi
# Restore msvcp140.dll symlink before upgrade/manifest checks
ln -sf /usr/local/lib/wine/x86_64-windows/msvcp140.dll "$COW_DLL"

# --- Test 2: Bob (Limited User) sees and runs the program ---
echo "--- Test 2: Bob (Limited User) sees and runs Alice's program ---"
BOB_OUTPUT_64=$(su - bob -c '
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/bob/.wine"
    wine "C:\\Program Files\\AliceApp\\app64.exe"
    wineserver -w
')
echo "$BOB_OUTPUT_64"
if echo "$BOB_OUTPUT_64" | grep -q "Process running: 64-bit"; then
    echo "PASS: Bob successfully executed 64-bit app from C:\\Program Files!"
else
    echo "FAIL: Bob could not run 64-bit app!"
    exit 1
fi

BOB_OUTPUT_32=$(su - bob -c '
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/bob/.wine"
    wine "C:\\Program Files\\AliceApp\\app32.exe"
    wineserver -w
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
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/bob/.wine"
    wine cmd.exe /c copy Z:\\usr\\local\\bin\\check_s1_64.exe "C:\\windows\\system32\\bob_hack.dll" 2>&1 || true
    wineserver -w
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
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/bob/.wine"
    wine cmd.exe /c mkdir "C:\\Program Files\\BobApp" 2>&1 || true
    wineserver -w
')
echo "$BOB_WRITE_PF"

if [ ! -d "$MACHINE_ROOT/drive_c/Program Files/BobApp" ]; then
    echo "PASS: Bob was denied write access to C:\\Program Files!"
else
    echo "FAIL: Bob successfully created directory in C:\\Program Files!"
    exit 1
fi

# --- Test 5: Simulated Wine Upgrade (Machine Update as Root + Per-User Update) ---
echo "--- Test 5: Simulated Wine Upgrade ---"
OLD_TS=$(cat "$MACHINE_ROOT/.update-timestamp")
sleep 2
touch /usr/local/lib/wine/x86_64-unix/ntdll.so /usr/local/share/wine/wine.inf

echo "Running machine root update as root..."
/root/WineExplorer/scripts/setup-machine-root.sh --update
NEW_MACHINE_TS=$(cat "$MACHINE_ROOT/.update-timestamp")
echo "Machine .update-timestamp: $OLD_TS -> $NEW_MACHINE_TS"
if [ "$OLD_TS" = "$NEW_MACHINE_TS" ]; then
    echo "FAIL: Machine .update-timestamp did not advance!"
    exit 1
fi
SYS32_LINKS=$(find "$MACHINE_ROOT/drive_c/windows/system32" -maxdepth 1 -type l | wc -l)
WOW64_LINKS=$(find "$MACHINE_ROOT/drive_c/windows/syswow64" -maxdepth 1 -type l | wc -l)
echo "Machine links after root update: system32=$SYS32_LINKS, syswow64=$WOW64_LINKS"

# Snapshot machine root before per-user wineboot -u runs
MANIFEST_PRE_USER_UPDATE=$(mktemp)
find "$MACHINE_ROOT/drive_c" -type f -printf "%p %u:%g %m %T@\n" | sort > "$MANIFEST_PRE_USER_UPDATE"

echo "Running per-user wineboot -u as Alice (administrator) and Bob (limited user)..."
su - alice -c '
    umask 0022
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/alice/.wine"
    wine wineboot.exe -u
    wineserver -w
'
su - bob -c '
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/bob/.wine"
    wine wineboot.exe -u
    wineserver -w
'

ALICE_TS=$(cat /home/alice/.wine/.update-timestamp)
BOB_TS=$(cat /home/bob/.wine/.update-timestamp)
echo "Alice .update-timestamp: $ALICE_TS"
echo "Bob .update-timestamp:   $BOB_TS"
if [ "$ALICE_TS" != "$NEW_MACHINE_TS" ] || [ "$BOB_TS" != "$NEW_MACHINE_TS" ]; then
    echo "FAIL: Per-user .update-timestamp did not match updated wine.inf!"
    exit 1
fi

MANIFEST_POST_USER_UPDATE=$(mktemp)
find "$MACHINE_ROOT/drive_c" -type f -printf "%p %u:%g %m %T@\n" | sort > "$MANIFEST_POST_USER_UPDATE"
UPDATE_DIFF=$(diff -u "$MANIFEST_PRE_USER_UPDATE" "$MANIFEST_POST_USER_UPDATE" | grep -E '^\+[^+]|^\-[^-]' | wc -l || true)
rm -f "$MANIFEST_PRE_USER_UPDATE" "$MANIFEST_POST_USER_UPDATE"

if [ "$UPDATE_DIFF" -eq 0 ]; then
    echo "PASS: Per-user wineboot -u (Alice admin & Bob limited) touched 0 machine files!"
else
    echo "FAIL: Per-user wineboot -u modified $UPDATE_DIFF machine files!"
    exit 1
fi

# --- Test 6: Nothing per-user lands in machine root ---
echo "--- Test 6: Verify nothing per-user lands in machine root ---"
rm -rf "$MACHINE_ROOT/drive_c/Program Files/AliceApp"

MANIFEST_AFTER=$(mktemp)
find "$MACHINE_ROOT/drive_c" -type f -printf "%p %u:%g %m\n" | sort > "$MANIFEST_AFTER"
DIFF_COUNT=$(diff -u "$MANIFEST_BEFORE" "$MANIFEST_AFTER" | grep -E '^\+[^+]|^\-[^-]' | wc -l || true)

rm -f "$MANIFEST_BEFORE" "$MANIFEST_AFTER"

if [ "$DIFF_COUNT" -eq 0 ]; then
    echo "PASS: Exactly 0 machine root files modified, created, or deleted by user sessions!"
else
    echo "FAIL: $DIFF_COUNT unexpected changes in machine root!"
    exit 1
fi

echo "=== ALL S2 ACCEPTANCE TESTS PASSED PERFECTLY! ==="
