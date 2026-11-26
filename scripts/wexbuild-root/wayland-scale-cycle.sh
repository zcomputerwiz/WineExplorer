#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# wayland-scale-cycle.sh: the open "restored live scaling at 150%" item (docs/WAYLAND_L2.md, wayland-polish/item3-notes.md)
# without a VM. Headless wexcomp (server-side decoration), explorer /shell with WEX_WAYLAND_NATIVE_WINDOWS=1, and a real
# restored foot (needs the foot package) that Explorer hosts in a Wine frame. The output scale goes 100% -> 150% -> 100%
# CYCLES times; after each change (SETTLE seconds, default 10: the first seconds are transient) the physical bounding box
# of foot's background colour is read from a screenshot. A restored window must be back where it was at 100%, and at
# 150% at its physical place or its logical one (times 1.5), within TOL px (default 8), in every cycle.
# Run as root through `unshare -m` with a tmpfs on /tmp/.X11-unix (WSLg mounts it read-only): see wayland-class-shadow.sh.
# COMP, W (the wow64 build), OUT and CYCLES name the compositor, the build, the output directory and the cycle count.
COMP=${COMP:-/root/wexcomp-dev/build/labwc}
W=${W:-/root/WE-chrome/build/wow64}
OUT=${OUT:-/root/sc-out}
CYCLES=${CYCLES:-3}
SETTLE=${SETTLE:-10}
TOL=${TOL:-8}
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
(WAYLAND_DISPLAY=wex-shell-0 WEX_WAYLAND_NATIVE_WINDOWS=1 timeout 600 ./wine explorer.exe /shell >>"$L" 2>&1 &)
sleep 15
(WAYLAND_DISPLAY=wayland-0 GDK_BACKEND=wayland foot --title="Wex real native client" --app-id=wex-real -w240x180 \
    -o colors.background=338855 /usr/bin/sleep 900 >"$OUT/foot.log" 2>&1 &)
sleep 8

bbox() {   # bbox NAME: the screenshot of the output, then the bounding box (x0 y0 x1 y1) of foot's background colour
    WAYLAND_DISPLAY=wex-shell-0 grim "$OUT/$1.png"
    python3 - "$OUT/$1.png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB"); w, h = im.size; px = im.load()
xs = []; ys = []
for y in range(h):
    for x in range(w):
        if px[x, y] == (0x33, 0x88, 0x55): xs.append(x); ys.append(y)
print(min(xs), min(ys), max(xs), max(ys)) if xs else print("none")
PY
}
RC=0
check() {   # check LABEL "x0 y0 x1 y1" PERCENT: the top left against the first reading at that scale
    local label=$1 box=$2 percent=$3 x0 y0 ex ey ok=0
    read -r x0 y0 _ <<<"$box"
    if [ "$box" = none ]; then RC=1; echo "FAIL $label: no foot window on the screen"; return; fi
    # at 100% the window must be where it was. At 150% either its physical place (the Wine shell keeps it) or its
    # logical place (the compositor keeps it: physical = 100% position x 1.5) is consistent; both stay on the screen
    for ex_ey in "$BX0 $BY0" "$((BX0 * percent / 100)) $((BY0 * percent / 100))"; do
        read -r ex ey <<<"$ex_ey"
        if [ $((x0 > ex ? x0 - ex : ex - x0)) -le "$TOL" ] && [ $((y0 > ey ? y0 - ey : ey - y0)) -le "$TOL" ]; then ok=1; fi
        [ "$percent" = 100 ] && break
    done
    if [ "$ok" = 1 ]; then echo "PASS $label: bbox $box"
    else RC=1; echo "FAIL $label: bbox $box, top left expected $BX0,$BY0 (or at 150%: $((BX0 * 3 / 2)),$((BY0 * 3 / 2)))"; fi
}
first=$(bbox start-100)
read -r BX0 BY0 _ <<<"$first"
echo "restored foot at 100%: bbox $first"
for cycle in $(seq 1 "$CYCLES"); do
    WAYLAND_DISPLAY=wex-shell-0 wlr-randr --output HEADLESS-1 --scale 1.5
    sleep "$SETTLE"
    check "cycle $cycle at 150%" "$(bbox c$cycle-150)" 150
    WAYLAND_DISPLAY=wex-shell-0 wlr-randr --output HEADLESS-1 --scale 1
    sleep "$SETTLE"
    check "cycle $cycle back at 100%" "$(bbox c$cycle-100)" 100
done
pkill -9 -x foot
./server/wineserver -k; pkill -9 -x wineserver; pkill -9 -x services.exe
kill $CPID 2>/dev/null; wait $CPID 2>/dev/null
rm -rf "$R"
[ "$RC" = 0 ] && echo "PASS a restored hosted window keeps its place through live scale changes" \
              || echo "FAIL a restored hosted window moved on a live scale change (screenshots in $OUT)"
exit $RC
