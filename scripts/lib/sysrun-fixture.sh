#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Shared with test_sysrun_server.sh. Caller supplies SRC BUILD ROOT M GRP and DISPLAY.

reg_append()   # file section line...
{
    local file="$1" section="$2"; shift 2
    { printf '\n[%s] 1700000000\n#time=1d9a0000000000\n' "$section"; printf '%s\n' "$@"; } >> "$file"
}

wex_fixture_machine()
{
    "$@" env WEX_MACHINE_GROUP="$GRP" WEX_MACHINE_ROOT="$M" WEX_SKEL_PFX="$ROOT/skel/.wine" WEX_BACKUP_DIR="$ROOT/backup" \
        WEX_WINE_BIN="$BUILD/wine" WEX_WINESERVER_BIN="$BUILD/server/wineserver" WEX_NTDLL="$BUILD/dlls/ntdll/ntdll.so" \
        WEX_SYSRUN_ENABLE=1 WINEDEBUG=-all \
        bash "$SRC/scripts/setup-machine-root.sh" > "$ROOT/setup.log" 2>&1
}

add_service()   # name account report-name start-type [registry-escaped ImagePath]
{
    local image=${5:-"$SVCEXE svc $1 Z:\\\\tmp\\\\wexsvc\\\\$3.txt Z:\\\\var\\\\lib\\\\wexsys-c2\\\\machine"}
    reg_append "$M/system.reg" 'System\\ControlSet001\\Services\\'"$1" '"ErrorControl"=dword:00000001' \
        '"ImagePath"="'"$image"'"' '"ObjectName"="'"$2"'"' '"Start"=dword:0000000'"$4" '"Type"=dword:00000010'
}
