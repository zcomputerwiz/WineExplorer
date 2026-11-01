#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer Desktop Environment Project
# Runs scripts/secure_desktop_probe.c under Xvfb (DISPLAY must name it) with the X11 driver, and plays the host
# side: window snapshots with xwininfo and real keyboard and pointer events with xdotool.
# usage: secure_desktop_probe.sh <label> <plain|vd> <open|sys>   (vd = explorer /desktop=Default,1024x768)
# Environment: PFX (prefix), WBIN (wine), WSRV (wineserver), ARCH (x86_64 or i686); the probe executables are
# built as /tmp/wexout/sd/sdprobe-<arch>.exe: <arch>-w64-mingw32-gcc -O1 -o sdprobe-<arch>.exe
# scripts/secure_desktop_probe.c -ladvapi32 -luser32 (x86_64 and i686).
label=$1; mode=$2; sdmode=$3
run=/tmp/wexout/sd/$label; rm -rf $run; mkdir -p $run
export WINEPREFIX=${PFX:-/tmp/wexout/pfx1} WINEDEBUG=-all DISPLAY=${DISPLAY:-:77}
W=${WBIN:-/home/user/build/dev/wine}
exe=Z:\\tmp\\wexout\\sd\\sdprobe-${ARCH:-x86_64}.exe
wdir="Z:\\tmp\\wexout\\sd\\$label"
host=$run/host.log
snap() { # tag
  echo "== HOST SNAPSHOT $1" >> $host
  for n in SECURE-TARGET DEFAULT-DESKTOP-WINDOW ATTACKER-ON-SECURE; do
    for id in $(xdotool search --name "^$n\$" 2>/dev/null); do
      echo "$n id=$id: $(xwininfo -id $id | /usr/bin/grep -E 'Map State|Absolute upper-left X|Absolute upper-left Y|  Width|  Height' | tr -s ' ' | tr '\n' ';') $(xwininfo -tree -id $id | /usr/bin/grep -E 'Parent window id' | tr -s ' ')" >> $host
    done
  done
  echo "x11 active window: $(xdotool getactivewindow getwindowname 2>&1)  focus: $(xdotool getwindowfocus getwindowname 2>&1)" >> $host
  echo "mouse: $(xdotool getmouselocation 2>&1)" >> $host
}
center() { # name -> "x y"
  id=$(xdotool search --name "^$1\$" | head -1)
  eval $(xdotool getwindowgeometry --shell $id)
  echo "$id $X $Y $WIDTH $HEIGHT"
}
if [ $mode = vd ]; then
  $W explorer /desktop=Default,1024x768 "$exe" main "$wdir" $sdmode > $run/wine.out 2>&1 &
else
  $W "$exe" main "$wdir" $sdmode > $run/wine.out 2>&1 &
fi
for step in 1 2 3 4 5 7 8 6; do
  name=$(ls $run | grep -E '^s'$step'_.*\.at$' | head -1)
  until ls $run/s${step}_*.at >/dev/null 2>&1; do sleep 0.5; [ -f $run/main.done ] && break; done
  name=$(basename $(ls $run/s${step}_*.at | head -1) .at)
  echo "## step $name" >> $host
  case $step in
   1) snap "$name";;
   2) snap "$name"
      set -- $(center SECURE-TARGET); id=$1; tx=$(( $2 + 20 + 40 )); ty=$(( $3 + 20 + 20 + 20 ))
      echo "target X geometry: $*  (host click at $tx,$ty on the Approve button area, approx)" >> $host
      xdotool mousemove $tx $ty click 1; sleep 0.3
      xdotool key --window $id z 2>>$host; xdotool key y; sleep 0.5
      echo "(host pointer/key sent; target log lines below at end)" >> $host
      ;;
   3) xdotool key q; xdotool mousemove 5 5 click 1; sleep 0.3; xdotool mousemove 20 20; sleep 0.5
      snap "$name";;
   4) snap "$name";;
   5) set -- $(center DEFAULT-DESKTOP-WINDOW); echo "defwin geometry $*" >> $host
      snap "$name"
      xdotool mousemove $(( $2 + 100 )) $(( $3 + 100 )) click 1; sleep 0.6
      snap "$name-after-click";;
   7) snap "$name-before"; xdotool mousemove 1000 760; sleep 0.3; xdotool mousemove 1010 770; sleep 0.5; snap "$name-after";;
   8) xdotool mousemove 1000 760 click 1; sleep 0.5;;
   6) snap "$name";;
  esac
  touch $run/$name.go
done
if [ $mode = vd ]; then until [ -f $run/main.done ]; do sleep 0.5; done; sleep 1; $W wineserver -k 2>/dev/null; ${WSRV:-/home/user/build/dev/server/wineserver} -k; fi
wait
cat $run/defwin.log >> $host 2>/dev/null
