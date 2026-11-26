#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# wayland-class-shadow.sh: D3 without a VM. Headless wexcomp, explorer /shell with desktop composition on, and
# class-shadow-probe.exe (scripts/class-shadow-probe.c: an orange 200x120 popup at 400,300 whose class has
# CS_DROPSHADOW). The pixels just below and right of the window must be darker than the same row or column 30 pixels
# further out (a shadow falls off over a dozen pixels). Run it through `unshare -m` with a tmpfs on /tmp/.X11-unix
# (WSLg mounts it read-only): see wayland-class-shadow-run.sh. COMP and W name the compositor and the wow64 build.
COMP=${COMP:-/root/wexcomp-dev/build/labwc}
W=${W:-/root/WE-chrome/build/wow64}
PROBE=${PROBE:-/root/class-shadow-probe.exe}
OUT=${OUT:-/root/wlshadow-out}
mkdir -p $OUT
R=$(mktemp -d /tmp/wex-wlshadow-XXXXXX); chmod 700 $R
export XDG_RUNTIME_DIR=$R WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 WEX_SHELL_SOCKET=wex-shell-0
unset DISPLAY
pkill -9 -x wineserver; pkill -9 -x services.exe; sleep 1
mkdir -p $R/cfg
cat > $R/cfg/rc.xml <<'EOF'
<?xml version="1.0"?>
<labwc_config>
  <core><decoration>none</decoration></core>
  <placement><policy>center</policy></placement>
</labwc_config>
EOF
$COMP -C $R/cfg > $OUT/comp.log 2>&1 &
CPID=$!
for i in $(seq 1 60); do [ -S $R/wex-shell-0 ] && break; sleep 0.2; done
[ -S $R/wex-shell-0 ] || { echo "compositor socket missing"; head $OUT/comp.log; kill $CPID; exit 1; }
WAYLAND_DISPLAY=wex-shell-0 wlr-randr --output HEADLESS-1 --custom-mode 1280x800 >/dev/null 2>&1 || true
export WAYLAND_DISPLAY=wex-shell-0 WEX_WAYLAND_NATIVE_WINDOWS=1
export WINEPREFIX=${WINEPREFIX:-/root/wlshadow-pfx} WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=${WINEDEBUG:--all}
L=$OUT/wine.log; rm -f $L
cd $W
if [ ! -d $WINEPREFIX/drive_c ]; then
    timeout 150 ./wine wineboot -i >>$L 2>&1
    ./wine reg add 'HKCU\Software\Wine\Drivers' /v Graphics /t REG_SZ /d wayland /f >>$L 2>&1
    ./wine reg add 'HKCU\Software\WineExplorer\Shell' /v Composition /t REG_DWORD /d 1 /f >>$L 2>&1
    timeout 30 ./server/wineserver -w
fi
rm -f $WINEPREFIX/drive_c/shadow.stop
(timeout 200 ./wine explorer.exe /shell >>$L 2>&1 &)
sleep 15
(timeout 120 ./wine Z:$PROBE >>$L 2>&1 &)
sleep 8
grim $OUT/shadow.png
touch $WINEPREFIX/drive_c/shadow.stop
sleep 2
./server/wineserver -k; pkill -9 -x wineserver; pkill -9 -x services.exe
kill $CPID 2>/dev/null; wait $CPID 2>/dev/null
rm -rf $R
python3 - <<'EOF'
from PIL import Image
im = Image.open("/root/wlshadow-out/shadow.png").convert("RGB")
w, h = im.size
x0, y0, x1, y1 = 400, 300, 600, 420
print("screen", w, h, "window pixel", im.getpixel((500, 360)))
rc = 0
bg = im.getpixel((x1 + 30, 360))
def dark(p): return sum(bg) - sum(p)
# XP's class shadow (the X11 compositor's six nested rectangles, 8 px right of and below the window, fading out):
# below and right: darker at the edge than 6 px out, and gone by 12 px; above and left: none at all
for name, at in (("bottom", lambda d: (500, y1 + d)), ("right", lambda d: (x1 + d, 360))):
    a, b, c = (im.getpixel(at(d)) for d in (1, 6, 12))
    print(f"{name}: 1 px out {a} (darker by {dark(a)}), 6 px {b} (by {dark(b)}), 12 px {c} (by {dark(c)})")
    if not (dark(a) >= 20 and dark(a) > dark(b) > 0 and dark(c) == 0):
        rc = 1; print(f"  FAIL {name}: not a shadow that falls off over 8 px")
for name, p in (("top", (500, y0 - 2)), ("left", (x0 - 2, 360))):
    print(f"{name}: 2 px out {im.getpixel(p)} (darker by {dark(im.getpixel(p))})")
    if dark(im.getpixel(p)) != 0: rc = 1; print(f"  FAIL {name}: a shadow where XP's has none")
print("PASS XP's class shadow around a CS_DROPSHADOW window" if rc == 0 else "FAIL the class shadow is not XP's")
raise SystemExit(rc)
EOF
