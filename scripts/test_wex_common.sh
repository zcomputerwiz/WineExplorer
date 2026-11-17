#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Unit tests for scripts/lib/wex-common.sh (no root needed).
# Tests wex_machine_root, wex_reg_section, and wex_prefix_needs_update.

set -euo pipefail

HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
LIB="$HERE/lib/wex-common.sh"

if [ ! -f "$LIB" ]; then
    echo "FAIL: $LIB does not exist" >&2
    exit 1
fi

# Sourcing must have no side effects
output=$( . "$LIB" )
if [ -n "$output" ]; then
    echo "FAIL: sourcing $LIB produced output: $output" >&2
    exit 1
fi

. "$LIB"

TMPDIR=$(mktemp -d)
trap 'rm -rf -- "$TMPDIR"' EXIT

passed=0
failed=0

pass() {
    echo "PASS: $*"
    passed=$((passed + 1))
}

fail() {
    echo "FAIL: $*" >&2
    failed=$((failed + 1))
}

# ==============================================================================
# 1. wex_machine_root
# ==============================================================================

# 1a. Env override wins
res=$(WEX_MACHINE_ROOT="/custom/from_env" wex_machine_root)
if [ "$res" = "/custom/from_env" ]; then
    pass "wex_machine_root: env override"
else
    fail "wex_machine_root: expected '/custom/from_env', got '$res'"
fi

# 1b. Valid configuration file
CONF_VALID="$TMPDIR/valid.conf"
cat > "$CONF_VALID" <<'EOF'
group=wexsys
machine_root=/custom/machine/from_file
admin_group=wexadmins
EOF
chmod 644 "$CONF_VALID"

res=$(WEX_MACHINE_ROOT="" WEX_SYSRUN_CONF="$CONF_VALID" wex_machine_root)
if [ "$res" = "/custom/machine/from_file" ]; then
    pass "wex_machine_root: valid file"
else
    fail "wex_machine_root: valid file expected '/custom/machine/from_file', got '$res'"
fi

# 1c. Duplicate key ignored -> default (/var/lib/wineexplorer/machine)
CONF_DUP="$TMPDIR/dup.conf"
cat > "$CONF_DUP" <<'EOF'
group=wexsys
machine_root=/custom/first
machine_root=/custom/second
EOF
chmod 644 "$CONF_DUP"

res=$(WEX_MACHINE_ROOT="" WEX_SYSRUN_CONF="$CONF_DUP" wex_machine_root)
if [ "$res" = "/var/lib/wineexplorer/machine" ]; then
    pass "wex_machine_root: duplicate key ignored -> default"
else
    fail "wex_machine_root: duplicate key expected '/var/lib/wineexplorer/machine', got '$res'"
fi

# 1d. Unsafe mode ignored -> default
CONF_UNSAFE="$TMPDIR/unsafe.conf"
cat > "$CONF_UNSAFE" <<'EOF'
group=wexsys
machine_root=/custom/unsafe
EOF

# Test via mock stat with unsafe permissions (mode 666 / 664)
MOCK_BIN="$TMPDIR/mock_bin"
mkdir -p "$MOCK_BIN"
cat > "$MOCK_BIN/stat" <<'EOF'
#!/bin/sh
# Return UID 0 and unsafe permissions (mode 666 - writable by group and others)
echo "0 666"
EOF
chmod 755 "$MOCK_BIN/stat"

res=$(PATH="$MOCK_BIN:$PATH" WEX_MACHINE_ROOT="" WEX_SYSRUN_CONF="$CONF_UNSAFE" wex_machine_root)
if [ "$res" = "/var/lib/wineexplorer/machine" ]; then
    pass "wex_machine_root: unsafe mode (666) ignored -> default"
else
    fail "wex_machine_root: unsafe mode (666) expected '/var/lib/wineexplorer/machine', got '$res'"
fi

# Group-writable (mode 664)
cat > "$MOCK_BIN/stat" <<'EOF'
#!/bin/sh
echo "0 664"
EOF
res=$(PATH="$MOCK_BIN:$PATH" WEX_MACHINE_ROOT="" WEX_SYSRUN_CONF="$CONF_UNSAFE" wex_machine_root)
if [ "$res" = "/var/lib/wineexplorer/machine" ]; then
    pass "wex_machine_root: unsafe mode (664) ignored -> default"
else
    fail "wex_machine_root: unsafe mode (664) expected '/var/lib/wineexplorer/machine', got '$res'"
fi

