#!/bin/bash
cd /root/WE-prefetch/build/wow64
pkill -9 wineserver; sleep 1
export WINEPREFIX=/root/dwm-pfx6 WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=-all DISPLAY=:0
./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmService /t REG_DWORD /d $1 /f >/dev/null 2>&1
(timeout 40 ./wine explorer.exe /desktop=dwmshot,800x600 notepad >/dev/null 2>&1 &)
sleep 15
ps -eo pid,cmd | grep -c "[d]wm.exe"
import -window root /root/shot-dwm$1.png
