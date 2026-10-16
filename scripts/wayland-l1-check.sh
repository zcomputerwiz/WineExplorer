#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Run in an isolated lab session. See docs/WAYLAND_L1.md for environment setup.
set -euo pipefail
phase=${1:?Usage: wayland-l1-check.sh stack|click|compose|pointer|mixed output-directory}
out=$(realpath -m "${2:?Provide an output directory}")
project=$(cd "$(dirname "$0")/.." && pwd)
wine_bin=$(command -v "${WEX_WINE_BIN:-wine}")
server_bin=$(command -v "${WEX_WINESERVER_BIN:-wineserver}")
export WINESERVER="$server_bin" WINELOADER="$wine_bin"
export WINEDEBUG=${WINEDEBUG:--all}
export WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d'
test -n "${WINEPREFIX:-}" && test "$WINEPREFIX" != "$HOME/.wine"
mkdir -p "$out"
control="$out/window-control.exe"
x86_64-w64-mingw32-gcc -Wall -I"$project/src/include" "$project/scripts/wayland-window-control.c" -lgdi32 -o "$control"
run() { "$wine_bin" "$control" "$@"; }
capture() {
    if [ -n "${WAYLAND_DISPLAY:-}" ]; then grim "$1"; else import -window root "$1"; fi
}
click() { DISPLAY=${WEX_L1_XDISPLAY:?Provide the nested Xvfb display} xdotool mousemove "$1" "$2" click 1; sleep 1; }
keys() { DISPLAY=${WEX_L1_XDISPLAY:?Provide the nested Xvfb display} xdotool key "$@"; }
child=
native=
cleanup() {
    if [ -n "$native" ]; then kill "$native" 2>/dev/null || true; wait "$native" || true; fi
    if [ -n "$child" ]; then kill "$child" 2>/dev/null || true; wait "$child" || true; fi
}
trap cleanup EXIT
if [ "${WAYLAND_DISPLAY:-}" = wex-shell-0 ]; then
    for ((i=0;i<100;i++)); do
        if run shell-ready > "$out/shell-ready.txt"; then break; fi
        sleep .2
    done
    run shell-ready > "$out/shell-ready.txt"
