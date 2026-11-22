#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# run-window-test.sh RUNTIME_DIR LOADER_DLL [BUILD_DIR] - the token broker's WebView2 sign-in window (WAM plan step
# 4) against the local test authority: the test provider with "TestWindow" = 1 signs in through the window, whose
# navigations the test authority redirects to the broker's redirect URI without any user. 64-bit only. RUNTIME_DIR is
# a WebView2 Fixed Version runtime folder and LOADER_DLL a 64-bit WebView2Loader.dll, both outside the repository
# (docs/WEBVIEW2.md, the lab's /root/webview2-lab). As root in a build machine; prints PASS or FAIL.
set -u
RUNTIME=${1:?runtime folder}
LOADER=${2:?WebView2Loader.dll}
BUILD=${3:-/root/WE-wamprov/build/wow64}
HERE=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
if [ -z "${WEX_WAM_NS:-}" ]; then
    install -d -m 1777 /var/tmp/wex-wam-x
    exec env WEX_WAM_NS=1 unshare --mount --propagation private sh -c \
        "mount --bind /var/tmp/wex-wam-x /tmp/.X11-unix && exec bash '$0' '$RUNTIME' '$LOADER' '$BUILD'"
fi

WORK=$(mktemp -d /var/tmp/wex-wam-window.XXXXXX)
export WINEPREFIX=$WORK/prefix HOME=$WORK/home WINEDEBUG=${WINEDEBUG:--all} WINEDLLOVERRIDES="mscoree,mshtml="
export DISPLAY=:96
mkdir -p "$HOME"
cleanup()
{
    "$BUILD/server/wineserver" -k 2>/dev/null
    [ -n "${AUTH_PID:-}" ] && kill "$AUTH_PID" 2>/dev/null
    [ -n "${X_PID:-}" ] && kill "$X_PID" 2>/dev/null
    [ "${WEX_WAM_KEEP:-}" ] && { rm -rf /var/tmp/wex-wam-window-last; cp -r "$WORK" /var/tmp/wex-wam-window-last; }
    rm -rf "$WORK"
}
trap cleanup EXIT

Xvfb :96 -screen 0 1280x1024x24 -nolisten tcp > "$WORK/xvfb.log" 2>&1 &
X_PID=$!
python3 "$HERE/test_authority.py" "$WORK/port" 2> "$WORK/authority.log" &
AUTH_PID=$!
for _ in $(seq 50); do [ -s "$WORK/port" ] && break; sleep 0.1; done
PORT=$(cat "$WORK/port")
timeout 300 "$BUILD/wine" wineboot -i > "$WORK/wineboot.log" 2>&1 || { echo "FAIL wineboot"; exit 1; }
W="$BUILD/wine"
win_path() { "$W" winepath -w "$1" 2>/dev/null | tr -d '\r'; }
# where the image installs it (installer/stage-runtime-assets.py), bound over that folder in this mount namespace
# only; WEX_WAM_LOADER=beside puts it beside the broker instead (the web_core tests remove the WebAuthentication key
# before test_broker runs, so the HKLM setting is not what is tested)
if [ "${WEX_WAM_LOADER:-image}" = beside ]; then
    cp "$LOADER" "$WINEPREFIX/drive_c/windows/system32/WebView2Loader.dll"
else
    IMAGE_DIR=/usr/local/share/wineexplorer/webview2
    install -d -m 755 "$WORK/webview2/x64" && install -m 644 "$LOADER" "$WORK/webview2/x64/WebView2Loader.dll"
    [ -d "$IMAGE_DIR" ] || install -d -m 755 "$IMAGE_DIR"
    mount --bind "$WORK/webview2" "$IMAGE_DIR" || { echo "FAIL could not bind the loader folder"; exit 1; }
    [ ! -e "$WINEPREFIX/drive_c/windows/system32/WebView2Loader.dll" ] || { echo "FAIL a loader is beside the broker"; exit 1; }
    echo "loader: the image's folder $IMAGE_DIR/x64"
fi
# Chromium under Wine without DirectComposition (docs/WEBVIEW2.md, Proton's setting) and with software rendering
"$W" reg add 'HKCU\Software\Wine\AppDefaults\msedgewebview2.exe' /v Version /t REG_SZ /d win7 /f > /dev/null 2>&1
export WEBVIEW2_BROWSER_EXECUTABLE_FOLDER="$(win_path "$RUNTIME")"
export WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS="--disable-gpu --enable-unsafe-swiftshader --use-angle=swiftshader --no-sandbox"
export WEBCORE_TEST_AUTHORITY="http://127.0.0.1:$PORT/" WEBCORE_TEST_WINDOW=1
echo "runtime folder: $WEBVIEW2_BROWSER_EXECUTABLE_FOLDER"

exe=$BUILD/dlls/windows.security.authentication.web.core/tests/x86_64-windows/windows.security.authentication.web.core_test.exe
timeout 400 "$W" "$exe" web_core > "$WORK/web_core.log" 2>&1
rc=$?
grep -E 'Test failed|tests executed' "$WORK/web_core.log" | tail -5
echo "authority: $(curl -s "http://127.0.0.1:$PORT/stats" 2>/dev/null)"
if [ $rc = 0 ] && ! grep -q 'Test failed' "$WORK/web_core.log"; then echo "PASS window sign-in"; else echo "FAIL window sign-in (exit $rc)"; fi
exit $rc
