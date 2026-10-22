#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Test machine root metadata capture, apply, rollback, and write denial in a disposable fixture.
set -euo pipefail

FIXTURE="/tmp/disposable-machine-root-$$"
SKEL="/tmp/disposable-skel-$$"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

trap 'rm -rf "$FIXTURE" "$SKEL"' EXIT

mkdir -p "$FIXTURE/drive_c/windows/system32" "$FIXTURE/drive_c/Program Files"
touch "$FIXTURE/system.reg" "$FIXTURE/userdef.reg"
echo "custom content" > "$FIXTURE/drive_c/windows/testfile.txt"
chmod 700 "$FIXTURE/drive_c/windows/testfile.txt"

if command -v setfacl >/dev/null 2>&1; then
    setfacl -m u:wineuser:r-x "$FIXTURE/drive_c/windows/testfile.txt" 2>/dev/null || true
fi

ORIG_STAT=$(stat -c "%a %u %g" "$FIXTURE/drive_c/windows/testfile.txt")
ORIG_ACL=""
if command -v getfacl >/dev/null 2>&1; then
    ORIG_ACL=$(getfacl -p "$FIXTURE/drive_c/windows/testfile.txt" 2>/dev/null || true)
fi

echo "Initial stat: $ORIG_STAT"

# 1. Apply permissions
WEX_MACHINE_ROOT="$FIXTURE" WEX_SKEL_PFX="$SKEL" WEX_MACHINE_GROUP="wineuser" WEX_SKIP_WINEBOOT="1" \
    bash "$SCRIPT_DIR/setup-machine-root.sh" apply

APPLIED_STAT=$(stat -c "%a %u %g" "$FIXTURE/drive_c/windows/testfile.txt")
echo "Applied stat: $APPLIED_STAT"

if [ "$APPLIED_STAT" = "$ORIG_STAT" ]; then
    echo "ERROR: Applied permissions did not change file mode" >&2
    exit 1
fi

# 1b. Verify unprivileged write denial
set +e
su nobody -s /bin/bash -c "touch '$FIXTURE/drive_c/windows/unprivileged_write.txt'" 2>/dev/null
WRITE_RC=$?
set -e
if [ $WRITE_RC -eq 0 ]; then
    echo "FAIL: Unprivileged write succeeded on machine root" >&2
    exit 1
fi
echo "b3_unprivileged_write_denial=PASS"

# 2. Rollback permissions
WEX_MACHINE_ROOT="$FIXTURE" WEX_SKEL_PFX="$SKEL" WEX_MACHINE_GROUP="wineuser" \
    bash "$SCRIPT_DIR/setup-machine-root.sh" rollback

RESTORED_STAT=$(stat -c "%a %u %g" "$FIXTURE/drive_c/windows/testfile.txt")
RESTORED_ACL=""
if command -v getfacl >/dev/null 2>&1; then
    RESTORED_ACL=$(getfacl -p "$FIXTURE/drive_c/windows/testfile.txt" 2>/dev/null || true)
fi

echo "Restored stat: $RESTORED_STAT"

if [ "$ORIG_STAT" != "$RESTORED_STAT" ]; then
    echo "FAIL: stat mismatch: expected $ORIG_STAT got $RESTORED_STAT" >&2
    exit 1
fi

if [ -n "$ORIG_ACL" ] && [ "$ORIG_ACL" != "$RESTORED_ACL" ]; then
    echo "FAIL: acl mismatch: expected $ORIG_ACL got $RESTORED_ACL" >&2
    exit 1
fi

echo "b3_metadata_capture_and_rollback=PASS"

# 3. Path validation assertions: dangerous root path rejection
set +e
WEX_MACHINE_ROOT="/" bash "$SCRIPT_DIR/setup-machine-root.sh" apply 2>/dev/null
EXIT_CODE=$?
set -e
if [ $EXIT_CODE -eq 0 ]; then
    echo "FAIL: setup-machine-root allowed / as machine root" >&2
    exit 1
fi

echo "b3_path_validation_and_safety=PASS"
