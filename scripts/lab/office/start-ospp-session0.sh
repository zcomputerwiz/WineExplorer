#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Put the copy made by grab-ospp.sh into the machine root and start Office's licensing service in
# session 0 (a machine set up with WEX_SESSION0=1), where services get service-account tokens.
# Run as root.  Log: /tmp/ospp-session0.log
#   start-ospp-session0.sh COPY_DIR        WINEDEBUG selects the channels of the start command
set -uo pipefail
copy=$1
machine=/var/lib/wineexplorer/machine
dir="$machine/drive_c/Program Files/Common Files/Microsoft Shared/OfficeSoftwareProtectionPlatform"
as_session0() {
    runuser -u wexsys -- env -i PATH=/usr/local/bin:/usr/bin:/bin HOME=$machine WINEPREFIX=$machine \
        WINEDEBUG="${debug:--all}" "WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d" "$@"
}
mkdir -p "$dir"
cp "$copy"/files/* "$dir/"
chgrp -R wexsys "$dir"
chmod -R g+rX,o+rX "$dir"
install -m 0644 "$copy/ospp.reg" "$copy/service.reg" /tmp/
as_session0 wine reg import 'Z:\tmp\ospp.reg' /reg:64
as_session0 wine reg import 'Z:\tmp\service.reg'
debug=${WINEDEBUG:-err+all,fixme-all,+service} as_session0 timeout 120 wine net start osppsvc > /tmp/ospp-session0.log 2>&1
echo "net start exit: $?"
sleep 5
as_session0 wine sc query osppsvc 2>/dev/null | tr -d '\r' | grep -a 'STATE\|EXIT'
tail -5 /tmp/ospp-session0.log | cut -c1-200
journalctl -u wex-session0-services.service -u wex-session0.service --since '-3 min' --no-pager 2>/dev/null | tail -8 | cut -c1-200
