#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# PAM check for Safe Mode (docs/SAFE_MODE.md), run by pam_exec from two lines the session package adds once:
#   /etc/pam.d/lightdm            wex-safemode-pam-check.sh            Safe Mode with Command Prompt: administrators
#                                                                      (root, group sudo) only
#   /etc/pam.d/lightdm-autologin  wex-safemode-pam-check.sh autologin  no automatic logon in any Safe Mode; LightDM
#                                                                      then shows the logon screen
# In a normal start both succeed. The mode comes from wex-bootmode, so nothing is edited per boot.
bootmode=${WEX_BOOTMODE_HELPER:-/usr/local/bin/wex-bootmode}
mode=$("$bootmode" 2>/dev/null) || mode=normal

case "${1:-logon}" in
    autologin)
        case "$mode" in
            safe|safenet|safecmd)
                logger -t wex-safemode "automatic logon skipped: Safe Mode ($mode)" 2>/dev/null || true
                exit 1
                ;;
        esac
        exit 0
        ;;
esac

[ "$mode" = safecmd ] || exit 0

user=${PAM_USER:-}
[ -n "$user" ] || exit 1
[ "$user" = root ] && exit 0
if id -nG "$user" 2>/dev/null | tr ' ' '\n' | grep -qx sudo; then
    exit 0
fi
# pam_exec passes this to the logon screen with its "stdout" option
echo "Only administrators can log on in Safe Mode with Command Prompt."
logger -t wex-safemode "Safe Mode with Command Prompt: logon refused for '$user', not an administrator" 2>/dev/null || true
exit 1
