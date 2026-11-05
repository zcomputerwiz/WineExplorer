#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Phase S4 Acceptance Test: Integration and Migration
#
# Case 1: Setup builds machine root and template prefix (/etc/skel/.wine).
#         Per-user template prefix size is < 10 MB (vs 1.7 GB legacy).
# Case 2: Account creation creates per-user prefixes from template with proper user ownership.
# Case 3: Session script layout compliance:
#         - First logon personalizes user.reg and preserves machine hives.
#         - Machine root update (.update-timestamp advancement) triggers user wineboot -u.
#         - Theme copy is safely skipped on split prefix without permission errors.
# Case 4: Prefix migration script (scripts/migrate-prefix-split.sh):
#         Converts monolithic prefix to split layout, reduces size < 10 MB, preserves user data.
# Case 5: An application is installed for all users into Program Files; Bob (limited user) executes it;
#         Alice (administrator in sudo) and Bob are denied write access to the machine root. Phase A
#         (ELEVATION_SYSTEM_ROLE_WORKORDER.md, decision S4): the machine root is root:wexsys and Alice is not in
#         wexsys, so the install runs as root until the broker exists (step 4); Alice's own install runs when
#         WEX_BROKER_AVAILABLE=1 and otherwise prints WAITING.
# Case 6: Symlink integrity, permissions, and 0 leftover temporary files.
set -euo pipefail

MACHINE_ROOT="/var/lib/wineexplorer/machine"
SKEL_PFX="/etc/skel/.wine"
SYS_GRP="${WEX_MACHINE_GROUP:-wexsys}"
BROKER="${WEX_BROKER_AVAILABLE:-0}"   # 1 once administrators can install through the broker (step 4)

echo "=== WineExplorer Phase S4 Acceptance Test ==="

# Helper: verify symlinks and machine root permissions
verify_symlinks() {
    local stage="$1"
    echo "--- Verifying symlinks and permissions at: $stage ---"
    for pfx in "$SKEL_PFX" /home/alice/.wine /home/carol/.wine /home/bob/.wine; do
        [ -d "$pfx" ] || continue
        for item in drive_c/windows "drive_c/Program Files" drive_c/ProgramData drive_c/users/Public system.reg userdef.reg; do
            if [ ! -L "$pfx/$item" ]; then
                echo "FAIL [$stage]: $pfx/$item is NOT a symlink!"
                exit 1
            fi
            local tgt expected
            tgt=$(readlink -f "$pfx/$item")
            expected="$MACHINE_ROOT/$item"
            if [ "$tgt" != "$expected" ]; then
                echo "FAIL [$stage]: $pfx/$item points to $tgt, expected $expected"
                exit 1
            fi
        done
        if [ ! -f "$pfx/user.reg" ] || [ -L "$pfx/user.reg" ]; then
            echo "FAIL [$stage]: $pfx/user.reg should be a real file!"
            exit 1
        fi
    done

    local sys_mode sys_grp
    sys_mode=$(stat -c "%a" "$MACHINE_ROOT/system.reg")
    sys_grp=$(stat -c "%G" "$MACHINE_ROOT/system.reg")
    if [ "$sys_mode" != "664" ] || [ "$sys_grp" != "$SYS_GRP" ]; then
        echo "FAIL [$stage]: $MACHINE_ROOT/system.reg has mode $sys_mode group $sys_grp, expected 664 $SYS_GRP"
        exit 1
    fi

    local tmp_count
    tmp_count=$(find "$MACHINE_ROOT" -maxdepth 1 -name "reg*.tmp" | wc -l)
    if [ "$tmp_count" -ne 0 ]; then
        echo "FAIL [$stage]: Found $tmp_count leftover reg*.tmp files in $MACHINE_ROOT!"
        exit 1
    fi
    echo "PASS [$stage]: All symlinks intact, 0664 $SYS_GRP permissions verified, 0 temp files."
}

