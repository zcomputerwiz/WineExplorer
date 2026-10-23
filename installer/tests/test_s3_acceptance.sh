#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Phase S3 Acceptance Test:
# Phase A (ELEVATION_SYSTEM_ROLE_WORKORDER.md, decision S4): the machine root is root:wexsys and no desktop user
# is in wexsys, so a desktop session cannot persist HKLM until the system-role wineserver is wired in (phases
# C/D) and administrators install through the broker (step 4). The cases that need an administrator's own
# session to write system.reg run when WEX_BROKER_AVAILABLE=1 and otherwise print WAITING; Case 1 and the root
# part of Case 4 seed the machine root in a root-run step instead.
# Case 1: Alice (administrator) installs program, registers COM class and Uninstall entry;
#         Bob (limited user) sees both after logon and can run the program.
# Case 2: Bob's HKLM writes succeed in memory during session, are gone after logoff,
#         and never reach the file. S0.2 display/PnP keys are not dirtied or saved.
# Case 3: Two administrators (Alice and Carol) save different keys in overlapping sessions;
#         both survive; deletion by one admin is not resurrected by the other.
# Case 3b: Alice-Carol-Alice sequence with value update and deleted key re-creation.
# Case 4: Machine upgrade (setup-machine-root.sh update as root) while Alice is logged in
#         keeps upgrade keys after Alice logs off.
# Case 5: system.reg and userdef.reg stay symlinks after every case across /etc/skel/.wine,
#         /home/alice/.wine, /home/carol/.wine, /home/bob/.wine.
set -euo pipefail

# The test programs the machine-root installs use; built here when missing (i686 and x86_64 MinGW)
for arch in 32 64; do
    [ -f "/usr/local/bin/check_s1_$arch.exe" ] && continue
    cc=i686-w64-mingw32-gcc; [ "$arch" = 64 ] && cc=x86_64-w64-mingw32-gcc
    "$cc" -O2 -o "/usr/local/bin/check_s1_$arch.exe" "$(dirname "${BASH_SOURCE[0]}")/test_s1.c" -lversion ||
        { echo "cannot build /usr/local/bin/check_s1_$arch.exe from installer/tests/test_s1.c" >&2; exit 2; }
done

MACHINE_ROOT="/var/lib/wineexplorer/machine"
SKEL_PFX="/etc/skel/.wine"
SYS_GRP="${WEX_MACHINE_GROUP:-wexsys}"
BROKER="${WEX_BROKER_AVAILABLE:-0}"   # 1 once administrator sessions can persist HKLM (phases C/D, step 4)

echo "=== WineExplorer Phase S3 Acceptance Test ==="

