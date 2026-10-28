#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# PAM authorization check for WineExplorer Safe Mode.
# Strictly limits logon to administrative users (group sudo) when Safe Mode is active.

# Exit immediately with success if Safe Mode is not active
if [ -z "$WEXMODE_FORCE_SAFECMD" ] && [ ! -f /run/wex-safemode/active ] && ! grep -qw "wexmode=safecmd" /proc/cmdline 2>/dev/null; then
    exit 0
fi

AUTH_USER="${PAM_USER:-$1}"
if [ -z "$AUTH_USER" ]; then
    exit 1
fi

# Root is always an administrator
if [ "$AUTH_USER" = "root" ]; then
    exit 0
fi

# Verify user membership in sudo group
if id -nG "$AUTH_USER" 2>/dev/null | grep -qw "sudo"; then
    exit 0
fi

# Deny logon to non-administrative users during Safe Mode recovery
logger -t wex-safemode "Safe Mode login rejected for non-sudo user '$AUTH_USER'"
exit 1
