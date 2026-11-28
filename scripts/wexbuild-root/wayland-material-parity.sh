#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# wayland-material-parity.sh: the on-screen colour of each system material under wexcomp, against the shared default
# tint table (wex_material_default_tint, src/include/wexhost.h), the same eight windows and the same expected values as
# x11-material-parity.sh (material-parity-check.py). Headless wexcomp (pixman, so the compositor draws the tint alone;
# set WLR_RENDERER=gles2 where there is a GPU to include SceneFX's blur, which changes nothing over a flat desktop),
# explorer /shell with composition on, scripts/dwm-material-parity-probe.c; the output is read with grim. Run it
# through run.sh style `unshare -m` with a tmpfs on /tmp/.X11-unix (see wayland-scale-cycle.sh). COMP, W, PROBE, OUT,
# BG, TOL as in x11-material-parity.sh.
RC=0
HERE=$(cd "$(dirname "$0")" && pwd)
COMP=${COMP:-/root/wexcomp-dev/build/labwc}
W=${W:-/root/WE-chrome/build/wow64}
PROBE=${PROBE:-/root/dwm-material-parity-probe.exe}
OUT=${OUT:-/root/wmp-out}
BG=${BG:-3a6ea5}
TOL=${TOL:-6}
mkdir -p "$OUT"
R=$(mktemp -d /tmp/wex-wmp-XXXXXX); chmod 700 "$R"
export XDG_RUNTIME_DIR=$R WLR_BACKENDS=headless WLR_RENDERER=${WLR_RENDERER:-pixman} WLR_HEADLESS_OUTPUTS=1 WEX_SHELL_SOCKET=wex-shell-0
unset DISPLAY
pkill -9 -x wineserver; pkill -9 -x services.exe; sleep 1
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
export WINEPREFIX=${WINEPREFIX:-/root/wmp-pfx} WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=${WINEDEBUG:--all}
cd "$W"
if [ ! -d "$WINEPREFIX/drive_c" ]; then
    WAYLAND_DISPLAY=wex-shell-0 timeout 150 ./wine wineboot -i >"$OUT/boot.log" 2>&1
    timeout 30 ./server/wineserver -w
fi
export WAYLAND_DISPLAY=wex-shell-0 WEX_WAYLAND_NATIVE_WINDOWS=1   # the shell adopts the Wayland window manager only with this
R8=$((16#${BG:0:2})); G8=$((16#${BG:2:2})); B8=$((16#${BG:4:2}))
./wine reg add 'HKCU\Control Panel\Colors' /v Background /t REG_SZ /d "$R8 $G8 $B8" /f >/dev/null 2>&1
./wine reg add 'HKCU\Control Panel\Desktop' /v Wallpaper /t REG_SZ /d "" /f >/dev/null 2>&1
for v in Composition DwmService DwmAlpha DwmMaterials DwmFrameEffects; do
    ./wine reg add 'HKCU\Software\WineExplorer\Shell' /v $v /t REG_DWORD /d 1 /f >/dev/null 2>&1
done
./wine reg add 'HKCU\Software\Wine\Drivers' /v Graphics /t REG_SZ /d wayland /f >/dev/null 2>&1
# FX=0: no frame effects (class shadow and corner rounding), to tell them from the material
[ -n "$FX" ] && ./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmFrameEffects /t REG_DWORD /d "$FX" /f >/dev/null 2>&1
timeout 30 ./server/wineserver -w
(timeout 120 ./wine explorer.exe /shell >"$OUT/shell.log" 2>&1 &)
sleep 15
(timeout 90 ./wine "Z:$PROBE" 60 >"$OUT/probe.log" 2>&1 &)
sleep 12
grim -o HEADLESS-1 "$OUT/screen.png"
python3 "$HERE/material-parity-check.py" "$OUT/screen.png" "$BG" "$TOL" || RC=1
./server/wineserver -k; pkill -9 -x wineserver; pkill -9 -x services.exe
kill $CPID 2>/dev/null; wait $CPID 2>/dev/null
rm -rf "$R"
exit $RC
