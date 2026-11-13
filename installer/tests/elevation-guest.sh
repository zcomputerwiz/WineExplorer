#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Runs INSIDE an installed machine with enforced elevation, as root (put there by installer/tests/elevation.sh).
#
#   elevation-guest.sh before [USER]     the checks before any elevation: "ok NAME" / "not ok NAME: detail" lines
#   elevation-guest.sh ask NAME [USER]   the installed administrator asks for one elevation (ShellExecute "runas"
#                                        of cmd.exe that writes the value NAME below HKLM\Software\WexElevTest);
#                                        returns when the request has ended; the probe's lines go to
#                                        /root/el-NAME.log
#   elevation-guest.sh value NAME [USER] prints the value NAME, or nothing
#
# /opt/elprobe.exe is scripts/wexelevate_probe.c, built and copied in by the host script.
set -uo pipefail

MODE=${1:?mode}
ok() { echo "ok $1"; }
notok() { echo "not ok $1: $(printf '%s' "$2" | tr '\n' ' ' | cut -c1-240)"; }
wine_cmd() { su - "$1" -c "cd /tmp; DISPLAY=:0 WINEDEBUG=-all timeout 150 $2" 2>&1; }
KEY='HKLM\Software\WexElevTest'

case "$MODE" in
before)
    U=${2:-wineuser}
    helper=$(wine_cmd "$U" 'wine reg query "HKLM\Software\Wine\Elevation" /v Helper' | tr -d '\r' | awk '$1 == "Helper" { print $NF }')
    if [ -n "$helper" ]; then ok enforcement_configured; else notok enforcement_configured "no Helper value"; fi
    if id -nG "$U" | tr ' ' '\n' | grep -qx sudo; then ok user_is_administrator; else notok user_is_administrator "$(id -nG "$U")"; fi
    if wine_cmd "$U" "wine reg add \"$KEY\" /v limited /d 1 /f" >/dev/null &&
       [ -n "$(wine_cmd "$U" "wine reg query \"$KEY\" /v limited" | grep -w limited)" ]; then
        notok limited_write_refused "the administrator's own (limited) program wrote HKLM"
    else ok limited_write_refused; fi
    n=$(pgrep -u wexsys -f 'wineserver --session0' 2>/dev/null | wc -l)
    if [ "$n" -eq 1 ]; then ok session0_runs; else notok session0_runs "wineserver --session0 processes: $n"; fi
    ;;
ask)
    NAME=${2:?name}; U=${3:-wineuser}
    su - "$U" -c "cd /tmp; DISPLAY=:0 WINEDEBUG=-all timeout 150 wine /opt/elprobe.exe shexec runas 'C:\\windows\\system32\\cmd.exe' 0 'Z:\\tmp\\el-$NAME.out' - 'raw:/c reg add $KEY /v $NAME /d 1 /f'" > "/root/el-$NAME.log" 2>&1
    echo "probe rc=$?" >> "/root/el-$NAME.log"
    ;;
value)
    NAME=${2:?name}; U=${3:-wineuser}
    wine_cmd "$U" "wine reg query \"$KEY\" /v $NAME" | tr -d '\r' | awk -v n="$NAME" '$1 == n { print $NF }'
    ;;
esac