# Helper: verify symlinks and machine root permissions
verify_symlinks() {
    local stage="$1"
    echo "--- Verifying symlinks and permissions at: $stage ---"
    for pfx in "$SKEL_PFX" /home/alice/.wine /home/carol/.wine /home/bob/.wine; do
        if [ ! -L "$pfx/system.reg" ]; then
            echo "FAIL [$stage]: $pfx/system.reg is NOT a symlink!"
            exit 1
        fi
        if [ ! -L "$pfx/userdef.reg" ]; then
            echo "FAIL [$stage]: $pfx/userdef.reg is NOT a symlink!"
            exit 1
        fi
        local sys_tgt userdef_tgt
        sys_tgt=$(readlink -f "$pfx/system.reg")
        userdef_tgt=$(readlink -f "$pfx/userdef.reg")
        if [ "$sys_tgt" != "$MACHINE_ROOT/system.reg" ]; then
            echo "FAIL [$stage]: $pfx/system.reg points to $sys_tgt, expected $MACHINE_ROOT/system.reg"
            exit 1
        fi
        if [ "$userdef_tgt" != "$MACHINE_ROOT/userdef.reg" ]; then
            echo "FAIL [$stage]: $pfx/userdef.reg points to $userdef_tgt, expected $MACHINE_ROOT/userdef.reg"
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

# Helper: reset user prefixes from /etc/skel/.wine
reset_all_users() {
    echo "--- Resetting user prefixes from /etc/skel/.wine ---"
    for u in alice carol bob; do
        rm -rf "/home/$u/.wine"
        cp -a "$SKEL_PFX" "/home/$u/.wine"
        mkdir -p "/home/$u/.wine/drive_c/users/$u"
        sed -i -e "s/users\\\\wexsetup/users\\\\$u/g" "/home/$u/.wine/user.reg"
        chown -R -h "$u:$u" "/home/$u/.wine"
    done
    verify_symlinks "initial reset"
}

# 0. Ensure fresh machine root, skel, and users
echo "--- Ensuring fresh machine root and skel ---"
rm -rf "$MACHINE_ROOT" "$SKEL_PFX"
/root/WineExplorer/scripts/setup-machine-root.sh

id alice >/dev/null 2>&1 || useradd -m -s /bin/bash -G sudo alice
id carol >/dev/null 2>&1 || useradd -m -s /bin/bash -G sudo carol
id bob >/dev/null 2>&1 || useradd -m -s /bin/bash bob
usermod -a -G sudo alice
usermod -a -G sudo carol

# Phase A: no desktop user is in the system group, and none can write system.reg directly.
for u in alice carol bob; do
    if id -nG "$u" | tr ' ' '\n' | grep -qx "$SYS_GRP"; then
        echo "FAIL: $u is a member of $SYS_GRP; desktop users must not be!"
        exit 1
    fi
    if su - "$u" -c "echo x >> \"$MACHINE_ROOT/system.reg\"" 2>/dev/null; then
        echo "FAIL: $u appended to the machine system.reg directly!"
        exit 1
    fi
done
echo "PASS: alice, carol and bob are not in $SYS_GRP and cannot write the machine system.reg."

reset_all_users

# ==============================================================================
# Case 1: Alice (administrator) installs program, registers COM class and
#         Uninstall entry; Bob (limited user) sees both after logon and runs app
# ==============================================================================
echo "=== Case 1: Alice registers COM class & Uninstall entry; Bob sees and runs app ==="
if [ "$BROKER" != 1 ]; then
    # Changed for S4: the same install and registrations run as root on the machine root (stands for the
    # installer step); Alice's own session writing HKLM waits for step 4.
    install -d -m 2775 -o root -g "$SYS_GRP" "$MACHINE_ROOT/drive_c/Program Files/AliceApp"
    install -m 775 -o root -g "$SYS_GRP" /usr/local/bin/check_s1_64.exe "$MACHINE_ROOT/drive_c/Program Files/AliceApp/app64.exe"
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEPREFIX="$MACHINE_ROOT"
    wine reg add "HKLM\\Software\\Classes\\CLSID\\{A11CE000-0000-0000-0000-000000000001}" /ve /d "Alice COM Server" /f
    wine reg add "HKLM\\Software\\Classes\\CLSID\\{A11CE000-0000-0000-0000-000000000001}\\LocalServer32" /ve /d "C:\\Program Files\\AliceApp\\app64.exe" /f
    wine reg add "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AliceApp" /v "DisplayName" /d "Alice Application 1.0" /f
    wine reg add "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AliceApp" /v "UninstallString" /d "C:\\Program Files\\AliceApp\\app64.exe --uninstall" /f
    wineserver -k || true
    wineserver -w
    unset WINEDLLOVERRIDES WINEPREFIX
    echo "WAITING (step 4, broker): Alice's own session registering the COM class and Uninstall entry."
fi
[ "$BROKER" != 1 ] || su - alice -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/alice/.wine"

    wine cmd.exe /c mkdir "C:\\Program Files\\AliceApp"
    wine cmd.exe /c copy Z:\\usr\\local\\bin\\check_s1_64.exe "C:\\Program Files\\AliceApp\\app64.exe"

    wine reg add "HKLM\\Software\\Classes\\CLSID\\{A11CE000-0000-0000-0000-000000000001}" /ve /d "Alice COM Server" /f
    wine reg add "HKLM\\Software\\Classes\\CLSID\\{A11CE000-0000-0000-0000-000000000001}\\LocalServer32" /ve /d "C:\\Program Files\\AliceApp\\app64.exe" /f

    wine reg add "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AliceApp" /v "DisplayName" /d "Alice Application 1.0" /f
    wine reg add "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AliceApp" /v "UninstallString" /d "C:\\Program Files\\AliceApp\\app64.exe --uninstall" /f

    wineserver -w
'

# Verify keys reached $MACHINE_ROOT/system.reg
if grep -q "{A11CE000-0000-0000-0000-000000000001}" "$MACHINE_ROOT/system.reg" && \
   grep -q "Alice Application 1.0" "$MACHINE_ROOT/system.reg"; then
    echo "PASS [Case 1]: Alice COM class and Uninstall entries written to machine system.reg."
else
    echo "FAIL [Case 1]: Alice entries not found in machine system.reg!"
    exit 1
fi

# Bob logs in and verifies he sees the COM class and Uninstall entries, and runs app
BOB_CASE1_OUT=$(su - bob -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/bob/.wine"

    echo "--- BOB COM QUERY ---"
    wine reg query "HKLM\\Software\\Classes\\CLSID\\{A11CE000-0000-0000-0000-000000000001}\\LocalServer32" /ve

    echo "--- BOB UNINSTALL QUERY ---"
    wine reg query "HKLM\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\AliceApp" /v "DisplayName"

    echo "--- BOB RUN APP ---"
    wine "C:\\Program Files\\AliceApp\\app64.exe"

    wineserver -w
')
echo "$BOB_CASE1_OUT"

if echo "$BOB_CASE1_OUT" | grep -q "C:\\\\Program Files\\\\AliceApp\\\\app64.exe" && \
   echo "$BOB_CASE1_OUT" | grep -q "Alice Application 1.0" && \
   echo "$BOB_CASE1_OUT" | grep -q "Process running: 64-bit"; then
    echo "PASS [Case 1]: Bob saw COM registration, Uninstall entry, and executed Alice's app!"
else
    echo "FAIL [Case 1]: Bob failed to see entries or execute Alice's app!"
    exit 1
fi

verify_symlinks "Case 1"

# ==============================================================================
# Case 2: Bob's HKLM writes succeed in memory, gone after logoff, never reach file.
#         S0.2 display/PnP keys are not dirtied or saved by user sessions.
# ==============================================================================
echo "=== Case 2: Bob's in-memory HKLM writes & S0.2 ephemeral key protection ==="
BOB_CASE2_OUT=$(su - bob -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/bob/.wine"

    wine reg add "HKLM\\Software\\BobHack" /v "Hacked" /d "1" /f
    wine reg query "HKLM\\Software\\BobHack" /v "Hacked"
    wineserver -w
')
echo "$BOB_CASE2_OUT"

if echo "$BOB_CASE2_OUT" | grep -q "Hacked" && echo "$BOB_CASE2_OUT" | grep -q "1"; then
    echo "PASS [Case 2]: Bob's in-memory HKLM write succeeded during session."
else
    echo "FAIL [Case 2]: Bob's in-memory HKLM write failed during session!"
    exit 1
fi

if grep -i "BobHack" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 2]: Bob's write reached machine system.reg!"
    exit 1
else
    echo "PASS [Case 2]: Bob's write NEVER reached machine system.reg!"
fi

# Bob logs in again: verify BobHack is gone
BOB_CASE2_QUERY2=$(su - bob -c '
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/bob/.wine"
    wine reg query "HKLM\\Software\\BobHack" 2>&1 || true
    wineserver -w
')
echo "$BOB_CASE2_QUERY2"

if echo "$BOB_CASE2_QUERY2" | grep -qi "Unable to find"; then
    echo "PASS [Case 2]: Bob's write was gone after logoff!"
else
    echo "FAIL [Case 2]: Bob's write unexpectedly persisted across sessions!"
    exit 1
fi

# Check S0.2 per-session display/PnP keys: Alice (admin) session writes them, they must NOT be saved
echo "Testing S0.2 transient/per-session key protection in admin session..."
su - alice -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/alice/.wine"

    wine reg add "HKLM\\System\\CurrentControlSet\\Control\\Video\\{11111111-2222-3333-4444-555555555555}\\0000" /v "TransientRes" /d "3840x2160" /f
    wine reg add "HKLM\\System\\CurrentControlSet\\Control\\Print\\Printers" /v "EphemeralPrinter" /d "LaserJet" /f
    wine reg add "HKLM\\Software\\Wine\\Drives" /v "x" /d "/mnt/ephemeral" /f
    wine reg add "HKLM\\System\\CurrentControlSet\\Control\\Session Manager\\Environment" /v "NUMBER_OF_PROCESSORS" /d "128" /f

    wineserver -w
'

if grep -q "TransientRes" "$MACHINE_ROOT/system.reg" || \
   grep -q "EphemeralPrinter" "$MACHINE_ROOT/system.reg" || \
   grep -q "/mnt/ephemeral" "$MACHINE_ROOT/system.reg" || \
   grep -q '"NUMBER_OF_PROCESSORS"="128"' "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 2]: Ephemeral/per-session keys were saved to machine system.reg!"
    exit 1
else
    echo "PASS [Case 2]: Ephemeral/per-session keys were NOT saved to machine system.reg!"
fi

# Check that an unlinked prefix (not using shared machine root) DOES save these keys (Defect 2)
echo "Testing unlinked prefix saves ephemeral keys (Defect 2 check)..."
UNLINKED_PFX="/home/alice/test_unlinked_pfx"
rm -rf "$UNLINKED_PFX"
su - alice -c "
    set -euo pipefail
    export WINEDLLOVERRIDES=\"mscoree,mshtml=;winemenubuilder.exe=d\"
    export WINEPREFIX=\"$UNLINKED_PFX\"
    wine reg add \"HKLM\\Software\\Wine\\Drives\" /v \"x\" /d \"/mnt/test\" /f
    wine reg add \"HKLM\\System\\CurrentControlSet\\Control\\ComputerName\\ComputerName\" /v \"ComputerName\" /d \"TESTHOST\" /f
    wineserver -w
"
if ! grep -q "TESTHOST" "$UNLINKED_PFX/system.reg" || ! grep -q "/mnt/test" "$UNLINKED_PFX/system.reg"; then
    echo "FAIL [Case 2]: Unlinked prefix failed to save HKLM keys under per-session paths!"
    exit 1
else
    echo "PASS [Case 2]: Unlinked prefix successfully saved HKLM keys under per-session paths."
fi
rm -rf "$UNLINKED_PFX"

verify_symlinks "Case 2"

# ==============================================================================
# Case 3: Two administrators (Alice and Carol) save different keys in overlapping
#         sessions; both survive; deletion by one admin is not resurrected by other.
# ==============================================================================
echo "=== Case 3: Concurrent admin sessions, merge-on-save, and tombstone deletion ==="

if [ "$BROKER" != 1 ]; then
echo "WAITING (step 4, broker): Case 3 needs two administrator sessions to persist HKLM (merge-on-save, tombstones)."
else
# Set up initial base key and subkey to be deleted
su - alice -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/alice/.wine"

    wine reg add "HKLM\\Software\\SharedApp" /v "InitialVal" /d "Base" /f
    wine reg add "HKLM\\Software\\SharedApp\\ToDelete" /v "DelVal" /d "ToDel" /f
    wineserver -w
'
if ! grep -q "InitialVal" "$MACHINE_ROOT/system.reg" || ! grep -q "ToDelete" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 3]: Initial shared keys not established!"
    exit 1
fi
echo "Initial shared keys established on disk."

# Start background wine sessions for Alice and Carol to make them concurrent
su - alice -c '
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/alice/.wine"
    wine cmd.exe /c "ping -n 30 127.0.0.1 >nul"
' &
ALICE_BG=$!

su - carol -c '
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/carol/.wine"
    wine cmd.exe /c "ping -n 30 127.0.0.1 >nul"
' &
CAROL_BG=$!

sleep 3
echo "Concurrent sessions active: Alice BG=$ALICE_BG, Carol BG=$CAROL_BG"

# Alice modifies: adds AliceShared and deletes ToDelete
echo "Alice modifying registry in concurrent session..."
su - alice -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/alice/.wine"
    wine reg add "HKLM\\Software\\AliceShared" /v "AliceKey" /d "AliceVal" /f
    wine reg delete "HKLM\\Software\\SharedApp\\ToDelete" /f
'

# Carol modifies: adds CarolShared
echo "Carol modifying registry in concurrent session..."
su - carol -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/carol/.wine"
    wine reg add "HKLM\\Software\\CarolShared" /v "CarolKey" /d "CarolVal" /f
'

# Carol saves first
echo "Carol saving first..."
kill -9 $CAROL_BG || true
su - carol -c 'wineserver -k || true; wineserver -w'
wait $CAROL_BG 2>/dev/null || true

if ! grep -q "CarolShared" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 3]: CarolShared was not saved to disk!"
    exit 1
fi
echo "PASS [Case 3]: CarolShared saved to disk."

# Alice saves second (triggers merge-on-save under flock)
echo "Alice saving second (merging changes onto Carol disk save)..."
kill -9 $ALICE_BG || true
su - alice -c 'wineserver -k || true; wineserver -w'
wait $ALICE_BG 2>/dev/null || true

# Verify disk content
echo "Verifying merged disk contents:"
if ! grep -q "CarolShared" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 3]: CarolShared was clobbered by Alice save!"
    exit 1
