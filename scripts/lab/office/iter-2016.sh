#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# One round of the Office 2016 setup in the prefix NAME (guest side): stop what runs there, start
# setup detached, wait until it ends (at most WAIT seconds), then print the licensing calls and
# what setup logged about them.
#   iter-2016.sh NAME DISPLAY [WAIT]      WINEDEBUG as for run.sh
set -uo pipefail
name=$1 disp=$2 wait=${3:-1500}
root=$HOME/office
cd "$root"
WINEPREFIX=$root/pfx-$name wineserver -k 2>/dev/null
sleep 2
export WINEDEBUG=${WINEDEBUG:-err+all,fixme-all,+slc,fixme+slc,+pidgenx}
setsid nohup bash lab/run.sh "$name" "$disp" 14400 media/2016/setup.exe /config Z:/home/wineuser/office/lab/config-2016.xml \
    > "logs/$name-setup.out" 2>&1 < /dev/null &
for ((i = 0; i < wait; i += 10)); do
    sleep 10
    grep -q '^exit:' "logs/$name-setup.out" && break
done
cat "logs/$name-setup.out"
log=$(sed -n 's/^log: //p' "logs/$name-setup.out")
echo "--- licensing calls (last 60)"
grep -a ':slc:\|unimplemented\|caused an exception' "$log" | cut -c1-250 | tail -60
echo "--- setup log"
setup_log=$(ls -t "pfx-$name"/drive_c/users/*/AppData/Local/Temp/Office2016Setup*.txt | head -1)
tr -d '\r' < "$setup_log" | grep -a 'CAInstallLicenses:\|Error:\|Catalyst execution finished\|Return value 3' |
    grep -av 'Exception caught during license clean-up' | tail -25 | cut -c1-300
tr -d '\r' < "$setup_log" | grep -ac 'Successfully installed package'