# 1e. Missing or invalid file -> default
res=$(WEX_MACHINE_ROOT="" WEX_SYSRUN_CONF="$TMPDIR/nonexistent.conf" wex_machine_root)
if [ "$res" = "/var/lib/wineexplorer/machine" ]; then
    pass "wex_machine_root: nonexistent file -> default"
else
    fail "wex_machine_root: nonexistent file expected '/var/lib/wineexplorer/machine', got '$res'"
fi


# ==============================================================================
# 2. wex_reg_section
# ==============================================================================

REG="$TMPDIR/test.reg"
cat > "$REG" <<'EOF'
WINE REGISTRY Version 2
;; All keys relative to \\Machine

#arch=win64

[Software\\Vendor\\App] 1731234567
#time=1db33b1c5ac3a2e
"Version"="1.0.0"
"InstallDir"="C:\\Program Files\\App"
"MultiLine"=hex:01,02,03,04,\
  05,06

[Software\\Vendor\\App\\SubKey] 1731234567
"SubValue"="MustNotMatch"

[Software\\Vendor\\OtherApp] 1731234567
"OtherValue"="ShouldStopBeforeThis"

[Software] 1731234567
"RootVal"="1"
EOF

# 2a. Case-insensitive key match
out=$(wex_reg_section "$REG" 'software\vendor\app')
if echo "$out" | grep -Fq '"Version"="1.0.0"' && echo "$out" | grep -Fq '"InstallDir"="C:\\Program Files\\App"'; then
    pass "wex_reg_section: case-insensitive key match"
else
    fail "wex_reg_section: case-insensitive key did not match expected values: $out"
fi

# Case-insensitive with uppercase and doubled backslashes
out_upper=$(wex_reg_section "$REG" 'SOFTWARE\\VENDOR\\APP')
if echo "$out_upper" | grep -q '"Version"="1.0.0"'; then
    pass "wex_reg_section: uppercase and doubled backslashes match"
else
    fail "wex_reg_section: uppercase query failed: $out_upper"
fi

# Multi-line value preserved
if echo "$out" | grep -q '05,06'; then
    pass "wex_reg_section: continuation lines preserved"
else
    fail "wex_reg_section: multi-line continuation missing: $out"
fi

# 2b. Stops at the next key
if echo "$out" | grep -q 'ShouldStopBeforeThis'; then
    fail "wex_reg_section: did not stop at next key; output contains following section"
else
    pass "wex_reg_section: stops at next key"
fi

# 2c. Subkey with same prefix is not matched
if echo "$out" | grep -q 'MustNotMatch'; then
    fail "wex_reg_section: subkey with same prefix was matched"
else
    pass "wex_reg_section: subkey with same prefix not matched in section values"
fi

# Querying prefix key that has subkeys but no exact match
out_prefix=$(wex_reg_section "$REG" 'Software\Vendor')
if [ -z "$out_prefix" ]; then
    pass "wex_reg_section: prefix-only key returns empty"
else
    fail "wex_reg_section: prefix-only key returned unexpected output: $out_prefix"
fi

# Querying exact root key
out_root=$(wex_reg_section "$REG" 'Software')
if echo "$out_root" | grep -q '"RootVal"="1"' && ! echo "$out_root" | grep -q '"Version"='; then
    pass "wex_reg_section: exact root key matched without subkeys"
else
    fail "wex_reg_section: exact root key output unexpected: $out_root"
fi

# 2d. .reg.lock shared locking
touch "$TMPDIR/.reg.lock"
out_locked=$(wex_reg_section "$REG" 'Software')
if echo "$out_locked" | grep -q '"RootVal"="1"'; then
    pass "wex_reg_section: read succeeds with .reg.lock present"
else
    fail "wex_reg_section: read failed with .reg.lock: $out_locked"
fi


# ==============================================================================
# 3. wex_prefix_needs_update
# ==============================================================================

PFX="$TMPDIR/test_pfx"
mkdir -p "$PFX"
NTDLL_FILE="$TMPDIR/ntdll.so"
touch -d "2026-01-01 10:00:00" "$NTDLL_FILE"

# 3a. Prefix without .update-timestamp -> needs update (returns 0)
if WEX_NTDLL="$NTDLL_FILE" wex_prefix_needs_update "$PFX" ""; then
    pass "wex_prefix_needs_update: no .update-timestamp needs update"
else
    fail "wex_prefix_needs_update: expected 0 when .update-timestamp is missing"
fi