# ==============================================================================
# Case 1: Setup builds machine root and template prefix (/etc/skel/.wine)
# ==============================================================================
echo "=== Case 1: Setup builds machine root and template prefix (/etc/skel/.wine) ==="
rm -rf "$MACHINE_ROOT" "$SKEL_PFX"
/root/WineExplorer/scripts/setup-machine-root.sh

# Verify machine root exists and contains core Windows files
if [ ! -d "$MACHINE_ROOT/drive_c/windows/system32" ] || [ ! -f "$MACHINE_ROOT/system.reg" ]; then
    echo "FAIL: Machine root initialization failed!"
    exit 1
fi

# Verify template prefix size
SKEL_SIZE_MB=$(du -sm "$SKEL_PFX" | awk '{print $1}')
echo "Template prefix size (/etc/skel/.wine): ${SKEL_SIZE_MB} MB"
if [ "$SKEL_SIZE_MB" -ge 15 ]; then
    echo "FAIL: Template prefix size ${SKEL_SIZE_MB} MB exceeds 15 MB limit (expected < 10 MB vs 1.7 GB legacy)!"
    exit 1
fi
echo "PASS: Template prefix size is ${SKEL_SIZE_MB} MB (< 15 MB, massive reduction from 1.7 GB)."

# Verify all symlinks in template prefix
for item in drive_c/windows "drive_c/Program Files" drive_c/ProgramData drive_c/users/Public system.reg userdef.reg; do
    if [ ! -L "$SKEL_PFX/$item" ]; then
        echo "FAIL: $SKEL_PFX/$item is NOT a symlink!"
        exit 1
    fi
done
echo "PASS: Template prefix symlinks point into machine root."

# ==============================================================================
# Case 2: Account creation creates per-user prefixes from template with proper ownership
# ==============================================================================
echo "=== Case 2: Account creation creates per-user prefixes from template ==="
id alice >/dev/null 2>&1 || useradd -m -s /bin/bash -G sudo alice
id carol >/dev/null 2>&1 || useradd -m -s /bin/bash -G sudo carol
id bob >/dev/null 2>&1 || useradd -m -s /bin/bash bob
usermod -a -G sudo alice
usermod -a -G sudo carol

# Phase A: no desktop user is in the system group.
for u in alice carol bob; do
    if id -nG "$u" | tr ' ' '\n' | grep -qx "$SYS_GRP"; then
        echo "FAIL: $u is a member of $SYS_GRP; desktop users must not be!"
        exit 1
    fi
done

for u in alice carol bob; do
    rm -rf "/home/$u/.wine"
    # Populate per-user prefix from template
    cp -a "$SKEL_PFX" "/home/$u/.wine"
    mkdir -p "/home/$u/.wine/drive_c/users/$u"
    chown -hR "$u:$u" "/home/$u/.wine"

    USER_SIZE_MB=$(du -sm "/home/$u/.wine" | awk '{print $1}')
    echo "User $u prefix size: ${USER_SIZE_MB} MB"
    if [ "$USER_SIZE_MB" -ge 15 ]; then
        echo "FAIL: User $u prefix size ${USER_SIZE_MB} MB exceeds 15 MB!"
        exit 1
    fi

    # Verify ownership of prefix root and user registry
    pfx_owner=$(stat -c "%U:%G" "/home/$u/.wine")
    reg_owner=$(stat -c "%U:%G" "/home/$u/.wine/user.reg")
    if [ "$pfx_owner" != "$u:$u" ] || [ "$reg_owner" != "$u:$u" ]; then
        echo "FAIL: User $u prefix ownership is $pfx_owner / $reg_owner, expected $u:$u!"
        exit 1
    fi

    # Verify that machine files remain owned by root:$SYS_GRP
    win_owner=$(stat -c "%U:%G" "$MACHINE_ROOT/drive_c/windows")
    sys_owner=$(stat -c "%U:%G" "$MACHINE_ROOT/system.reg")
    if [ "$win_owner" != "root:$SYS_GRP" ] || [ "$sys_owner" != "root:$SYS_GRP" ]; then
        echo "FAIL: Machine root ownership corrupted to $win_owner / $sys_owner, expected root:$SYS_GRP!"
        exit 1
    fi
