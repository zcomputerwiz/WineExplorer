#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Host-side helper (Git Bash): drive the owned U1 guest through its controller (owned-lab.py).
#   lab.sh ssh 'command'            run a command in the guest (55 s limit)
#   lab.sh push FILE [GUEST_NAME]   copy FILE (a file name inside $LABPAD) into the guest workspace's claude/ directory
#   lab.sh wait 'command' [SECONDS] run a command in the guest and wait for it (default 600 s): one ssh with the controller's key and
#                                   host key, without the controller's 55 s limit
# $LABPAD: a directory on the host in Git Bash form (default /tmp/wex-lab-pad) that `push` reads from; it must be below a Windows drive.
set -euo pipefail
export MSYS_NO_PATHCONV=1
here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
ctl_win=$(cygpath -m "$here/owned-lab.py")
ctl=$(wsl.exe wslpath -a "$ctl_win" | tr -d '\r')
pad_win="${LABPAD:-$(cygpath -u "$TEMP")/wex-lab-pad}"
mkdir -p "$pad_win"
pad_wsl=$(wsl.exe wslpath -a "$(cygpath -m "$pad_win")" | tr -d '\r')
ws=/home/debian/uwp-u1-20261111
lab() { wsl.exe --distribution Debian --user root --exec python3 "$ctl" "$@"; }
case "$1" in
ssh) lab ssh "$2" ;;
wait)
    wsl.exe --distribution Debian --user root --exec timeout "${3:-600}" ssh -i /var/tmp/wex-g4-lab-credentials/vm_key -p 2237 \
        -o BatchMode=yes -o StrictHostKeyChecking=yes -o UserKnownHostsFile=/var/tmp/wex-uwp-u1-20261111/known_hosts \
        -o ConnectTimeout=5 -o ServerAliveInterval=15 debian@127.0.0.1 "$2"
    ;;
push) lab push "$pad_wsl/$2" "$ws/claude/${3:-$2}" ;;
*) echo "usage: lab.sh ssh|wait|push ..." >&2; exit 2 ;;
esac
