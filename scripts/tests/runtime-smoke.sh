#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Run in a network namespace as a non-root build user. No global killall, fixed
# display, installed-runtime mutation or real user prefix is used. Logs retained.
set -euo pipefail
: "${WINE:?Point WINE at the pinned WoW64 loader}"
: "${WINESERVER:?Point WINESERVER at the matching server}"
PAYLOAD=$(realpath "${1:?Pass the staged open-payload directory}")
SOURCE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
if [ "$(readlink /proc/self/ns/net)" = "$(readlink /proc/1/ns/net)" ]; then
    echo 'Run this test in its own offline network namespace (unshare -n).' >&2
    exit 1
fi
TEST_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/wex-runtime-smoke.XXXXXXXX")
echo "Evidence and disposable prefixes: $TEST_ROOT"
export WINEPREFIX="$TEST_ROOT/machine" WINELINKFAKEDLLS=1 WINEDEBUG=-all
export XDG_CACHE_HOME="$TEST_ROOT/cache"
export WINEDLLOVERRIDES=winemenubuilder.exe=d
xvfb_pid=
compositor_pid=
cleanup()
{
    "$WINESERVER" -k 2>/dev/null || true
    [ -z "$xvfb_pid" ] || kill "$xvfb_pid" 2>/dev/null || true
    [ -z "$compositor_pid" ] || kill "$compositor_pid" 2>/dev/null || true
}
trap cleanup EXIT
backend=${WEX_SMOKE_BACKEND:-x11}
if [ "$backend" = wayland ]; then
    : "${WEX_WEXCOMP_BIN:?Point at the existing headless-test compositor}"
    mkdir -m700 "$TEST_ROOT/runtime" "$TEST_ROOT/config"
    cp "$SOURCE/runtime-labwc.xml" "$TEST_ROOT/config/rc.xml"
    export XDG_RUNTIME_DIR="$TEST_ROOT/runtime" WAYLAND_DISPLAY=wex-shell-0
    export WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1
    export LABWC_WEX_SHELL_SOCKET=wex-shell-0 WEX_WAYLAND_NATIVE_WINDOWS=1
    unset DISPLAY
    "$WEX_WEXCOMP_BIN" -C "$TEST_ROOT/config" >"$TEST_ROOT/compositor.log" 2>&1 &
    compositor_pid=$!
    for ((i=0; i<100; i++)); do
        [ ! -S "$XDG_RUNTIME_DIR/wex-shell-0" ] || break
        kill -0 "$compositor_pid"
        sleep .1
    done
    test -S "$XDG_RUNTIME_DIR/wex-shell-0"
elif [ "$backend" = x11 ]; then
Xvfb -displayfd 3 -screen 0 1024x768x24 -nolisten tcp 3>"$TEST_ROOT/display" >"$TEST_ROOT/xvfb.log" 2>&1 &
xvfb_pid=$!
for ((i=0; i<100; i++)); do
    [ ! -s "$TEST_ROOT/display" ] || break
    kill -0 "$xvfb_pid"
    sleep .1
done
export DISPLAY=":$(<"$TEST_ROOT/display")"
else
    echo "Unknown smoke backend: $backend" >&2
    exit 1
fi
# A source-build loader searches its build tree ahead of WINEDATADIR. Its
# isolated hash-checked addon cache exercises the same shipped MSI files without
# installing anything in /usr/local or modifying the build tree.
mkdir -p "$XDG_CACHE_HOME/wine"
for msi in "$PAYLOAD/usr/local/share/wine/mono/"*.msi "$PAYLOAD/usr/local/share/wine/gecko/"*.msi; do
    ln -s "$msi" "$XDG_CACHE_HOME/wine/$(basename "$msi")"
done
timeout 240 "$WINE" wineboot.exe -i >"$TEST_ROOT/wineboot.log" 2>&1
timeout 120 "$WINESERVER" -w
for spec in '32 i686-w64-mingw32-gcc' '64 x86_64-w64-mingw32-gcc'; do
    read -r bits compiler <<< "$spec"
    "$compiler" -Wall -Wextra -O2 "$SOURCE/runtime-smoke.c" -o "$TEST_ROOT/native$bits.exe" -lole32 -loleaut32 -luuid -ld3d11
    timeout 60 "$WINE" "$TEST_ROOT/native$bits.exe"
    timeout 180 "$WINE" "$TEST_ROOT/native$bits.exe" html >"$TEST_ROOT/mshtml$bits.log" 2>&1
    cat "$TEST_ROOT/mshtml$bits.log"
done
mcs=$(find "$WINEPREFIX/drive_c/windows/mono" -path '*/4.5/mcs.exe' -print -quit)
[ -n "$mcs" ]
source_cs=$("$WINE" winepath.exe -w "$SOURCE/runtime-smoke.cs")
for bits in 32 64; do
    platform=x86
    [ "$bits" = 32 ] || platform=x64
    target_cs=$("$WINE" winepath.exe -w "$TEST_ROOT/managed$bits.exe")
    timeout 120 "$WINE" "$mcs" -nologo "-platform:$platform" "-out:$target_cs" "$source_cs"
    timeout 60 "$WINE" "$TEST_ROOT/managed$bits.exe"
done
echo "Offline Mono and Gecko native/managed smoke checks passed ($backend, source-build cache path)."
