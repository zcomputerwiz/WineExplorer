#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Run one Windows program as the standard test user USER (created from /etc/skel on first use, its
# prefix personalized) on the lab display of run-session0.sh.  Run as root.
#   run-user.sh USER LABEL DISPLAY_NUM TIMEOUT_S EXE [ARGS...]
set -uo pipefail
user=$1 label=$2 disp=$3 limit=$4
shift 4
here=$(dirname "$(readlink -f "$0")")
out=/var/tmp/office-s0
mkdir -p "$out"
chmod 1777 "$out"
if ! getent passwd "$user" > /dev/null; then
    useradd -m -s /bin/bash "$user"
    # the repository copy next to the lab scripts, or the guest's source tree
    for helper in "$here/../../personalize-prefix.sh" /home/wineuser/src/scripts/personalize-prefix.sh; do
        [ -f "$helper" ] && { bash "$helper" "/home/$user/.wine" "$user"; break; }
    done
    chown -R "$user:" "/home/$user/.wine"
fi
if ! [ -e "/tmp/.X11-unix/X$disp" ]; then
    setsid nohup Xvfb ":$disp" -ac -screen 0 1280x1024x24 -nolisten tcp > "$out/xvfb-$disp.log" 2>&1 < /dev/null &
    sleep 2
fi
log=$out/$label-$(date +%Y%m%d-%H%M%S).log
echo "log: $log"
runuser -u "$user" -- env -i PATH=/usr/local/bin:/usr/bin:/bin HOME="/home/$user" USER="$user" DISPLAY=":$disp" \
    WINEDEBUG="${WINEDEBUG:-err+all,fixme-all}" "WINEDLLOVERRIDES=mscoree,mshtml=;winemenubuilder.exe=d" \
    timeout "$limit" wine "$@" > "$log" 2>&1
status=$?
chmod a+r "$log"
echo "exit: $status" | tee -a "$log"
exit $status
