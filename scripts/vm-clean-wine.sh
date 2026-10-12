#!/bin/bash
# Run ON the VM: stop every Wine process of this user, including orphans left behind by "pkill wineserver"
# (they keep stale server state and make new Wine clients hang), then optionally restart the session.
#   vm-clean-wine.sh            clean only
#   vm-clean-wine.sh restart    clean and restart the display manager (autologin starts the WineExplorer session)
wineserver -k 2>/dev/null
sleep 1
pkill -9 -f 'windows.system32' 2>/dev/null
pkill -9 -x wineserver 2>/dev/null
pkill -9 -x wineserver64 2>/dev/null
sleep 1
[ "$1" = restart ] && sudo systemctl restart display-manager
exit 0