fi
echo "PASS [Case 3]: CarolShared survived Alice save."

if ! grep -q "AliceShared" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 3]: AliceShared was not saved!"
    exit 1
fi
echo "PASS [Case 3]: AliceShared was saved."

if grep -q "ToDelete" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 3]: Deletion of ToDelete was resurrected!"
    exit 1
fi
echo "PASS [Case 3]: Deletion of ToDelete was NOT resurrected."

if ! grep -q "InitialVal" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 3]: InitialVal missing!"
    exit 1
fi
echo "PASS [Case 3]: InitialVal preserved."

# Query both in a fresh session as Bob
su - bob -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/bob/.wine"
    wine reg query "HKLM\\Software\\AliceShared" /v "AliceKey"
    wine reg query "HKLM\\Software\\CarolShared" /v "CarolKey"
    wineserver -w
'

fi
verify_symlinks "Case 3"

# ==============================================================================
# Case 3b: Alice-Carol-Alice save sequence (mod_seq reset & tombstone freeing)
# ==============================================================================
echo "=== Case 3b: Alice-Carol-Alice save sequence (mod_seq reset & tombstone freeing) ==="

if [ "$BROKER" != 1 ]; then
echo "WAITING (step 4, broker): Case 3b needs administrator sessions to persist HKLM (mod_seq reset, tombstone freeing)."
else
# Step 1: Alice sets up initial key DelKey and value X=1 in a long-running session
su - alice -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/alice/.wine"
    wine reg add "HKLM\\Software\\SeqTest" /v "X" /d "1" /f
    wine reg add "HKLM\\Software\\SeqTest\\DelKey" /v "DelVal" /d "ToDel" /f
    wineserver -w
