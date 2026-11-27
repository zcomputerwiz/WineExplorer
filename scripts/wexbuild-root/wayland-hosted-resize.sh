#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# wayland-hosted-resize.sh: an interactive resize of a hosted native window, without a VM (docs/WAYLAND_DEFAULT.md,
# "Restored live scaling": the accept-committed-size case). Headless wexcomp (server-side decoration), explorer /shell
# with WEX_WAYLAND_NATIVE_WINDOWS=1 and a real foot (it snaps to its cell grid) hosted in a Wine frame. A virtual
# pointer (scripts/wex-vpointer.c) presses on the frame's bottom-right corner and drags it STEPS times by DX,DY, PAUSE ms
# apart, then releases. From wexhost's trace (+timestamp), every resize transaction of the frame: the time from
# "accepted" to "complete" (the stall: how long the frame waits for the native client), and any that end otherwise.
# Checked (LIVE=1, "show window contents while dragging", the default here): every transaction completes, none stalls
# longer than MAXSTALL ms (default 500) or is superseded within that by the next step, and the last completes; LIVE=0 (an outline drag): the last completes at the release. Both: the frame grew, and at the
# end foot fills the frame's client area (its bounding box matches the frame's client box within TOL px, default 2) at
# the size foot chose (its cell grid), with no gap and no overlap. COMP, W, OUT, VP name the compositor, the build, the
# output directory and the wex-vpointer binary.
COMP=${COMP:-/root/wexcomp-dev/build/labwc}
W=${W:-/root/WE-chrome/build/wow64}
OUT=${OUT:-/root/hr-out}
VP=${VP:-/root/wex-vpointer}
STEPS=${STEPS:-8}; DX=${DX:-20}; DY=${DY:-15}; PAUSE=${PAUSE:-400}
MAXSTALL=${MAXSTALL:-500}; TOL=${TOL:-2}
RC=0
mkdir -p "$OUT"
R=$(mktemp -d /tmp/wex-hr-XXXXXX); chmod 700 "$R"
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
export WINEPREFIX=${WINEPREFIX:-/root/hr-pfx} WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"
L=$OUT/wine.log; rm -f "$L"
cd "$W"
if [ ! -d "$WINEPREFIX/drive_c" ]; then
    WAYLAND_DISPLAY=wex-shell-0 WINEDEBUG=-all timeout 150 ./wine wineboot -i >>"$L" 2>&1
    WAYLAND_DISPLAY=wex-shell-0 WINEDEBUG=-all ./wine reg add 'HKCU\Software\Wine\Drivers' /v Graphics /t REG_SZ /d wayland /f >>"$L" 2>&1
    timeout 30 ./server/wineserver -w
fi
# LIVE=1 (default): "show window contents while dragging" on, as XP has it, so every step is published during the drag;
# LIVE=0: an outline drag, the frame changes once at the release
WAYLAND_DISPLAY=wex-shell-0 WINEDEBUG=-all ./wine reg add 'HKCU\Control Panel\Desktop' /v DragFullWindows /t REG_SZ \
    /d "${LIVE:-1}" /f >>"$L" 2>&1
timeout 30 ./server/wineserver -w
(WAYLAND_DISPLAY=wex-shell-0 WEX_WAYLAND_NATIVE_WINDOWS=1 WINEDEBUG=-all,+timestamp,trace+wexhost timeout 600 ./wine explorer.exe /shell >>"$L" 2>&1 &)
sleep 15
(WAYLAND_DISPLAY=wayland-0 GDK_BACKEND=wayland foot --title="Wex real native client" --app-id=wex-real -w240x180 \
    -o colors.background=338855 /usr/bin/sleep 900 >"$OUT/foot.log" 2>&1 &)
sleep 8

