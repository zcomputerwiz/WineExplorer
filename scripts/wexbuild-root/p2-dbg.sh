cd /root/WE-prefetch/build/wow64
pkill -9 -x wineserver; sleep 1
export WINEPREFIX=/root/dwm-pfx6 WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=err+all,warn+explorer
rm -rf $WINEPREFIX
timeout 120 ./wine wineboot -i >/dev/null 2>&1
./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmService /t REG_DWORD /d 1 /f >/dev/null 2>&1
timeout 12 ./wine explorer.exe /desktop=dwmtest,800x600 > /root/p2-expl.log 2>&1
grep -a -i "dwm" /root/p2-expl.log | head -20 | cut -c1-250
./server/wineserver -k 2>/dev/null; pkill -9 -x wineserver; rm -rf /root/dwm-pfx6
