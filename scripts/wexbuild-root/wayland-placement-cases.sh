#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# wayland-placement-cases.sh: the placement cases around a live scale change that wayland-scale-cycle.sh does not cover
# (docs/WAYLAND_DEFAULT.md, "Restored live scaling"), without a VM. Headless wexcomp with two outputs side by side,
# HEADLESS-1 at 100% and HEADLESS-2 at 150%, explorer /shell with WEX_WAYLAND_NATIVE_WINDOWS=1, a real foot hosted in a
# Wine frame (foot snaps to its cell grid) and a plain Wine window (scale-window-probe.exe, a pure blue client area).
# wex-winctl.exe drives and reports the windows through Win32.
#   CASE=max  (maximize/restore across a scale change, on HEADLESS-1): maximize at 100%; HEADLESS-1 to 150%: the window
#             stays maximized and fills the output's width; restore at 150%: the restored window is at its 100% top
#             left with 1.5 times its 100% size (its device-independent size kept); back to 100%: as at the start. Then
#             the same the other way round: maximize at 150%, back to 100% while maximized, restore at 100%.
#   CASE=move (a move between outputs at different scales): move each window onto HEADLESS-2 (150%): it is on that
#             output only and 1.5 times its HEADLESS-1 size; move it back: its HEADLESS-1 size again.
# Sizes are checked within 15% or 24 px (foot's cells), positions within TOL px (default 10). COMP, W, OUT, PROBE,
# CTL name the compositor, the build, the output directory, the probe and wex-winctl.exe.
COMP=${COMP:-/root/wexcomp-dev/build/labwc}
W=${W:-/root/WE-chrome/build/wow64}
OUT=${OUT:-/root/pc-out}
PROBE=${PROBE:-/root/scale-window-probe.exe}
CTL=${CTL:-/root/wex-winctl.exe}
CASE=${CASE:-max}
NAMES=${NAMES:-plain foot}
SETTLE=${SETTLE:-10}
TOL=${TOL:-10}
FOOT_TITLE="Wex real native client"; PLAIN_TITLE="scale probe"
mkdir -p "$OUT"; rm -f "$OUT"/*.png
R=$(mktemp -d /tmp/wex-pc-XXXXXX); chmod 700 "$R"
export XDG_RUNTIME_DIR=$R WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=${OUTPUTS:-2} WEX_SHELL_SOCKET=wex-shell-0
unset DISPLAY
pkill -9 -x wineserver; pkill -9 -x services.exe; pkill -9 -x foot; sleep 1
mkdir -p "$R/cfg"
cat > "$R/cfg/rc.xml" <<'EOF'
<?xml version="1.0"?>
<labwc_config>
  <core><decoration>server</decoration></core>
  <placement><policy>center</policy></placement>
</labwc_config>
EOF
"$COMP" -C "$R/cfg" > "$OUT/comp.log" 2>&1 &
CPID=$!
for i in $(seq 1 60); do [ -S "$R/wex-shell-0" ] && [ -S "$R/wayland-0" ] && break; sleep 0.2; done
[ -S "$R/wex-shell-0" ] || { echo "compositor socket missing"; head "$OUT/comp.log"; kill $CPID; exit 1; }
randr() { WAYLAND_DISPLAY=wex-shell-0 wlr-randr "$@"; }
randr --output HEADLESS-1 --custom-mode 1280x800 --pos 0,0 >/dev/null 2>&1
[ "${OUTPUTS:-2}" = 1 ] || randr --output HEADLESS-2 --custom-mode 1280x800 --pos 1280,0 --scale 1.5 >/dev/null 2>&1
randr > "$OUT/outputs.txt" 2>&1
export WINEPREFIX=${WINEPREFIX:-/root/pc-pfx} WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=${WINEDEBUG:--all}
L=$OUT/wine.log; rm -f "$L"
cd "$W"
if [ ! -d "$WINEPREFIX/drive_c" ]; then
    WAYLAND_DISPLAY=wex-shell-0 timeout 150 ./wine wineboot -i >>"$L" 2>&1
    WAYLAND_DISPLAY=wex-shell-0 ./wine reg add 'HKCU\Software\Wine\Drivers' /v Graphics /t REG_SZ /d wayland /f >>"$L" 2>&1
    timeout 30 ./server/wineserver -w
fi
rm -f "$WINEPREFIX/drive_c/scale.stop"
(WAYLAND_DISPLAY=wex-shell-0 WEX_WAYLAND_NATIVE_WINDOWS=1 timeout 900 ./wine explorer.exe /shell >>"$L" 2>&1 &)
sleep 15
(WAYLAND_DISPLAY=wex-shell-0 timeout 900 ./wine "Z:$PROBE" >>"$L" 2>&1 &)
sleep 3
(WAYLAND_DISPLAY=wayland-0 GDK_BACKEND=wayland foot --title="$FOOT_TITLE" --app-id=wex-real -w240x180 \
    -o colors.background=338855 /usr/bin/sleep 1500 >"$OUT/foot.log" 2>&1 &)
sleep 8
ctl() { WAYLAND_DISPLAY=wex-shell-0 ./wine "Z:$CTL" "$@" 2>>"$OUT/ctl.err" | tr -d '\r'; }
ctl monitors > "$OUT/monitors.txt"
echo "outputs (Win32):"; sed 's/^/  /' "$OUT/monitors.txt"

bbox() {   # bbox NAME HEXCOLOUR OUTPUT: a screenshot of one output, then the bounding box (x0 y0 x1 y1) of that colour
    WAYLAND_DISPLAY=wex-shell-0 grim -o "$3" "$OUT/$1.png"
    python3 - "$OUT/$1.png" "$2" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB"); w, h = im.size; px = im.load()
c = tuple(int(sys.argv[2][i:i + 2], 16) for i in (0, 2, 4))
xs = []; ys = []
for y in range(h):
    for x in range(w):
        if px[x, y] == c: xs.append(x); ys.append(y)
print(min(xs), min(ys), max(xs), max(ys)) if xs else print("none")
PY
}
RC=0
declare -A OX0 OY0 OW OH
near() { local d=$(( $1 > $2 ? $1 - $2 : $2 - $1 )); [ $d -le $3 ]; }
sizetol() { local t=$(( $1 * 15 / 100 )); [ $t -lt 24 ] && t=24; echo $t; }
check() {   # check LABEL NAME "x0 y0 x1 y1" PERCENT [anywhere]: top left kept (unless anywhere); size PERCENT of the start
    local label=$1 name=$2 box=$3 percent=$4 x0 y0 x1 y1 w h ew eh ok=1 why=""
    if [ "$box" = none ]; then RC=1; echo "FAIL $label ($name): not on the output"; return; fi
    read -r x0 y0 x1 y1 <<<"$box"
    w=$((x1 - x0 + 1)); h=$((y1 - y0 + 1))
    ew=$((OW[$name] * percent / 100)); eh=$((OH[$name] * percent / 100))
    if [ "$5" != anywhere ]; then
        near $x0 ${OX0[$name]} $TOL || { ok=0; why="$why left $x0 (was ${OX0[$name]})"; }
        near $y0 ${OY0[$name]} $TOL || { ok=0; why="$why top $y0 (was ${OY0[$name]})"; }
    fi
    near $w $ew $(sizetol $ew) || { ok=0; why="$why width $w (want about $ew)"; }
    near $h $eh $(sizetol $eh) || { ok=0; why="$why height $h (want about $eh)"; }
    if [ $ok = 1 ]; then echo "PASS $label ($name): bbox $box"; else RC=1; echo "FAIL $label ($name): bbox $box;$why"; fi
}
check_max() {   # check_max LABEL NAME "x0 y0 x1 y1" OUTPUT_WIDTH: maximized, and its client area spans the output
    local label=$1 name=$2 box=$3 ow=$4 x0 y0 x1 y1 state
    state=$(ctl "$(title $name)" state | awk '{print $NF}')
    if [ "$box" = none ]; then RC=1; echo "FAIL $label ($name): not on the output"; return; fi
    read -r x0 y0 x1 y1 <<<"$box"
    if [ "$state" = max ] && [ $((x1 - x0 + 1)) -ge $((ow * 95 / 100)) ]; then echo "PASS $label ($name): maximized, bbox $box"
    else RC=1; echo "FAIL $label ($name): state $state, bbox $box (want maximized, about $ow px wide)"; fi
}
title() { [ $1 = foot ] && echo "$FOOT_TITLE" || echo "$PLAIN_TITLE"; }
colour() { [ $1 = foot ] && echo 338855 || echo 0000ff; }
for name in $NAMES; do
    first=$(bbox start-$name $(colour $name) HEADLESS-1)
    [ "$first" != none ] || { echo "FAIL the $name window is not on HEADLESS-1 at the start"; RC=1; first="0 0 1 1"; }
    read -r x0 y0 x1 y1 <<<"$first"
    OX0[$name]=$x0; OY0[$name]=$y0; OW[$name]=$((x1 - x0 + 1)); OH[$name]=$((y1 - y0 + 1))
    echo "$name at the start: bbox $first; $(ctl "$(title $name)" state)"
done

if [ "$CASE" = max ]; then
    for name in $NAMES; do
        c=$(colour $name); t=$(title $name)
        # control: maximize and restore with no scale change
        ctl "$t" max >/dev/null; sleep 3
        check_max "maximized (no scale change)" $name "$(bbox $name-max0 $c HEADLESS-1)" 1280
        ctl "$t" restore >/dev/null; sleep 3
        check "restored (no scale change)" $name "$(bbox $name-res0 $c HEADLESS-1)" 100
        echo "  $name: $(ctl "$t" state)"
        [ "$CONTROL_ONLY" = 1 ] && continue
        # maximized at 100%, scale to 150% while maximized, restore at 150%, back to 100%
        ctl "$t" max >/dev/null; sleep 3
        check_max "maximized at 100%" $name "$(bbox $name-max100 $c HEADLESS-1)" 1280
        randr --output HEADLESS-1 --scale 1.5; sleep "$SETTLE"
        check_max "still maximized at 150%" $name "$(bbox $name-max150 $c HEADLESS-1)" 1280
        ctl "$t" restore >/dev/null; sleep "$SETTLE"
        check "restored at 150%" $name "$(bbox $name-res150 $c HEADLESS-1)" 150
        randr --output HEADLESS-1 --scale 1; sleep "$SETTLE"
        check "back at 100%" $name "$(bbox $name-res100 $c HEADLESS-1)" 100
        # the other way round: maximized at 150%, back to 100% while maximized, restore at 100%
        randr --output HEADLESS-1 --scale 1.5; sleep "$SETTLE"
        ctl "$t" max >/dev/null; sleep 3
        check_max "maximized at 150%" $name "$(bbox $name-max150b $c HEADLESS-1)" 1280
        randr --output HEADLESS-1 --scale 1; sleep "$SETTLE"
        check_max "still maximized at 100%" $name "$(bbox $name-max100b $c HEADLESS-1)" 1280
        ctl "$t" restore >/dev/null; sleep "$SETTLE"
        check "restored at 100%" $name "$(bbox $name-res100b $c HEADLESS-1)" 100
        echo "  $name: $(ctl "$t" state)"
    done
elif [ "$CASE" = move ]; then
    read -r ml mt mr mb < <(awk '$1 > 0 || $2 > 0' "$OUT/monitors.txt" | head -1)
    [ -n "$ml" ] || { echo "FAIL Win32 sees one monitor only"; RC=1; }
    for name in $NAMES; do
        [ -n "$ml" ] || break
        c=$(colour $name); t=$(title $name)
        read -r _ wl wt _ <<<"$(ctl "$t" state)"
        ctl "$t" move $((ml + 60)) $((mt + 60)) >/dev/null; sleep "$SETTLE"
        echo "  $name on HEADLESS-2: $(ctl "$t" state)"
        on1=$(bbox $name-moved-1 $c HEADLESS-1)
        [ "$on1" = none ] && echo "PASS moved ($name): no longer on HEADLESS-1" || { RC=1; echo "FAIL moved ($name): still on HEADLESS-1 at $on1"; }
        check "on HEADLESS-2 (150%)" $name "$(bbox $name-moved-2 $c HEADLESS-2)" 150 anywhere
        ctl "$t" move $wl $wt >/dev/null; sleep "$SETTLE"
        echo "  $name back: $(ctl "$t" state)"
        check "back on HEADLESS-1 (100%)" $name "$(bbox $name-back $c HEADLESS-1)" 100
        on2=$(bbox $name-back-2 $c HEADLESS-2)
        [ "$on2" = none ] && echo "PASS back ($name): no longer on HEADLESS-2" || { RC=1; echo "FAIL back ($name): still on HEADLESS-2 at $on2"; }
    done
fi
touch "$WINEPREFIX/drive_c/scale.stop"
pkill -9 -x foot
./server/wineserver -k; pkill -9 -x wineserver; pkill -9 -x services.exe
kill $CPID 2>/dev/null; wait $CPID 2>/dev/null
rm -rf "$R"
[ "$RC" = 0 ] && echo "PASS placement case $CASE" || echo "FAIL placement case $CASE (screenshots in $OUT)"
exit $RC