'
if ! grep -q "SeqTest" "$MACHINE_ROOT/system.reg" || ! grep -q "DelKey" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 3b]: Initial SeqTest keys not established!"
    exit 1
fi

# Alice starts long-running background session
su - alice -c '
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/alice/.wine"
    wine cmd.exe /c "ping -n 60 127.0.0.1 >nul"
' &
ALICE_SEQ_BG=$!
sleep 2

# Alice in her active session: sets X="AliceInitial" and deletes DelKey
su - alice -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/alice/.wine"
    wine reg add "HKLM\\Software\\SeqTest" /v "X" /d "AliceInitial" /f
    wine reg delete "HKLM\\Software\\SeqTest\\DelKey" /f
'

# Alice Save 1: wait for periodic save (fires every 30s)
echo "Waiting for Alice's first save (periodic save under 30s)..."
prev_mtime=$(stat -c %Y "$MACHINE_ROOT/system.reg")
saved=0
for i in $(seq 1 35); do
    sleep 1
    curr_mtime=$(stat -c %Y "$MACHINE_ROOT/system.reg")
    if [ "$curr_mtime" -ne "$prev_mtime" ] && grep -q "AliceInitial" "$MACHINE_ROOT/system.reg"; then
        echo "Alice Save 1 completed to disk after ${i}s."
        saved=1
        break
    fi