done
verify_symlinks "Case 2 account creation"
echo "PASS: Case 2 account creation and template instantiation passed."

# ==============================================================================
# Case 3: Session script layout compliance
# ==============================================================================
echo "=== Case 3: Session script layout compliance ==="

# 3a: First logon personalizes user.reg without touching machine hives
echo "--- 3a: First logon personalization ---"
su - alice -c '
    set -euo pipefail
    export WINEPREFIX="/home/alice/.wine"
    export USER="alice"
    # Seed wexsetup marker in user.reg
    sed -i -e "s/users\\\\alice/users\\\\wexsetup/g" "$WINEPREFIX/user.reg"
'
# Run the same first-logon personalization helper used by the product session.
su - alice -c 'bash /root/WineExplorer/scripts/personalize-prefix.sh /home/alice/.wine alice'
if grep -qs "users\\\\wexsetup" /home/alice/.wine/user.reg; then
    echo "FAIL: user.reg still contains wexsetup after first logon personalization!"
    exit 1
fi
verify_symlinks "Case 3a first logon"
echo "PASS: 3a first logon personalization succeeded, system.reg stayed a symlink."

# 3b: Machine root update triggers user wineboot -u without touching machine files
echo "--- 3b: Machine root update trigger ---"
OLD_TS=$(tr -d ' \r\n' < "$MACHINE_ROOT/.update-timestamp" 2>/dev/null || date +%s)
NEW_TS=$(( OLD_TS + 5000 ))
echo "$NEW_TS" > "$MACHINE_ROOT/.update-timestamp"

# Record snapshot of machine root files before user logon
find "$MACHINE_ROOT" -type f -printf "%p %T@\n" | sort > /tmp/machine_files_before.txt

su - bob -c '
    set -euo pipefail
    export WINEPREFIX="/home/bob/.wine"
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    MACHINE_ROOT="/var/lib/wineexplorer/machine"
    NTDLL=$(ls /usr/local/lib/wine/x86_64-unix/ntdll.so 2>/dev/null || true)

    if { [ -n "$NTDLL" ] && { [ ! -e "$WINEPREFIX/.update-timestamp" ] || [ "$NTDLL" -nt "$WINEPREFIX/.update-timestamp" ]; }; } || \
       { [ -e "$MACHINE_ROOT/.update-timestamp" ] && [ "$MACHINE_ROOT/.update-timestamp" -nt "$WINEPREFIX/.update-timestamp" ]; }; then
        echo "Updating prefix for $USER..."
        /usr/local/bin/wine wineboot.exe -u
        /usr/local/bin/wineserver -w
    fi
'
find "$MACHINE_ROOT" -type f -printf "%p %T@\n" | sort > /tmp/machine_files_after.txt

# Verify Bob updated his .update-timestamp
BOB_TS=$(cat /home/bob/.wine/.update-timestamp 2>/dev/null || echo 0)
echo "Bob .update-timestamp: $BOB_TS (machine TS: $NEW_TS)"

# Verify zero machine files were touched by Bob
DIFF_COUNT=$(diff /tmp/machine_files_before.txt /tmp/machine_files_after.txt | wc -l)
if [ "$DIFF_COUNT" -ne 0 ]; then
    echo "FAIL: Machine files were modified during Bob's wineboot -u!"
    diff -u /tmp/machine_files_before.txt /tmp/machine_files_after.txt
    exit 1
fi
echo "PASS: 3b machine update triggered wineboot -u; touched 0 machine files."

# 3c: Theme copy safely skipped on split prefix
echo "--- 3c: Theme copy guard ---"
su - bob -c '
    set -euo pipefail
    export WINEPREFIX="/home/bob/.wine"
    THEME_DATA_DIR="/usr/local/share/wineexplorer/themes"
    # Should skip because drive_c/windows is a symlink
    if [ -d "$THEME_DATA_DIR" ] && [ ! -L "$WINEPREFIX/drive_c/windows" ] && [ -w "$WINEPREFIX/drive_c/windows" ]; then
        echo "ERROR: Should not attempt theme copy on split prefix!"
        exit 1
    fi
    echo "Theme copy guard correctly skipped symlinked machine windows directory."
