#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# wayland-scale-cycle.sh: live output scale changes of two Wine windows, without a VM (docs/WAYLAND_DEFAULT.md,
# "Restored live scaling"). Headless wexcomp (server-side decoration), explorer /shell with WEX_WAYLAND_NATIVE_WINDOWS=1,
# a real restored foot (needs the foot package) that Explorer hosts in a Wine frame, and a plain Wine window
# (scripts/scale-window-probe.c, pure blue client area). The output scale goes 100% -> 150% -> 100% CYCLES times; after
# each change (SETTLE seconds, default 10: the first seconds are transient) the physical bounding box of each window's
# colour is read from a screenshot. The rule (Windows' Per-Monitor-V2 model, owned by the shell: wex_scale_changed in
# wexshell/linux_windows.c): a window keeps its TOP LEFT and its device-independent size, so its physical size follows
# the scale ratio. Checked in every cycle, both directions, for both windows: the top left within TOL px (default 10) of
# where it was at 100%; the size at 150% 1.5 times the 100% size and back at 100% the original, each within 15% or 24 px
# (foot snaps to its cell grid).
# Run as root through `unshare -m` with a tmpfs on /tmp/.X11-unix (WSLg mounts it read-only): see wayland-class-shadow.sh.
# COMP, W (the wow64 build), OUT, PROBE (the plain window's exe) and CYCLES name the compositor, the build, the output
# directory, the probe and the cycle count.
COMP=${COMP:-/root/wexcomp-dev/build/labwc}
W=${W:-/root/WE-chrome/build/wow64}
OUT=${OUT:-/root/sc-out}
PROBE=${PROBE:-/root/scale-window-probe.exe}
CYCLES=${CYCLES:-3}
WITH_FOOT=${WITH_FOOT:-1}   # 0: no hosted foot, the plain window only (the default Wayland session does not host natives)
WINDOWS="foot plain"; [ "$WITH_FOOT" = 0 ] && WINDOWS=plain
SETTLE=${SETTLE:-10}
TOL=${TOL:-10}
mkdir -p "$OUT"
R=$(mktemp -d /tmp/wex-sc-XXXXXX); chmod 700 "$R"
export XDG_RUNTIME_DIR=$R WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 WEX_SHELL_SOCKET=wex-shell-0
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
WAYLAND_DISPLAY=wex-shell-0 wlr-randr --output HEADLESS-1 --custom-mode 1280x800 >/dev/null 2>&1 || true
export WINEPREFIX=${WINEPREFIX:-/root/sc-pfx} WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=${WINEDEBUG:--all}
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
if [ "$WITH_FOOT" != 0 ]; then
    (WAYLAND_DISPLAY=wayland-0 GDK_BACKEND=wayland foot --title="Wex real native client" --app-id=wex-real -w240x180 \
        -o colors.background=338855 /usr/bin/sleep 1500 >"$OUT/foot.log" 2>&1 &)
fi
sleep 8

bbox() {   # bbox NAME HEXCOLOUR: a screenshot of the output, then the bounding box (x0 y0 x1 y1) of that colour
    WAYLAND_DISPLAY=wex-shell-0 grim "$OUT/$1.png"
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
check() {   # check LABEL NAME "x0 y0 x1 y1" PERCENT: top left kept; size at PERCENT of the 100% size
    local label=$1 name=$2 box=$3 percent=$4 x0 y0 x1 y1 w h ew eh tw th
    read -r x0 y0 x1 y1 <<<"$box"
    if [ "$box" = none ]; then RC=1; echo "FAIL $label ($name): no window on the screen"; return; fi
    w=$((x1 - x0 + 1)); h=$((y1 - y0 + 1))
    ew=$((OW[$name] * percent / 100)); eh=$((OH[$name] * percent / 100))
    tw=$((ew * 15 / 100)); [ $tw -lt 24 ] && tw=24
    th=$((eh * 15 / 100)); [ $th -lt 24 ] && th=24
    local ok=1 why=""
    [ $((x0 > OX0[$name] ? x0 - OX0[$name] : OX0[$name] - x0)) -le "$TOL" ] || { ok=0; why="$why left $x0 (was ${OX0[$name]})"; }
    [ $((y0 > OY0[$name] ? y0 - OY0[$name] : OY0[$name] - y0)) -le "$TOL" ] || { ok=0; why="$why top $y0 (was ${OY0[$name]})"; }
    [ $((w > ew ? w - ew : ew - w)) -le $tw ] || { ok=0; why="$why width $w (want about $ew)"; }
    [ $((h > eh ? h - eh : eh - h)) -le $th ] || { ok=0; why="$why height $h (want about $eh)"; }
    if [ "$ok" = 1 ]; then echo "PASS $label ($name): bbox $box"
    else RC=1; echo "FAIL $label ($name): bbox $box;$why"; fi
}
for name in $WINDOWS; do
    [ $name = foot ] && colour=338855 || colour=0000ff
    first=$(bbox start-$name $colour)
    [ "$first" != none ] || { echo "FAIL the $name window is not on the screen at 100%"; RC=1; first="0 0 1 1"; }
    read -r x0 y0 x1 y1 <<<"$first"
    OX0[$name]=$x0; OY0[$name]=$y0; OW[$name]=$((x1 - x0 + 1)); OH[$name]=$((y1 - y0 + 1))
    echo "$name at 100%: bbox $first"
done
for cycle in $(seq 1 "$CYCLES"); do
    WAYLAND_DISPLAY=wex-shell-0 wlr-randr --output HEADLESS-1 --scale 1.5
    sleep "$SETTLE"
    for name in $WINDOWS; do
        [ $name = foot ] && colour=338855 || colour=0000ff
        check "cycle $cycle at 150%" $name "$(bbox c$cycle-150-$name $colour)" 150
    done
    WAYLAND_DISPLAY=wex-shell-0 wlr-randr --output HEADLESS-1 --scale 1
    sleep "$SETTLE"
    for name in $WINDOWS; do
        [ $name = foot ] && colour=338855 || colour=0000ff
        check "cycle $cycle back at 100%" $name "$(bbox c$cycle-100-$name $colour)" 100
    done
done
touch "$WINEPREFIX/drive_c/scale.stop"
pkill -9 -x foot
./server/wineserver -k; pkill -9 -x wineserver; pkill -9 -x services.exe
kill $CPID 2>/dev/null; wait $CPID 2>/dev/null
rm -rf "$R"
[ "$RC" = 0 ] && echo "PASS the windows keep their top left and follow the scale through live scale changes" \
              || echo "FAIL a window moved or kept the wrong size on a live scale change (screenshots in $OUT)"
exit $RC