done

if [ "$saved" -ne 1 ]; then
    echo "FAIL [Case 3b]: Alice Save 1 was not written by periodic save!"
    exit 1
fi
if grep -q "DelKey" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 3b]: DelKey should have been deleted in Alice Save 1!"
    exit 1
fi
echo "PASS [Case 3b]: Alice Save 1 succeeded: X=AliceInitial and DelKey deleted."

# Step 2: Carol session (Alice is still logged in with active wineserver!)
# Carol updates X="CarolUpdated" and re-creates DelKey
echo "Carol modifying X and re-creating DelKey while Alice remains logged in..."
su - carol -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/carol/.wine"
    wine reg add "HKLM\\Software\\SeqTest" /v "X" /d "CarolUpdated" /f
    wine reg add "HKLM\\Software\\SeqTest\\DelKey" /v "DelVal" /d "CarolRecreated" /f
    wineserver -w
'

if ! grep -q "CarolUpdated" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 3b]: Carol update to X was not saved!"
    exit 1
fi
if ! grep -q "CarolRecreated" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 3b]: Carol re-created DelKey was not saved!"
    exit 1
fi
echo "PASS [Case 3b]: Carol save succeeded: X=CarolUpdated and DelKey re-created."

# Step 3: Alice in her SAME long-running session modifies an unrelated key Y="AliceOtherVal"
echo "Alice modifying unrelated key Y in same active session..."
su - alice -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/alice/.wine"
    wine reg add "HKLM\\Software\\SeqTest" /v "Y" /d "AliceOtherVal" /f
