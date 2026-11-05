#!/bin/bash
cd /root/WE-prefetch/build/wow64
pkill -9 wineserver; sleep 1
export WINEPREFIX=/root/dwm-pfx6 WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=err+all,warn+explorer
rm -rf $WINEPREFIX
timeout 120 ./wine wineboot -i >/dev/null 2>&1
./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmService /t REG_DWORD /d 1 /f >/dev/null 2>&1
(timeout 60 ./wine explorer.exe /desktop=dwmtest,800x600 > /root/p2-expl.log 2>&1 &)
sleep 12
echo "--- after start"; ps -eo pid,etime,cmd | grep -E "[d]wm.exe|[e]xplorer.exe"
pid=$(pgrep -f 'C:.windows.system32.dwm.exe' | head -1)
echo "kill $pid"; kill -9 $pid; sleep 3
echo "--- after kill"; ps -eo pid,etime,cmd | grep -E "[d]wm.exe"
sleep 50
echo "--- after explorer timeout"; ps -eo pid,etime,cmd | grep -E "[d]wm.exe|[e]xplorer.exe"
tail -20 /root/p2-expl.log
