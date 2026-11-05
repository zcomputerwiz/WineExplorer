#!/bin/bash
# p4-visual.sh MODE : shell on Xvfb with composition, corner probe, root screenshot. MODE = on (DwmService=1) or off
# Every Wine process appends to /root/p4-wine.log (O_APPEND, so inherited descriptors never overwrite each other).
MODE=${1:-on}
L=/root/p4-wine.log
cd /root/WE-prefetch/build/wow64
pkill -9 -x wineserver; pkill -9 -x services.exe; pkill -9 Xvfb; sleep 1
unset WAYLAND_DISPLAY
export DISPLAY=:77 WINEPREFIX=/root/dwm-pfx7 WINEDLLOVERRIDES="mscoree,mshtml="
export WINEDEBUG=${WINEDEBUG:-err+all,trace+wexshell,trace+wexhost,warn+explorer}
rm -rf /root/fb $L; mkdir -p /root/fb
rm -f /tmp/.X77-lock /tmp/.X11-unix/X77
Xvfb :77 -screen 0 ${RES:-1024x768}x24 -fbdir /root/fb +extension Composite +extension DAMAGE +extension RENDER >/dev/null 2>&1 &
sleep 2
xdpyinfo -display :77 >/dev/null 2>&1 || { echo "Xvfb :77 did not start"; exit 1; }
rm -rf $WINEPREFIX
timeout 150 ./wine wineboot -i >>$L 2>&1
./wine reg add 'HKCU\Software\WineExplorer\Shell' /v Composition /t REG_DWORD /d ${COMP:-1} /f >>$L 2>&1
v=1; [ "$MODE" = off ] && v=0
./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmService /t REG_DWORD /d $v /f >>$L 2>&1
./wine reg add "HKCU\Software\Wine\Drivers" /v Graphics /t REG_SZ /d x11 /f >>$L 2>&1
[ -n "$TB" ] && ./wine reg add 'HKCU\Software\WineExplorer\Shell' /v TaskbarThumbnails /t REG_DWORD /d $TB /f >>$L 2>&1
[ -n "$FX" ] && ./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmFrameEffects /t REG_DWORD /d $FX /f >>$L 2>&1
[ -n "$DA" ] && ./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmAlpha /t REG_DWORD /d $DA /f >>$L 2>&1
[ -n "$MAT" ] && ./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmMaterials /t REG_DWORD /d $MAT /f >>$L 2>&1
[ -n "$PEEK" ] && ./wine reg add 'HKCU\Software\WineExplorer\Shell' /v TaskbarPeek /t REG_DWORD /d $PEEK /f >>$L 2>&1
[ -n "$FS" ] && ./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmFrameStyle /t REG_DWORD /d $FS /f >>$L 2>&1
[ -n "$COL" ] && ./wine reg add 'HKCU\Software\Microsoft\Windows\DWM' /v ColorizationColor /t REG_DWORD /d $COL /f >>$L 2>&1
[ -n "$AT" ] && ./wine reg add 'HKCU\Software\WineExplorer\Shell' /v AltTabThumbnails /t REG_DWORD /d $AT /f >>$L 2>&1
timeout 30 ./server/wineserver -w
(timeout 100 ./wine explorer.exe /desktop=Default,${RES:-1024x768} /shell >>$L 2>&1 &)
sleep 15
(timeout 60 ./wine explorer.exe /desktop=Default,${RES:-1024x768} "Z:\\root\\${PROBE:-dwm-corner-visual-probe}.exe" ${PARGS} 'Z:\root\p4-probe.log' >>$L 2>&1 &)
sleep 12
# HOVER="x y": rest the pointer there (a taskbar button) before the screenshot
if [ -n "$HOVER" ]; then DISPLAY=:77 xdotool mousemove $HOVER; sleep 0.3; DISPLAY=:77 xdotool mousemove_relative 1 0; sleep 2; fi
# HOVER2="x y": then move onto the preview popup and rest there (Peek)
if [ -n "$HOVER2" ]; then DISPLAY=:77 xdotool mousemove $HOVER2; sleep 0.3; DISPLAY=:77 xdotool mousemove_relative 1 0; sleep 2; fi
# CLICK="x y": click there (a caption button) before the screenshot
if [ -n "$CLICK" ]; then DISPLAY=:77 xdotool mousemove $CLICK; sleep 0.3; DISPLAY=:77 xdotool click 1; sleep 2; fi
# ALTTAB=1: hold Alt and press Tab, so the switcher is open in the screenshot
if [ -n "$ALTTAB" ]; then DISPLAY=:77 xdotool mousemove 750 300 click 1; sleep 1
  d=$(xwininfo -display :77 -root -tree | grep -a '"explorer.exe"' | grep -a -v "1x1+" | head -1 | awk '{print $1}')
  echo "focus desktop window $d"; DISPLAY=:77 xdotool windowfocus $d; sleep 0.5; DISPLAY=:77 xdotool keydown alt; sleep 0.3; DISPLAY=:77 xdotool key Tab; sleep 2; fi
import -display :77 -window root /root/p4-$MODE.png
# XWIN="name": depth and DWM properties of that X window
if [ -n "$XWIN" ]; then for id in $(xwininfo -display :77 -root -tree | grep -a "\"$XWIN\"" | awk '{print $1}'); do
  xwininfo -display :77 -id $id | grep -a "Depth\|Width\|Height"; xprop -display :77 -id $id _WEX_DWM_MARGINS
  import -display :77 -window $id png32:/root/p4-xwin.png; for p in 20,50 200,100 200,200; do echo "pixel $p: $(convert /root/p4-xwin.png -format "%[pixel:p{$p}]" info:)"; done; done; fi
[ -n "$ALTTAB" ] && DISPLAY=:77 xdotool keyup alt
grep -a "^window \|composition\|bridge\|poll:\|xwin\|no broker\|no x11\|unavailable\|wexhost:\|err:wex" $L | cut -c1-200 | head -40
cat /root/p4-probe.log; rm -f /root/p4-probe.log
xwininfo -display :77 -root -tree | grep -a "corner\|backdrop" | head
ps -eo args | grep -a "[d]wm.exe" | cut -c1-80
./server/wineserver -k; pkill -9 -x wineserver; pkill -9 -x services.exe; pkill -9 Xvfb; rm -rf $WINEPREFIX
