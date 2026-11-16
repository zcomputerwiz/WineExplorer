#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: a quick Calculator run on a copy of the base prefix (calc-prep.sh): the XamlShim files in claude/quick.tar
# replace the base's (and its classes are registered again when classes.txt changed), the Calculator is launched as
# from the Start menu (no arguments), and the run ends once the shim log has been quiet for QUIET seconds (at least MIN,
# at most MAX). Wine modules come from the build tree as they are. New classes of a changed Wine module are registered
# with REGCLASSES="wintypes.dll=Windows.Foundation.Diagnostics.LoggingFields,... other.dll=..." (WinRT) or
# REGISTER="module.dll ..." (regsvr32); wineboot -u hangs on a copied prefix, so it is not used.
# usage: calc-quick.sh [WINEDEBUG]   env: QUIET=15 MIN=15 MAX=120 REGCLASSES= REGISTER=
root=/home/debian/uwp-u1-20261111
build=$root/build/candidates/u11-2/build
c=$root/claude
base=$root/build/candidates/u11-2/quick-base
run=$root/build/candidates/u11-2/quick-run
QUIET=${QUIET:-15} MIN=${MIN:-15} MAX=${MAX:-120}
[ -f "$base/info" ] || { echo "no base prefix: run calc-prep.sh"; exit 1; }
. "$base/info"
t0=$(date +%s)
rm -rf "$run"; mkdir -p "$run"; cd "$run"
cp -a "$base/prefix" prefix
t1=$(date +%s)
export WINEPREFIX="$run/prefix" WINESERVER="$build/server/wineserver"
xaml="$WINEPREFIX/drive_c/Program Files/WineExplorer/Xaml/x64"
if [ -f "$c/quick.tar" ]; then
  tar -xf "$c/quick.tar" -C "$xaml"
  echo "swapped: $(tar -tf "$c/quick.tar" | wc -l) files"
  if ! cmp -s "$xaml/classes.txt" "$base/classes.txt"; then
    python3 "$c/quick-reg.py" "$c/fw/wex-components.py" "$xaml/classes.txt" "$WINEPREFIX" "$build/wine"
  fi
fi
for dll in $REGISTER; do
  WINEDEBUG=-all timeout 60 "$build/wine" regsvr32 /s "$dll" > /dev/null 2>&1; echo "regsvr32 $dll: $?"
done
[ -n "$REGISTER" ] && "$build/server/wineserver" -w
# new WinRT classes of a changed Wine module (registered by wineboot from the module's resource on a fresh prefix)
[ -n "$REGCLASSES" ] && python3 "$c/quick-reg.py" --winrt "$WINEPREFIX" "$build/wine" $REGCLASSES
t2=$(date +%s)
export WINEDEBUG="${1:-err+all,fixme+all}" XAMLSHIM_TRACE=1 XAMLSHIM_CALLS=1
"$build/wine" "$c/applaunch.exe" "$family!App" > launch.log 2>&1 &
launcher=$!
last=-1; still=0; elapsed=0; shimlog=
while [ $elapsed -lt $MAX ]; do
  sleep 2; elapsed=$(( $(date +%s) - t2 ))
  kill -0 $launcher 2>/dev/null || { echo "the application exited"; break; }
  [ -n "$shimlog" ] || shimlog=$(find "$WINEPREFIX/drive_c/users" -name xamlshim.txt 2>/dev/null | head -1)
  # quiet counts only from OnLaunched on (Uno's start writes nothing for a while); before it, MAX applies
  [ -n "$shimlog" ] && grep -q "OnLaunched" "$shimlog" || continue
  size=$(stat -c %s "$shimlog")
  if [ "$size" = "$last" ]; then still=$(( still + 2 )); else still=0; last=$size; fi
  [ $elapsed -ge $MIN ] && [ $still -ge $QUIET ] && { echo "quiet for $still s"; break; }
done
[ -n "$DISPLAY" ] && xwd -root -silent -display "$DISPLAY" | convert xwd:- quick.png 2>/dev/null
"$build/server/wineserver" -k
t3=$(date +%s)
[ -n "$shimlog" ] && tr -d '\r' < "$shimlog" > shim.txt
echo "timing: copy $((t1 - t0)) s, swap $((t2 - t1)) s, run $((t3 - t2)) s, total $((t3 - t0)) s"
echo "--- shim"
grep -h "probe \|OnLaunched\|OnNavigatedTo\|event handler returned\|not implemented:\|exception in\|DataContext =\|LoadComponent.*exception" shim.txt 2>/dev/null | sort | uniq -c | sort -rn | cut -c1-220 | head -30
echo "--- wine"
grep -a "err:\|stub!\|semi-stub" launch.log | sed 's/^[0-9a-f]*://; s/0x[0-9a-f]*\|[0-9A-F]\{16\}/X/g' | sort | uniq -c | sort -rn | cut -c1-200 | head -15
ls -la quick.png 2>/dev/null | awk '{print "screenshot " $5 " bytes"}'
echo quick-done
