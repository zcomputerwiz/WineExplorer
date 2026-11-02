# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#!/bin/bash
# p3-attr-test.sh : run dwmapi_test.exe dwm_attr on a shell desktop that has a DWM broker, both architectures
cd /root/WE-prefetch/build/wow64
pkill -9 -x wineserver; pkill -9 -x services.exe; sleep 1
export WINEPREFIX=/root/dwm-pfx6 WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=err+explorer
rm -rf $WINEPREFIX
timeout 120 ./wine wineboot -i >/dev/null 2>&1
./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmService /t REG_DWORD /d 1 /f >/dev/null 2>&1
for n in attr64 attr32; do
  ./wine reg add "HKCU\\Software\\Wine\\Explorer\\Desktops\\$n" /v EnableShell /t REG_DWORD /d 1 /f >/dev/null 2>&1
done
for a in x86_64 i386; do
  n=attr64; [ $a = i386 ] && n=attr32
  exe="Z:\\root\\WE-prefetch\\build\\wow64\\dlls\\dwmapi\\tests\\$a-windows\\dwmapi_test.exe"
  echo "== $a"
  rm -f /root/p3-out-$a.txt
  timeout 90 ./wine explorer.exe /desktop=$n,800x600 cmd /c "$exe dwm_attr > Z:\root\p3-out-$a.txt 2>&1" > /root/p3-$a.log 2>&1 &
  sleep 25
  cat /root/p3-out-$a.txt | head -20 | cut -c1-200; echo "lines: $(wc -l < /root/p3-$a.log)"; ps -eo pid,args | grep -a "[d]wmapi_test" | cut -c1-120
  for p in $(pgrep -f "C:.windows.system32.[e]xplorer.exe /desktop=$n"); do kill -9 $p; done
  sleep 2
done
./server/wineserver -k; pkill -9 -x wineserver; pkill -9 -x services.exe; rm -rf $WINEPREFIX /root/p3-*.log /root/p3-out-*.txt