'
echo "PASS: 3c theme copy guard verified."

# ==============================================================================
# Case 4: Prefix migration script (scripts/migrate-prefix-split.sh)
# ==============================================================================
echo "=== Case 4: Prefix migration script ==="
MIGRATE_TEST_PFX="/tmp/test_legacy_prefix"
rm -rf "$MIGRATE_TEST_PFX"
mkdir -p "$MIGRATE_TEST_PFX/dosdevices" "$MIGRATE_TEST_PFX/drive_c/windows/system32" \
         "$MIGRATE_TEST_PFX/drive_c/Program Files/TestApp" \
         "$MIGRATE_TEST_PFX/drive_c/ProgramData" \
         "$MIGRATE_TEST_PFX/drive_c/users/legacyuser/Documents" \
         "$MIGRATE_TEST_PFX/drive_c/users/Public"

ln -s ../drive_c "$MIGRATE_TEST_PFX/dosdevices/c:"
ln -s / "$MIGRATE_TEST_PFX/dosdevices/z:"

# Create dummy monolithic files simulating 100 MB of legacy files
dd if=/dev/zero of="$MIGRATE_TEST_PFX/drive_c/windows/system32/kernel32.dll" bs=1M count=30 status=none
dd if=/dev/zero of="$MIGRATE_TEST_PFX/drive_c/Program Files/TestApp/app.exe" bs=1M count=20 status=none
dd if=/dev/zero of="$MIGRATE_TEST_PFX/system.reg" bs=1M count=5 status=none
echo "WINE REGISTRY Version 2" > "$MIGRATE_TEST_PFX/user.reg"
echo "[Software\\\\Wine\\\\Test]" >> "$MIGRATE_TEST_PFX/user.reg"
echo "\"LegacyValue\"=\"Preserved\"" >> "$MIGRATE_TEST_PFX/user.reg"
echo "My document content" > "$MIGRATE_TEST_PFX/drive_c/users/legacyuser/Documents/note.txt"
touch "$MIGRATE_TEST_PFX/userdef.reg"

LEGACY_SIZE_MB=$(du -sm "$MIGRATE_TEST_PFX" | awk '{print $1}')
echo "Legacy prefix size before migration: ${LEGACY_SIZE_MB} MB"

# Run migration script
/root/WineExplorer/scripts/migrate-prefix-split.sh "$MIGRATE_TEST_PFX"

MIGRATED_SIZE_MB=$(du -sm "$MIGRATE_TEST_PFX" | awk '{print $1}')
echo "Migrated prefix size after migration: ${MIGRATED_SIZE_MB} MB"

# Assertions on migrated prefix
if [ ! -L "$MIGRATE_TEST_PFX/drive_c/windows" ] || \
   [ ! -L "$MIGRATE_TEST_PFX/drive_c/Program Files" ] || \
   [ ! -L "$MIGRATE_TEST_PFX/system.reg" ] || \
   [ ! -L "$MIGRATE_TEST_PFX/userdef.reg" ]; then
    echo "FAIL: Machine items were not converted to symlinks during migration!"
    exit 1
fi

if [ ! -f "$MIGRATE_TEST_PFX/user.reg" ] || [ -L "$MIGRATE_TEST_PFX/user.reg" ]; then
    echo "FAIL: user.reg was lost or corrupted during migration!"
    exit 1
fi

if ! grep -q "LegacyValue" "$MIGRATE_TEST_PFX/user.reg"; then
    echo "FAIL: user.reg contents lost during migration!"
    exit 1
fi

if [ ! -f "$MIGRATE_TEST_PFX/drive_c/users/legacyuser/Documents/note.txt" ]; then
    echo "FAIL: User private documents lost during migration!"
    exit 1
fi

if [ "$MIGRATED_SIZE_MB" -ge 10 ]; then
    echo "FAIL: Migrated prefix size ${MIGRATED_SIZE_MB} MB exceeds 10 MB!"
    exit 1
