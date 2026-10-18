#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Private split prefixes only; requires a completed runtime-smoke directory and
# the verified payload used by that run. No installed prefix or DLL is modified.
set -euo pipefail
: "${WINE:?Pinned WoW64 loader required}"
: "${WINESERVER:?Matching wineserver required}"
RUNTIME=$(realpath "${1:?Completed runtime-smoke directory}")
PAYLOAD=$(realpath "${2:?Verified runtime payload directory}")
SCRIPT=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/wex-graphics.py
if [ ! -f "$SCRIPT" ]; then SCRIPT="$(dirname "$0")/wex-graphics.py"; fi
TEST_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/wex-graphics-smoke.XXXXXXXX")
echo "Graphics evidence and private prefixes: $TEST_ROOT"
export WEX_DXVK_DIR="$PAYLOAD/usr/local/share/wineexplorer/dxvk"
export WINELINKFAKEDLLS=1 WINEDEBUG=-all
xvfb_pid=
cleanup()
{
    for user in alice bob; do
        WINEPREFIX="$TEST_ROOT/$user" "$WINESERVER" -k 2>/dev/null || true
    done
    [ -z "$xvfb_pid" ] || kill "$xvfb_pid" 2>/dev/null || true
}
trap cleanup EXIT
Xvfb -displayfd 3 -screen 0 1024x768x24 -nolisten tcp 3>"$TEST_ROOT/display" >"$TEST_ROOT/xvfb.log" 2>&1 &
xvfb_pid=$!
for ((i=0; i<100; i++)); do
    [ ! -s "$TEST_ROOT/display" ] || break
    kill -0 "$xvfb_pid"
    sleep .1
done
export DISPLAY=":$(<"$TEST_ROOT/display")"
for user in alice bob; do
    prefix="$TEST_ROOT/$user"
    mkdir -p "$prefix/drive_c" "$prefix/dosdevices"
    ln -s ../drive_c "$prefix/dosdevices/c:"
    ln -s / "$prefix/dosdevices/z:"
    for path in "$RUNTIME/machine/drive_c/"*; do
        if [ "$(basename "$path")" = users ]; then
            cp -a "$path" "$prefix/drive_c/"
        else
            ln -s "$path" "$prefix/drive_c/$(basename "$path")"
        fi
    done
    ln -s "$RUNTIME/machine/system.reg" "$prefix/system.reg"
    ln -s "$RUNTIME/machine/userdef.reg" "$prefix/userdef.reg"
    cp "$RUNTIME/machine/user.reg" "$prefix/user.reg"
    cp "$RUNTIME/machine/.update-timestamp" "$prefix/.update-timestamp"
done
shared="$RUNTIME/machine/drive_c/windows/system32/d3d11.dll"
before=$(sha256sum "$shared")
export WINEPREFIX="$TEST_ROOT/alice"
# Keep a real client active: on/off must not wait for the user's server to exit.
"$WINE" notepad.exe >"$TEST_ROOT/notepad.log" 2>&1 &
client=$!
timeout 30 python3 "$SCRIPT" vulkan on
timeout 5 python3 "$SCRIPT" vulkan off
kill -0 "$client"
echo 'PASS: configuration on/off completed while a Wine client remained active.'
"$WINESERVER" -k
timeout 30 python3 "$SCRIPT" vulkan on
overrides=$(python3 "$SCRIPT" vulkan prepare)
for bits in 32 64; do
    mkdir -p "$TEST_ROOT/dxvk$bits"
    WINEDLLOVERRIDES="$overrides;winemenubuilder.exe=d" WINEDEBUG=+loaddll \
        DXVK_LOG_PATH="$TEST_ROOT/dxvk$bits" timeout 90 "$WINE" "$RUNTIME/native$bits.exe" d3d11 \
        >"$TEST_ROOT/native$bits.log" 2>&1
    grep -Ei 'd3d11\.dll.*native|D3D11 .*PASS' "$TEST_ROOT/native$bits.log"
    grep -E 'DXVK: v2.4.1|llvmpipe|Device name' "$TEST_ROOT/dxvk$bits/"*.log
done
[ "$before" = "$(sha256sum "$shared")" ]
[ "$(readlink -f "$TEST_ROOT/bob/drive_c/windows/system32/d3d11.dll")" = "$(readlink -f "$shared")" ]
echo 'PASS: shared and Bob DLLs are unchanged.'
"$WINESERVER" -k
python3 "$SCRIPT" vulkan off
python3 "$SCRIPT" vulkan prepare
for bits in 32 64; do
    WINEDLLOVERRIDES=winemenubuilder.exe=d WINEDEBUG=+loaddll \
        timeout 90 "$WINE" "$RUNTIME/native$bits.exe" d3d11 >"$TEST_ROOT/builtin$bits.log" 2>&1
    grep -Ei 'd3d11\.dll.*builtin|D3D11 .*PASS' "$TEST_ROOT/builtin$bits.log"
done
if VK_ICD_FILENAMES=/nonexistent timeout 30 python3 "$SCRIPT" vulkan on; then
    echo 'FAIL: no-device enable unexpectedly succeeded' >&2
    exit 1
fi
[ ! -e "$WINEPREFIX/.wex-graphics/enabled" ]
echo 'PASS: no-device enable refused without changing the Wine backend.'