fi
case "$phase" in
    stack)
        test "${WAYLAND_DISPLAY:-}" = wex-shell-0
        x86_64-w64-mingw32-gcc -Wall "$project/scripts/wayland-stacking-probe.c" -lgdi32 -o "$out/stack64.exe"
        i686-w64-mingw32-gcc -Wall "$project/scripts/wayland-stacking-probe.c" -lgdi32 -o "$out/stack32.exe"
        python3 "$project/scripts/check-wayland-stacking.py" "$wine_bin" "$out/stack64.exe" "$out" --second-probe "$out/stack32.exe"
        ;;
    click)
        test "${WAYLAND_DISPLAY:-}" = wex-shell-0
        x86_64-w64-mingw32-gcc -Wall "$project/scripts/wayland-shell-probe.c" -lgdi32 -o "$out/shell-probe.exe"
        "$wine_bin" "$out/shell-probe.exe" > "$out/click-probe.log" 2>&1 & child=$!
        for ((i=0;i<100;i++)); do
            run zorder > "$out/click-ready.txt"
            if grep -q 'Activation red' "$out/click-ready.txt" &&
               grep -q 'Activation blue' "$out/click-ready.txt" &&
               grep -q 'Noactivate green' "$out/click-ready.txt" &&
               grep -q 'Untitled - Notepad' "$out/click-ready.txt"; then break; fi
            sleep .2
        done
        grep -q 'Untitled - Notepad' "$out/click-ready.txt"
        sleep 1
        run minimize > "$out/click-minimized.txt"
        sleep 1
        click 130 160
        capture "$out/click-red.png"; run > "$out/click-red-focus.txt"
        click 450 350
        capture "$out/click-blue.png"; run > "$out/click-blue-focus.txt"
        click 800 300
        run > "$out/click-green-focus.txt"
        click 980 500
        capture "$out/click-desktop.png"
        # ShowWindow can restore without foreground permission. v2 restore()
        # makes it visible; it must remain unfocused until the real click below.
        run restore > "$out/click-background-restore.txt"
        sleep 1
        click 130 160
        click 700 500
        capture "$out/click-restored-notepad.png"
        click 250 250
        run > "$out/click-covered-focus.txt"
        run zorder > "$out/click-note-zorder.txt"
        grep -q 'FOCUS .*Activation red' "$out/click-red-focus.txt"
        grep -q 'FOCUS .*Activation blue' "$out/click-blue-focus.txt"
        grep -q 'FOCUS .*Activation blue' "$out/click-green-focus.txt"
        grep -q 'FOCUS .*Untitled - Notepad' "$out/click-covered-focus.txt"
        python3 "$project/scripts/check-wayland-clicks.py" "$out"
        ;;
    compose)
        # Alpha comparisons need the same desktop background on both backends.
        run minimize >/dev/null
        x86_64-w64-mingw32-gcc -Wall -I "$project/src/include" "$project/scripts/compose-probe.c" -lgdi32 -o "$out/compose-probe.exe"
        "$wine_bin" "$out/compose-probe.exe" --stationary > "$out/compose.log" 2>&1 & child=$!
        for ((i=0;i<100;i++)); do
            if grep -q '^probe ready' "$out/compose.log" &&
               run composition-rects > "$out/compose-rects.txt"; then break; fi
            sleep .2
        done
        grep -q '^probe ready' "$out/compose.log"
        run composition-rects > "$out/compose-rects.txt"
        sleep 2
        capture "$out/compose.png"
        run composition-close
        wait "$child"; child=
        ;;
    pointer)
        test "${WAYLAND_DISPLAY:-}" = wex-shell-0
        WINEDEBUG=+waylanddrv,+cursor "$wine_bin" "$control" warp-own > "$out/pointer.log" 2>&1 & child=$!
        sleep 3
        click 800 500; keys w
        sleep 1
        grep -q 'WARP_OWN 1' "$out/pointer.log"
        grep -q 'MOTION 950 600' "$out/pointer.log"
        grep -q 'warp_pointer hwnd=' "$out/pointer.log"
        echo POINTER_PROTOCOL_AND_MOTION_PASSED
        ;;
    mixed)
        test "${WAYLAND_DISPLAY:-}" = wex-shell-0
        x86_64-w64-mingw32-gcc -Wall "$project/scripts/wayland-shell-probe.c" -lgdi32 -o "$out/mixed-focus.exe"
        "$wine_bin" "$out/mixed-focus.exe" > "$out/mixed-probe.log" 2>&1 & child=$!
        for ((i=0;i<100;i++)); do
            run zorder > "$out/mixed-ready.txt"
            grep -q 'Activation red' "$out/mixed-ready.txt" && break
            sleep .2
        done
        grep -q 'Activation red' "$out/mixed-ready.txt"
        sleep 1
        run minimize >/dev/null
        sleep 1
        click 130 160; keys a
        sleep 1
        grep -q 'value=97' "$out/mixed-probe.log"
        WAYLAND_DISPLAY=wayland-0 foot --window-size-pixels=400x200 --title=L1FocusPeer sh -c 'sleep 20' > "$out/mixed-foot.log" 2>&1 & native=$!
        sleep 3
        keys b; sleep 1
        capture "$out/mixed-foot.png"
        click 130 160; keys c
        sleep 1
        capture "$out/mixed-return.png"
        run > "$out/mixed-focus.txt"
        grep -q 'value=99' "$out/mixed-probe.log"
        ! grep -q 'value=98' "$out/mixed-probe.log"
        echo NATIVE_FOCUS_RETURN_PASSED
        ;;
    *) echo "Unknown phase: $phase" >&2; exit 2 ;;
esac