bbox() {   # bbox NAME HEXCOLOUR: a screenshot, then the bounding box (x0 y0 x1 y1) of that colour
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
read -r X0 Y0 X1 Y1 <<<"$(bbox start 338855)"
echo "hosted foot before the drag: $X0,$Y0 $X1,$Y1"
[ "$X0" = none ] && { echo "FAIL no hosted foot on the screen"; kill $CPID; exit 1; }
# the frame's bottom-right corner: 2 px outside foot's box, inside the frame's 4 px sizing border
GX=$((X1 + 2)); GY=$((Y1 + 2))
CMD=(move $GX $GY sleep 300 down sleep 300)
for s in $(seq 1 "$STEPS"); do CMD+=(move $((GX + s * DX)) $((GY + s * DY)) sleep "$PAUSE"); done
CMD+=(up sleep 2000)
WAYLAND_DISPLAY=wex-shell-0 "$VP" 1280 800 "${CMD[@]}" > "$OUT/pointer.log" 2>&1
sleep 2
read -r F0 G0 F1 G1 <<<"$(bbox end 338855)"
echo "hosted foot after the drag: $F0,$G0 $F1,$G1"
python3 - "$L" "$MAXSTALL" "${LIVE:-1}" <<'PY' || RC=1
import re, sys
log, maxstall, live = sys.argv[1], int(sys.argv[2]), sys.argv[3] != "0"
acc = {}; done = {}; other = {}
for line in open(log, errors="replace"):
    m = re.match(r'\s*(\d+\.\d+):.*resize (\d+) stage (-?\d+)', line)
    if not m: continue
    t, serial, stage = float(m.group(1)), int(m.group(2)), int(m.group(3))
    if stage == 0: acc.setdefault(serial, t)
    elif stage == 3: done.setdefault(serial, t)
    elif stage >= 5: other.setdefault(serial, stage)
stalls = sorted((int((done[s] - acc[s]) * 1000), s) for s in acc if s in done)
print("transactions %d, complete %d, ended otherwise %s" % (len(acc), len(stalls), other or "none"))
if stalls:
    ms = [x for x, _ in stalls]
    print("stall ms: min %d, median %d, max %d (all: %s)" % (ms[0], ms[len(ms) // 2], ms[-1], " ".join(map(str, ms))))
if live:
    # every step is published during the drag, or superseded by the next step (a newer pointer position before the
    # client answered) within the stall limit; the last one completes
    order = sorted(acc)
    superseded = []
    for i, s in enumerate(order):
        if s in done: continue
        gap = int((acc[order[i + 1]] - acc[s]) * 1000) if i + 1 < len(order) else None
        superseded.append((s, gap))
    if superseded: print("superseded (serial, ms until the next was accepted):", superseded)
    ok = (bool(acc) and order[-1] in done and not other and stalls[-1][0] <= maxstall and
          all(gap is not None and gap <= maxstall for _, gap in superseded))
    print("PASS every resize transaction completed or was superseded within %d ms" % maxstall if ok else
          "FAIL resize transactions: one neither completed nor was superseded within %d ms" % maxstall)
else:
    # an outline drag: each step supersedes the last unpublished one, and the last completes at the release
    ok = bool(acc) and max(acc) in done and not other
    print("PASS the outline drag's last transaction completed at the release" if ok else
          "FAIL the outline drag's last transaction did not complete")
sys.exit(0 if ok else 1)
PY
if [ "${F0:-none}" = none ]; then RC=1; echo "FAIL no hosted foot after the drag"
else
    if [ $((F1 - F0)) -gt $((X1 - X0 + DX * STEPS / 2)) ] && [ $((G1 - G0)) -gt $((Y1 - Y0 + DY * STEPS / 2)) ]; then
        echo "PASS the hosted window grew: $((X1 - X0 + 1))x$((Y1 - Y0 + 1)) -> $((F1 - F0 + 1))x$((G1 - G0 + 1))"
    else RC=1; echo "FAIL the hosted window did not follow the drag: $((X1 - X0 + 1))x$((Y1 - Y0 + 1)) -> $((F1 - F0 + 1))x$((G1 - G0 + 1))"; fi
    if [ "$F0" -eq "$X0" ] && [ "$G0" -eq "$Y0" ]; then echo "PASS its top left stayed at $F0,$G0"
    else RC=1; echo "FAIL its top left moved: $X0,$Y0 -> $F0,$G0"; fi
    # the frame's client area around foot: the frame's own colour must not show between foot and the frame's border
    python3 - "$OUT/end.png" "$F0" "$G0" "$F1" "$G1" "$TOL" <<'PY' || RC=1
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB"); px = im.load()
x0, y0, x1, y1, tol = map(int, sys.argv[2:7])
# the frame's sizing border is 4 px: 1 px outside foot's box is the border if foot fills the client area
right = [px[x1 + d, (y0 + y1) // 2] for d in range(1, 4 + tol)]
below = [px[(x0 + x1) // 2, y1 + d] for d in range(1, 4 + tol)]
gap = [c for c in right + below if c == (0x33, 0x88, 0x55)]
print("right of foot:", right[:3], "below foot:", below[:3])
# foot fills its frame when the first pixels outside it are the frame's border, all the same colour, on both sides
ok = len(set(right[:2] + below[:2])) == 1 and not gap
print("PASS foot fills the frame's client area" if ok else "FAIL a gap or overlap between foot and its frame")
sys.exit(0 if ok else 1)
PY
fi
WINEPREFIX=$WINEPREFIX ./server/wineserver -k; pkill -9 -x foot; kill $CPID 2>/dev/null
exit $RC
