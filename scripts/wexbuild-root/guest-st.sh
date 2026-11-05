export WINEPREFIX=/etc/skel/.wine
su -s /bin/bash wexsetup -c "export WINEPREFIX=/etc/skel/.wine; wineserver -k"; sleep 3
pkill -u wexsetup; sleep 1
su -s /bin/bash wexsetup -c 'export DISPLAY=:0 WINEPREFIX=/etc/skel/.wine WINEDLLOVERRIDES=winemenubuilder.exe=d WINEDEBUG=-all; cd /var/lib/wexsetup; nohup /usr/local/bin/wine explorer /desktop=Setup,1280x800 setup.exe -newsetup > /tmp/run.log 2>&1 &'
sleep 25
WS=$(pgrep -x wineserver); SU=$(pgrep -x setup.exe | head -1); EX=$(pgrep -x explorer.exe | head -1)
echo ws=$WS setup=$SU explorer=$EX
nohup strace -f -tt -o /tmp/st-ws.txt -p $WS > /dev/null 2>&1 &
nohup strace -f -tt -o /tmp/st-su.txt -p $SU > /dev/null 2>&1 &
nohup strace -f -tt -o /tmp/st-ex.txt -p $EX > /dev/null 2>&1 &
sleep 2; echo attached