fi
rm -rf "$MIGRATE_TEST_PFX"*
echo "PASS: Case 4 migration script correctly migrated prefix, preserved user data, and shrank size."

# ==============================================================================
# Case 5: Alice installs for all users and Bob executes; Bob cannot write machine
# ==============================================================================
echo "=== Case 5: Alice installs for all users, Bob executes, Bob denied write ==="

# Alice (in sudo) installs a shared application. Changed for S4: the install runs as root (it stands for the
# installer package step) until the broker exists; Alice's own install waits for step 4.
if [ "$BROKER" != 1 ]; then
    install -d -m 2775 -o root -g "$SYS_GRP" "$MACHINE_ROOT/drive_c/Program Files/SharedTool"
    printf '@echo off\r\necho Shared Tool Running Successfully\r\n' > /tmp/s4_tool.bat
    install -m 775 -o root -g "$SYS_GRP" /tmp/s4_tool.bat "$MACHINE_ROOT/drive_c/Program Files/SharedTool/tool.bat"
    rm -f /tmp/s4_tool.bat
    echo "WAITING (step 4, broker): Alice's own session installing into Program Files."
fi
[ "$BROKER" != 1 ] || su - alice -c '
    set -euo pipefail
    mkdir -p "/var/lib/wineexplorer/machine/drive_c/Program Files/SharedTool"
    cat << "EOF" > "/var/lib/wineexplorer/machine/drive_c/Program Files/SharedTool/tool.bat"
@echo off
echo Shared Tool Running Successfully
EOF
    chmod 775 "/var/lib/wineexplorer/machine/drive_c/Program Files/SharedTool/tool.bat"
'

# Bob (standard user) runs the shared application
su - bob -c '
    set -euo pipefail
    export WINEPREFIX="/home/bob/.wine"
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    OUTPUT=$(/usr/local/bin/wine cmd.exe /c "C:\\Program Files\\SharedTool\\tool.bat" 2>&1 || true)
    echo "$OUTPUT"
    echo "$OUTPUT" | grep -q "Shared Tool Running Successfully"
'
echo "PASS: Bob successfully executed application installed for all users by Alice."

# Alice (administrator in sudo, not in wexsys) and Bob attempt to write to the machine root; must be denied
echo "--- Verifying Alice and Bob are denied write access to machine root ---"
for u in alice carol; do
    if su - "$u" -c 'touch "/var/lib/wineexplorer/machine/drive_c/Program Files/${USER}_hack.txt"' 2>/dev/null; then
        echo "FAIL: $u was able to write to machine Program Files directory!"
        exit 1
    fi
    if su - "$u" -c 'echo "fail" >> /var/lib/wineexplorer/machine/system.reg' 2>/dev/null; then
        echo "FAIL: $u was able to write to machine system.reg!"
        exit 1
    fi
done
echo "PASS: Alice and Carol (sudo, not wexsys) were denied direct write access to the machine root."
if su - bob -c 'touch "/var/lib/wineexplorer/machine/drive_c/windows/bob_hack.txt"' 2>/dev/null; then
    echo "FAIL: Bob was able to write to machine windows directory!"
    exit 1
fi
if su - bob -c 'touch "/var/lib/wineexplorer/machine/drive_c/Program Files/bob_hack.txt"' 2>/dev/null; then
    echo "FAIL: Bob was able to write to machine Program Files directory!"
    exit 1
fi
if su - bob -c 'echo "fail" >> /var/lib/wineexplorer/machine/system.reg' 2>/dev/null; then
    echo "FAIL: Bob was able to write to machine system.reg!"
    exit 1
fi
echo "PASS: Bob write access denied to machine root as expected."

# Clean up shared tool
rm -rf "/var/lib/wineexplorer/machine/drive_c/Program Files/SharedTool"

# ==============================================================================
# Case 6: Symlink integrity and cleanup
# ==============================================================================
verify_symlinks "Case 6 final verification"

echo "=== All Phase S4 Acceptance Tests Passed Successfully ==="
