#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Root's step of a logon for domain users (docs/DOMAIN_LOGON.md, WO 3), run by pam_exec from an account line that
# the session package adds once to /etc/pam.d/lightdm and /etc/pam.d/lightdm-autologin:
#   account requisite pam_exec.so quiet /usr/local/sbin/wex-domain-admit-pam.sh
# A local account (a plain name) passes: it is sync's business. A domain account ("DOMAIN\user", Winbind with
# winbind use default domain = no) is admitted by wex-accounts admit, which takes the user only from PAM_USER and
# PAM_TYPE, gives the account its record of the joined domain and records its domain groups, or fails. The logon
# fails closed: without the system tier's wex-accounts there is no identity for a domain account, so it is refused.
# Fixed paths: pam_exec passes the whole PAM environment, which can carry a user's pam_env settings, so nothing here
# is taken from it but PAM_USER and PAM_TYPE, and the tool runs with only those two.
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
WEX_ACCOUNTS=/usr/local/libexec/wineexplorer/wex-accounts

case "${PAM_USER:-}" in
    "") exit 1 ;;
    *\\*) ;;
    *) exit 0 ;;
esac
if [ ! -x "$WEX_ACCOUNTS" ]; then
    logger -t wex-domain-admit "domain logon refused: wex-accounts is not installed (system tier)" 2>/dev/null || true
    exit 1
fi
env -i PAM_USER="$PAM_USER" PAM_TYPE="${PAM_TYPE:-}" "$WEX_ACCOUNTS" admit || exit $?
# The user's domain policy: a refresh in the background (it applies to the user under their domain SID, from the
# record just made; the logon neither waits for it nor fails with it).
systemctl start --no-block wex-gpupdate.service >/dev/null 2>&1 || true
exit 0
