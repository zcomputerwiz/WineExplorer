#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# lab: copy a file from the owned guest workspace (claude/) to the scratchpad, like owned-lab.py push in reverse
# usage (in WSL, as root): pull.sh GUEST_NAME
set -euo pipefail
name=$1
case "$name" in */*|..*) echo "plain file names only"; exit 1;; esac
pad=/mnt/c/Users/Owner/AppData/Local/Temp/claude/f10dd599a76cdd8a/cc6dd05f-7e73-49f2-be6b-aa4119777252/scratchpad
scp -i /var/tmp/wex-g4-lab-credentials/vm_key -P 2237 -o BatchMode=yes -o StrictHostKeyChecking=yes \
    -o UserKnownHostsFile=/var/tmp/wex-uwp-u1-20261111/known_hosts -o ConnectTimeout=5 \
    "debian@127.0.0.1:/home/debian/uwp-u1-20261111/claude/$name" "$pad/$name"
ls -la "$pad/$name"
