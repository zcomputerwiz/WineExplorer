#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: launch the Store app (lab copy) again in the prefix store-run left behind: the classes of the Wine modules named in
# REGISTER (a DLL with DllRegisterServer) and, with WINEBOOT_UPDATE=1, the whole prefix is updated are registered again first. Output in store-run/launch2.log and summary2.
# usage: store-launch.sh [WINEDEBUG]
root=/home/debian/uwp-u1-20261111
build=$root/build/candidates/u11-2/build
c=$root/claude
run=$root/build/candidates/u11-2/store-run
cd "$run"
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver" WINEDEBUG=-all DISPLAY=${DISPLAY:-:99}
"$build/server/wineserver" -k 2>/dev/null; sleep 1
if [ -n "$WINEBOOT_UPDATE" ]; then "$build/wine" wineboot -u > reg2.log 2>&1; fi
for m in $REGISTER; do "$build/wine" regsvr32 /s $m >> reg2.log 2>&1; done
"$build/server/wineserver" -w
family=$(tr -d '\r' < install.log | sed -n 's/^Application: \(.*\)!App$/\1/p')
[ -n "$HELLO" ] && export WINEAPPSERVICE_HELLO=1
[ -n "$STACK" ] && export WINEAPPSERVICE_STACK=$STACK
export WINEDEBUG="${1:-err+all,fixme-all}" XAMLSHIM_TRACE=1 XAMLSHIM_CALLS=1
( sleep 20; xwd -root -silent -display "$DISPLAY" | convert xwd:- store2-20s.png; sleep 25; xwd -root -silent -display "$DISPLAY" | convert xwd:- store2-45s.png ) &
timeout --kill-after=10s ${LAUNCH_SECONDS:-60}s "$build/wine" $c/applaunch.exe "$family!${APPID:-App}" > launch2.log 2>&1
echo "launcher exit $?" > summary2
sleep 5
echo "--- err lines" >> summary2
grep -aE 'err:|Unhandled|exception|unimplemented|fail|fixme' launch2.log | sed 's/^[0-9a-f]*://' | sort | uniq -c | sort -rn | head -50 >> summary2
"$build/server/wineserver" -k
echo done >> summary2
