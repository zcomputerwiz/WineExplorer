#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# check-wayland-dwm.sh: headless labwc (the wexcomp build, COMP) + the Wine shell on the Wayland driver + a probe exe
# from /root, then a grim screenshot (DWM P5: set_effects, glass alpha). Needs grim, wlr-randr, the wow64 build tree.
# env: PROBE (exe in /root), PARGS, DA FX MAT FS COL (gates), HOLD (seconds the probe runs, default 12), OUT (png name)
COMP=${COMP:-/root/wexcomp-dev/build/labwc}
W=/root/WE-prefetch/build/wow64
R=$(mktemp -d /tmp/wex-p5-XXXXXX); chmod 700 $R
export XDG_RUNTIME_DIR=$R WLR_BACKENDS=headless WLR_RENDERER=pixman WLR_HEADLESS_OUTPUTS=1 LABWC_WEX_SHELL_SOCKET=wex-shell-0
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
$COMP -C $R/cfg > $R/comp.log 2>&1 &
CPID=$!
for i in $(seq 1 60); do [ -S $R/wex-shell-0 ] && [ -S $R/wayland-0 ] && break; sleep 0.2; done
[ -S $R/wex-shell-0 ] || { echo "compositor sockets missing"; cat $R/comp.log | head; kill $CPID; exit 1; }
WAYLAND_DISPLAY=wayland-0 wlr-randr --output HEADLESS-1 --custom-mode 1280x800 >/dev/null 2>&1 || true
export WAYLAND_DISPLAY=wex-shell-0 WEX_WAYLAND_NATIVE_WINDOWS=1
export WINEPREFIX=/root/dwm-pfx9 WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=${WINEDEBUG:-err+all,trace+wexshell,trace+wexhost}
L=/root/p5-wine.log; rm -f $L
rm -rf $WINEPREFIX
cd $W
timeout 150 ./wine wineboot -i >>$L 2>&1
reg() { ./wine reg add "$1" /v "$2" /t REG_DWORD /d "$3" /f >>$L 2>&1; }
S='HKCU\Software\WineExplorer\Shell'
reg "$S" Composition 1; reg "$S" DwmService 1
./wine reg add 'HKCU\Software\Wine\Drivers' /v Graphics /t REG_SZ /d wayland /f >>$L 2>&1
[ -n "$DA" ] && reg "$S" DwmAlpha $DA
[ -n "$FX" ] && reg "$S" DwmFrameEffects $FX
[ -n "$MAT" ] && reg "$S" DwmMaterials $MAT
[ -n "$FS" ] && reg "$S" DwmFrameStyle $FS
[ -n "$COL" ] && reg 'HKCU\Software\Microsoft\Windows\DWM' ColorizationColor $COL
timeout 30 ./server/wineserver -w
(timeout 120 ./wine explorer.exe /shell >>$L 2>&1 &)
sleep 15
(timeout 60 ./wine "Z:\\root\\${PROBE:-dwm-blur-visual-probe}.exe" ${PARGS} 'Z:\root\p5-probe.log' >>$L 2>&1 &)
sleep ${HOLD:-12}
grim /root/${OUT:-p5-wl}.png
cat /root/p5-probe.log 2>/dev/null | head -8
grep -a "wayland .* radius\|set_effects\|err:wex\|bridge thread" $L | cut -c1-200 | head -12
grep -ai "error\|assert" $R/comp.log | head -5
./server/wineserver -k; pkill -9 -x wineserver; pkill -9 -x services.exe
kill $CPID 2>/dev/null; wait $CPID 2>/dev/null
cp $R/comp.log /root/p5-comp.log; rm -rf $R /root/p5-probe.log
