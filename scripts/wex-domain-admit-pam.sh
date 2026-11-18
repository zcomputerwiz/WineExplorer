#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Root's step of a logon (docs/DOMAIN_LOGON.md, WO 3; docs/GROUP_POLICY.md, "Policy before the desktop"), run by
# pam_exec from an account line that the session package adds once to /etc/pam.d/lightdm and
# /etc/pam.d/lightdm-autologin:
#   account requisite pam_exec.so quiet /usr/local/sbin/wex-domain-admit-pam.sh
# 1. A domain account ("DOMAIN\user", Winbind with winbind use default domain = no) is admitted by wex-accounts admit,
#    which takes the user only from PAM_USER and PAM_TYPE, gives the account its record of the joined domain and
#    records its domain groups, or fails. The logon fails closed: without the system tier's wex-accounts there is no
#    identity for a domain account, so it is refused. A local account (a plain name) is sync's business and passes.
# 2. Then, for every account, the user's policy on the user's first logon here: wex-gpupdate.sh --logon applies it
#    before the session starts, as Windows does before the desktop on a first sign-in (it ends at once when there is no
#    policy for the user, or at a later logon, which the session refreshes after the shell is up). The wait is
#    bounded (60 seconds) and never fails the logon: a policy that is not applied in
#    time arrives with the session's own refresh. It runs before the session exists, so it does not compete with the
#    session's registration with session 0 (B26), which is why the refresh of an existing session waits for the shell.
# Fixed paths: pam_exec passes the whole PAM environment, which can carry a user's pam_env settings, so nothing here
# is taken from it but PAM_USER and PAM_TYPE, and the tools run with only those two.
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH
WEX_ACCOUNTS=/usr/local/libexec/wineexplorer/wex-accounts
WEX_GPUPDATE=/usr/local/bin/wex-gpupdate.sh
LOGON_POLICY_WAIT=60   # seconds; fixed here, not taken from the PAM environment

case "${PAM_USER:-}" in
    "") exit 1 ;;
    *\\*) DOMAIN_ACCOUNT=1 ;;
    *) DOMAIN_ACCOUNT=0 ;;
esac
if [ "$DOMAIN_ACCOUNT" = 1 ]; then
    if [ ! -x "$WEX_ACCOUNTS" ]; then
        logger -t wex-domain-admit "domain logon refused: wex-accounts is not installed (system tier)" 2>/dev/null || true
        exit 1
    fi
    env -i PAM_USER="$PAM_USER" PAM_TYPE="${PAM_TYPE:-}" "$WEX_ACCOUNTS" admit || exit $?
    # The logon screen lists the users AccountsService has cached, and it caches a user who logged on with a local
    # passwd entry only: a domain user (Winbind) is cached on request, which is what CacheUser is for. The tile then
    # shows after the first logon. A failure does not fail the logon; a slow AccountsService holds the account phase
    # for 3 seconds at most. Like wex-accounts above, busctl gets nothing of the PAM environment but the fixed PATH
    # (it honours DBUS_SYSTEM_BUS_ADDRESS, LD_* and more).
    env -i PATH="$PATH" busctl --system --timeout=3 call org.freedesktop.Accounts /org/freedesktop/Accounts org.freedesktop.Accounts CacheUser s "$PAM_USER" >/dev/null 2>&1 || true
fi
# the user's policy before the session, bounded; whatever happens, the logon goes on
if [ -x "$WEX_GPUPDATE" ]; then
    started=$(date +%s)
    # (the output goes to a file, not a pipe: a child of the run that outlives the timeout would hold a pipe open and
    # make this step wait for it, past the bound)
    step_log=$(mktemp 2>/dev/null) || step_log=/dev/null
    timeout -k 5 "$LOGON_POLICY_WAIT" env -i PATH="$PATH" "$WEX_GPUPDATE" --logon "$PAM_USER" >"$step_log" 2>&1 </dev/null
    step_rc=$?
    step_out=$(head -c 4000 "$step_log" 2>/dev/null)
    [ "$step_log" = /dev/null ] || rm -f "$step_log"
    # what happened and how long it took, for the journal (the lab reads it): the run's own words tell "nothing to apply"
    case $step_rc in
        0) case $step_out in
               *"no policy to apply"*|*"not a first logon"*|*"no active record"*|*"not a user name"*) outcome="nothing to apply" ;;
               *) outcome=applied ;;
           esac ;;
        124|137) outcome="timed out" ;;
        *) outcome="failed (status $step_rc)" ;;
    esac
    logger -t wex-domain-admit -- "policy step for $PAM_USER: $outcome in $(( $(date +%s) - started )) s" 2>/dev/null || true
fi
exit 0
