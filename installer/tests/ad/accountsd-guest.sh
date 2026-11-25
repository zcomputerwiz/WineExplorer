#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Runs INSIDE an installed session 0 machine as root (sent there by installer/tests/ad/machine.sh with
# WEX_GUEST=accountsd-guest.sh). The accounts helper on a real system (docs/LOCAL_USERS_DESIGN.md, "Tests of the helper and
# its link"): the installed units, socket, account and rule; the MainPID of session 0's unit is the real wineserver; polkit
# grants the helper's account and nobody else's (session 0's own account is refused); a process that is not session 0's
# wineserver is dropped at the socket. One line per check, "ok NAME" or "not ok NAME: detail", then AD-GUEST-DONE; the host
# script decides. No domain is needed.
#
#   accountsd-guest.sh [INSTALLED_USER]
set -uo pipefail

S0_ACCOUNT=wexsys
ok() { echo "ok $1"; }
notok() { echo "not ok $1: $(printf '%s' "$2" | tr '\n' ' ' | cut -c1-240)"; }
check() { local name=$1; shift; local out; if out=$("$@" 2>&1); then ok "$name"; else notok "$name" "$out"; fi; }
finish() { userdel -r zzaccd 2>/dev/null; echo "AD-GUEST-DONE"; exit 0; }
want() { [ "$1" = "$2" ] || { echo "got [$1], want [$2]"; return 1; }; }

# 1. what the installer put there
check units_installed test -f /lib/systemd/system/wex-accountsd.socket -a -f /lib/systemd/system/wex-accountsd.service -a -f /lib/systemd/system/wex-accounts-sync.service
check helper_binary_installed bash -c '[ "$(stat -c "%U:%G:%a" /usr/local/libexec/wineexplorer/wex-accountsd)" = root:root:755 ]'
check helper_has_no_lab_options bash -c '! grep -q -e "--mainpid" -e "WEX_ACCOUNTSD_TEST" -e "--any-dumpable" /usr/local/libexec/wineexplorer/wex-accountsd'
check socket_enabled_and_listening bash -c 'systemctl is-enabled wex-accountsd.socket >/dev/null && systemctl is-active wex-accountsd.socket >/dev/null'
check socket_file_root_0660_session0_group bash -c '[ "$(stat -c "%U:%G:%a" /run/wineexplorer/accounts.sock)" = root:wexsession0:660 ]'
check account_has_no_other_group bash -c '[ "$(id -Gn wexaccounts)" = wexaccounts ] && ! id -nG wexaccounts | grep -qw wexsys'
check socket_group_has_only_session0 bash -c '[ "$(getent group wexsession0 | cut -d: -f4)" = '"$S0_ACCOUNT"' ]'
check accountsd_conf bash -c 'grep -qx "session0_user='"$S0_ACCOUNT"'" /etc/wineexplorer/accountsd.conf && grep -qx "session0_unit=wex-session0.service" /etc/wineexplorer/accountsd.conf && grep -qx "sync_unit=wex-accounts-sync.service" /etc/wineexplorer/accountsd.conf && [ "$(stat -c %a /etc/wineexplorer/accountsd.conf)" = 644 ]'
check polkit_rule_installed test -f /etc/polkit-1/rules.d/49-wexaccounts.rules

# 2. the identity of session 0's server: the unit's MainPID is the real wineserver (not a wrapper)
pid=$(systemctl show -p MainPID --value wex-session0.service)
if [ -n "$pid" ] && [ "$pid" != 0 ]; then
    exe=$(readlink "/proc/$pid/exe" 2>&1)
    want_exe=$(readlink -f /usr/local/bin/wineserver)
    if [ "$exe" = "$want_exe" ] && file -L "$want_exe" | grep -q ELF; then ok mainpid_is_the_real_wineserver
    else notok mainpid_is_the_real_wineserver "MainPID $pid runs [$exe], /usr/local/bin/wineserver is [$want_exe] $(file -L "$want_exe" | cut -c1-80)"; fi
else notok mainpid_is_the_real_wineserver "wex-session0.service has no MainPID"; fi