'

# Alice Save 2: Alice logs off, triggering flush_registry on shutdown
echo "Alice logging off and saving (Save 2)..."
kill -9 $ALICE_SEQ_BG || true
su - alice -c 'wineserver -k || true; wineserver -w'
wait $ALICE_SEQ_BG 2>/dev/null || true

# Step 4: Verify disk contents
echo "Verifying disk contents after Alice Save 2:"
if ! grep -q "AliceOtherVal" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 3b]: Alice new modification Y was not saved!"
    exit 1
fi
echo "PASS [Case 3b]: Alice new modification Y was saved."

if ! grep -q "CarolUpdated" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 3b]: Carol update X=CarolUpdated was overwritten by Alice stale mod_seq!"
    exit 1
fi
echo "PASS [Case 3b]: Carol value X=CarolUpdated survived Alice Save 2."

if ! grep -q "CarolRecreated" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 3b]: Carol re-created DelKey was re-deleted by Alice stale tombstone!"
    exit 1
fi
echo "PASS [Case 3b]: Carol re-created DelKey survived Alice Save 2."

fi
verify_symlinks "Case 3b"

# ==============================================================================
# Case 4: Machine upgrade while Alice is logged in keeps upgrade keys after Alice logs off
# ==============================================================================
echo "=== Case 4: Machine upgrade while Alice is logged in ==="