# 3b. Prefix timestamp newer than NTDLL -> up to date (returns 1)
touch -d "2026-01-02 10:00:00" "$PFX/.update-timestamp"
if WEX_NTDLL="$NTDLL_FILE" wex_prefix_needs_update "$PFX" "$TMPDIR/empty_machine"; then
    fail "wex_prefix_needs_update: expected 1 when prefix is newer than NTDLL"
else
    pass "wex_prefix_needs_update: prefix newer than NTDLL -> up to date"
fi

# 3c. NTDLL newer than prefix timestamp -> needs update (returns 0)
touch -d "2026-01-03 10:00:00" "$NTDLL_FILE"
if WEX_NTDLL="$NTDLL_FILE" wex_prefix_needs_update "$PFX" "$TMPDIR/empty_machine"; then
    pass "wex_prefix_needs_update: NTDLL newer than prefix -> needs update"
else
    fail "wex_prefix_needs_update: expected 0 when NTDLL is newer than prefix"
fi

# 3d. Machine root timestamp newer than prefix timestamp -> needs update (returns 0)
MACHINE_ROOT_TEST="$TMPDIR/test_machine"
mkdir -p "$MACHINE_ROOT_TEST"
touch -d "2026-01-04 10:00:00" "$PFX/.update-timestamp"
touch -d "2026-01-01 10:00:00" "$NTDLL_FILE"
touch -d "2026-01-05 10:00:00" "$MACHINE_ROOT_TEST/.update-timestamp"

if WEX_NTDLL="$NTDLL_FILE" wex_prefix_needs_update "$PFX" "$MACHINE_ROOT_TEST"; then
    pass "wex_prefix_needs_update: machine root newer than prefix -> needs update"
else
    fail "wex_prefix_needs_update: expected 0 when machine root timestamp is newer"
fi

# 3e. Prefix timestamp newer than both NTDLL and machine root -> up to date (returns 1)
touch -d "2026-01-06 10:00:00" "$PFX/.update-timestamp"
if WEX_NTDLL="$NTDLL_FILE" wex_prefix_needs_update "$PFX" "$MACHINE_ROOT_TEST"; then
    fail "wex_prefix_needs_update: expected 1 when prefix is newer than both"
else
    pass "wex_prefix_needs_update: prefix newer than both -> up to date"
fi


# ==============================================================================
# 4. Domain-join detection: wex-domain-state.sh --joined
# ==============================================================================

DOMAIN_STATE_SCRIPT="$HERE/../installer/scripts/wex-domain-state.sh"
if [ -f "$DOMAIN_STATE_SCRIPT" ]; then
    FAKE_KT="$TMPDIR/fake.keytab"
    touch "$FAKE_KT"

    # Unjoined: no realm -> exits 1
    if WEX_KEYTAB="$FAKE_KT" bash "$DOMAIN_STATE_SCRIPT" --joined >/dev/null 2>&1; then
        fail "wex-domain-state.sh --joined: expected exit 1 when unjoined"
    else
        pass "wex-domain-state.sh --joined: unjoined exits 1"
    fi

    # Joined via mock testparm
    TESTPARM_BIN="$TMPDIR/testparm_bin"
    mkdir -p "$TESTPARM_BIN"
    cat > "$TESTPARM_BIN/testparm" <<'EOF'
#!/bin/sh
case "$*" in
    *security*) echo "ads" ;;
    *realm*) echo "ad.wexlab.example" ;;
    *workgroup*) echo "WEXLAB" ;;
esac
EOF
    chmod 755 "$TESTPARM_BIN/testparm"

    if PATH="$TESTPARM_BIN:$PATH" WEX_KEYTAB="$FAKE_KT" bash "$DOMAIN_STATE_SCRIPT" --joined >/dev/null 2>&1; then
        pass "wex-domain-state.sh --joined: joined exits 0"
    else
        fail "wex-domain-state.sh --joined: expected exit 0 when joined"
    fi

    # Missing keytab -> exits 1
    if PATH="$TESTPARM_BIN:$PATH" WEX_KEYTAB="$TMPDIR/nonexistent.keytab" bash "$DOMAIN_STATE_SCRIPT" --joined >/dev/null 2>&1; then
        fail "wex-domain-state.sh --joined: expected exit 1 when keytab is missing"
    else
        pass "wex-domain-state.sh --joined: missing keytab exits 1"
    fi
fi

# Summary
echo ""
echo "=== Test Summary: $passed passed, $failed failed ==="
if [ "$failed" -gt 0 ]; then
    exit 1
fi
exit 0