# 3. polkit: the helper's account may administer users and start the sync unit, nothing else; session 0's own account may not
CREATE=(busctl --system --timeout=20 call org.freedesktop.Accounts /org/freedesktop/Accounts org.freedesktop.Accounts CreateUser ssi zzaccd "Accountsd Test" 0)
out=$(runuser -u "$S0_ACCOUNT" -- "${CREATE[@]}" 2>&1); rc=$?
if [ $rc != 0 ] && ! id zzaccd >/dev/null 2>&1; then ok polkit_refuses_session0_account_creating_a_user
else notok polkit_refuses_session0_account_creating_a_user "rc $rc: $out"; userdel -r zzaccd 2>/dev/null; fi
out=$(runuser -u wexaccounts -- "${CREATE[@]}" 2>&1); rc=$?
if [ $rc = 0 ] && id zzaccd >/dev/null 2>&1; then ok polkit_allows_the_helper_account_creating_a_user
else notok polkit_allows_the_helper_account_creating_a_user "rc $rc: $out"; fi
uid=$(id -u zzaccd 2>/dev/null)
if [ -n "$uid" ]; then
    out=$(runuser -u wexaccounts -- busctl --system --timeout=20 call org.freedesktop.Accounts /org/freedesktop/Accounts org.freedesktop.Accounts DeleteUser xb "$uid" 0 2>&1); rc=$?
    if [ $rc = 0 ] && ! id zzaccd >/dev/null 2>&1; then ok polkit_allows_the_helper_account_deleting_a_user
    else notok polkit_allows_the_helper_account_deleting_a_user "rc $rc: $out"; fi
fi
START=(busctl --system --timeout=20 call org.freedesktop.systemd1 /org/freedesktop/systemd1 org.freedesktop.systemd1.Manager StartUnit ss)
out=$(runuser -u wexaccounts -- "${START[@]}" wex-accounts-sync.service replace 2>&1); rc=$?
if [ $rc = 0 ]; then ok polkit_allows_starting_the_sync_unit; else notok polkit_allows_starting_the_sync_unit "rc $rc: $out"; fi
out=$(runuser -u wexaccounts -- "${START[@]}" systemd-tmpfiles-clean.service replace 2>&1); rc=$?
if [ $rc != 0 ]; then ok polkit_refuses_starting_any_other_unit; else notok polkit_refuses_starting_any_other_unit "started: $out"; fi
out=$(runuser -u "$S0_ACCOUNT" -- "${START[@]}" wex-accounts-sync.service replace 2>&1); rc=$?
if [ $rc != 0 ]; then ok polkit_refuses_session0_account_starting_the_sync_unit; else notok polkit_refuses_session0_account_starting_the_sync_unit "started: $out"; fi
out=$(runuser -u wexaccounts -- busctl --system --timeout=20 call org.freedesktop.Accounts /org/freedesktop/Accounts org.freedesktop.Accounts DeleteUser xb 0 0 2>&1); rc=$?
# the grant covers the action, not root: the helper itself refuses uid 0 (its own check); AccountsService refuses it too
if [ $rc != 0 ]; then ok accountsservice_refuses_deleting_root; else notok accountsservice_refuses_deleting_root "answered: $out"; fi

# 4. a process that is not session 0's wineserver is dropped at the socket (root can connect: the file mode is not the authority)
res=$(python3 - <<'PYEND'
import socket
s = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
s.settimeout(10)
try:
    s.connect('/run/wineexplorer/accounts.sock')
    import struct
    s.send(struct.pack('<IHHIIIII', 0x41584557, 1, 1, 1, 0, 2, 0, 0))
    r = s.recv(1000)
    print('answered' if r else 'dropped')
except Exception as e:
    print('dropped' if 'reset' in str(e).lower() or 'broken' in str(e).lower() else 'error ' + str(e))
PYEND
)
if [ "$res" = dropped ]; then ok a_process_that_is_not_the_server_is_dropped; else notok a_process_that_is_not_the_server_is_dropped "$res"; fi
journalctl -u wex-accountsd.service --no-pager -n 20 2>/dev/null | grep -q "peer refused" && ok the_refusal_is_in_the_journal || notok the_refusal_is_in_the_journal "no 'peer refused' line"

finish
