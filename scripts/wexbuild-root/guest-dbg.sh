systemctl list-units --no-legend | grep -i -E "wex-.*setup" | head -3
systemctl stop wex-gui-setup.service 2>/dev/null; su -s /bin/bash wexsetup -c 'wineserver -k' 2>/dev/null; sleep 2
rm -f /tmp/dbg.log
su -s /bin/bash wexsetup -c 'export DISPLAY=:0 WINEPREFIX=/etc/skel/.wine WINEDLLOVERRIDES=winemenubuilder.exe=d WINEDEBUG=-all WINE_PE_CACHE=0; cd /var/lib/wexsetup 2>/dev/null; nohup /usr/local/bin/wine explorer /desktop=Setup,1280x800 setup.exe -newsetup > /tmp/dbg.log 2>&1 &'
sleep 1; echo started