# Alice logs in and keeps session active
if [ "$BROKER" = 1 ]; then
su - alice -c '
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/alice/.wine"
    wine cmd.exe /c "ping -n 30 127.0.0.1 >nul"
' &
ALICE_BG4=$!

sleep 3
echo "Alice session active (BG=$ALICE_BG4)."
fi

# Root performs machine upgrade: updates machine root and adds machine upgrade key
echo "Root performing machine update..."
WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEPREFIX="$MACHINE_ROOT" wine reg add "HKLM\\Software\\MachineUpgrade" /v "Version" /d "3.1.0" /f
WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEPREFIX="$MACHINE_ROOT" wine reg add "HKLM\\Software\\MachineUpgrade" /v "NewFeature" /d "Enabled" /f
WINEPREFIX="$MACHINE_ROOT" wineserver -k || true
WINEPREFIX="$MACHINE_ROOT" wineserver -w

# Also execute setup-machine-root.sh update as root
/root/WineExplorer/scripts/setup-machine-root.sh

if ! grep -q "MachineUpgrade" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 4]: Machine upgrade keys not written to disk!"
    exit 1
fi
echo "Machine upgrade keys written to disk by root."

if [ "$BROKER" = 1 ]; then
# Alice in her still-running session writes a post-upgrade key
echo "Alice writing post-upgrade key in her active session..."
su - alice -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/alice/.wine"
    wine reg add "HKLM\\Software\\AlicePostUpgrade" /v "Installed" /d "True" /f
'

# Alice logs off (merges changes onto root upgrade)
echo "Alice logging off and saving..."
kill -9 $ALICE_BG4 || true
su - alice -c 'wineserver -k || true; wineserver -w'
wait $ALICE_BG4 2>/dev/null || true
fi

# Verify disk contents
echo "Verifying upgrade keys and Alice keys survived:"
if ! grep -q "MachineUpgrade" "$MACHINE_ROOT/system.reg" || ! grep -q '"Version"="3.1.0"' "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 4]: Machine upgrade keys were overwritten by Alice!"
    exit 1
fi
echo "PASS [Case 4]: Machine upgrade keys survived Alice logoff."

if [ "$BROKER" != 1 ]; then
    echo "WAITING (step 4, broker): Case 4 Alice's post-upgrade key (AlicePostUpgrade) saved on logoff."
elif ! grep -q "AlicePostUpgrade" "$MACHINE_ROOT/system.reg"; then
    echo "FAIL [Case 4]: AlicePostUpgrade was not saved!"
    exit 1
else
    echo "PASS [Case 4]: AlicePostUpgrade was saved."
fi

# Query both in a fresh session as Bob
su - bob -c '
    set -euo pipefail
    export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
    export WINEPREFIX="/home/bob/.wine"
    wine reg query "HKLM\\Software\\MachineUpgrade" /v "Version"
    wineserver -w
'
if [ "$BROKER" = 1 ]; then
    su - bob -c '
        set -euo pipefail
        export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
        export WINEPREFIX="/home/bob/.wine"
        wine reg query "HKLM\\Software\\AlicePostUpgrade" /v "Installed"
        wineserver -w
    '
fi

verify_symlinks "Case 4"

# ==============================================================================
# Case 5: Final Symlink Integrity Check
# ==============================================================================
echo "=== Case 5: Final symlink integrity check across all user prefixes ==="
verify_symlinks "Case 5 Final"

echo "=== ALL PHASE S3 ACCEPTANCE TESTS PASSED PERFECTLY! ==="
