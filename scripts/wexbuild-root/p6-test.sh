#!/bin/bash
# p6-test.sh : dwmapi_test.exe dwm_clock on the composited shell desktop (Xvfb), both architectures
L=/root/p6-wine.log
cd /root/WE-prefetch/build/wow64
pkill -9 -x wineserver; pkill -9 -x services.exe; pkill -9 Xvfb; sleep 1
unset WAYLAND_DISPLAY
export DISPLAY=:78 WINEPREFIX=/root/dwm-pfx8 WINEDLLOVERRIDES="mscoree,mshtml=" WINEDEBUG=err+all,trace+wexshell
rm -f $L
Xvfb :78 -screen 0 1024x768x24 +extension Composite +extension DAMAGE +extension RENDER >/dev/null 2>&1 &
sleep 2
rm -rf $WINEPREFIX
timeout 150 ./wine wineboot -i >>$L 2>&1
./wine reg add 'HKCU\Software\WineExplorer\Shell' /v Composition /t REG_DWORD /d 1 /f >>$L 2>&1
./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmService /t REG_DWORD /d 1 /f >>$L 2>&1
./wine reg add "HKCU\Software\Wine\Drivers" /v Graphics /t REG_SZ /d x11 /f >>$L 2>&1
[ -n "$FX" ] && ./wine reg add 'HKCU\Software\WineExplorer\Shell' /v DwmFrameEffects /t REG_DWORD /d $FX /f >>$L 2>&1
timeout 30 ./server/wineserver -w
(timeout 150 ./wine explorer.exe /desktop=Default,1024x768 /shell >>$L 2>&1 &)
sleep 15
for a in x86_64 i386; do
  exe="Z:\\root\\WE-prefetch\\build\\wow64\\dlls\\${MOD:-dwmapi}\\tests\\$a-windows\\${MOD:-dwmapi}_test.exe"
  echo "== $a"
  rm -f /root/p6-out-$a.txt
  timeout 120 ./wine explorer.exe /desktop=Default,1024x768 cmd /c "for %t in (${TESTS:-dwm_clock}) do $exe %t >> Z:\\root\\p6-out-$a.txt 2>&1" >>$L 2>&1 &
  sleep ${WAIT:-20}
  cut -c1-200 /root/p6-out-$a.txt | grep -a -E "tests executed|Test failed|Test succeeded inside todo|frames"
done
grep -a "frame clock\|err:wex" $L | head -5 | cut -c1-200
./server/wineserver -k; pkill -9 -x wineserver; pkill -9 -x services.exe; pkill -9 Xvfb; rm -rf $WINEPREFIX
