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
check accountsd_conf bash -c 'grep -qx "session0_user='"$S0_ACCOUNT"'" /etc/wineexplorer/accountsd.conf && grep -qx "session0_unit=wex-session0.service" /etc/wineexplorer/accountsd.conf && grep -qx "sync_unit=wex-accounts-sync.service" /etc/wineexplorer/accountsd.conf && grep -qx "helper_user=wexaccounts" /etc/wineexplorer/accountsd.conf && [ "$(stat -c %a /etc/wineexplorer/accountsd.conf)" = 644 ]'
check polkit_rule_installed test -f /etc/polkit-1/rules.d/49-wexaccounts.rules

# 2. the identity of session 0's server: the unit's MainPID is the real wineserver (not a wrapper)
pid=$(systemctl show -p MainPID --value wex-session0.service)
if [ -n "$pid" ] && [ "$pid" != 0 ]; then
    exe=$(readlink "/proc/$pid/exe" 2>&1)
    want_exe=$(readlink -f /usr/local/bin/wineserver)
    magic=$(head -c4 "$want_exe" | od -An -c | tr -d ' ')   # an ELF binary, not a script that starts it
    if [ "$exe" = "$want_exe" ] && [ "$magic" = '177ELF' ]; then ok mainpid_is_the_real_wineserver
    else notok mainpid_is_the_real_wineserver "MainPID $pid runs [$exe], /usr/local/bin/wineserver is [$want_exe], first bytes [$magic]"; fi
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

# 5. blank passwords at the logon screen only (docs/BLANK_PASSWORDS.md), on the real AccountsService and PAM
RECON=/usr/local/libexec/wineexplorer/wex-accounts-blank-reconcile
SECHASH='$6$saltsalt$AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA'
check blank_units_installed test -f /lib/systemd/system/wex-accounts-blank.service -a -f /lib/systemd/system/wex-accounts-blank.path -a -x "$RECON" -a -x /usr/local/libexec/wineexplorer/wex-blank-pam.sh
check blank_path_unit_active systemctl is-active wex-accounts-blank.path
out=$(runuser -u wexaccounts -- "${START[@]}" wex-accounts-blank.service replace 2>&1); rc=$?
if [ $rc = 0 ]; then ok polkit_allows_starting_the_blank_reconcile_unit; else notok polkit_allows_starting_the_blank_reconcile_unit "rc $rc: $out"; fi
out=$(runuser -u "$S0_ACCOUNT" -- "${START[@]}" wex-accounts-blank.service replace 2>&1); rc=$?
if [ $rc != 0 ]; then ok polkit_refuses_session0_account_starting_the_blank_reconcile_unit; else notok polkit_refuses_session0_account_starting_the_blank_reconcile_unit "started: $out"; fi
# no stack takes an empty password except through the greeter's own service (the greeter's pair has nullok on purpose)
if grep -rn --exclude='*.pam-old' --exclude='*.dpkg-*' --exclude=lightdm nullok /etc/pam.d > /tmp/accd-nullok.txt 2>&1; then notok no_nullok_in_pam_outside_the_greeter "$(head -3 /tmp/accd-nullok.txt)"; else ok no_nullok_in_pam_outside_the_greeter; fi
grep -q 'success=ignore default=1.*ingroup nopasswdlogin' /etc/pam.d/lightdm && grep -q 'success=done default=ignore.*pam_unix.so nullok' /etc/pam.d/lightdm && ! grep -q 'sufficient.*ingroup nopasswdlogin' /etc/pam.d/lightdm &&
    ok greeter_stack_has_the_group_and_nullok_pair || notok greeter_stack_has_the_group_and_nullok_pair "$(grep -n 'nopasswdlogin\|nullok' /etc/pam.d/lightdm | head -4)"
ls /usr/share/pam-configs/ | grep -q distrib && notok no_second_unix_profile_in_pam_configs "$(ls /usr/share/pam-configs)" || ok no_second_unix_profile_in_pam_configs
if DEBIAN_FRONTEND=noninteractive apt-get install -y --reinstall libpam-runtime >/tmp/accd-reinstall.txt 2>&1; then
    if grep -rn --exclude='*.pam-old' --exclude='*.dpkg-*' --exclude=lightdm nullok /etc/pam.d >/dev/null 2>&1; then notok no_nullok_after_reinstalling_libpam_runtime "$(grep -rn nullok /etc/pam.d | head -3)"; else ok no_nullok_after_reinstalling_libpam_runtime; fi
else echo "note no_nullok_after_reinstalling_libpam_runtime: skipped, the reinstall failed ($(tail -1 /tmp/accd-reinstall.txt))"; fi

# the PAM answers on the real stacks, from a small ctypes program: pamauth SERVICE USER UID (the answer is on stdin); uid 0 stays root
cat > /tmp/pamauth.py <<'PYEND'
import ctypes, ctypes.util, os, sys
service, user, uid = sys.argv[1], sys.argv[2], int(sys.argv[3])
answer = sys.stdin.readline().rstrip('\n').encode()
pam = ctypes.CDLL(ctypes.util.find_library('pam') or 'libpam.so.0')
libc = ctypes.CDLL(None)
libc.calloc.restype = ctypes.c_void_p
libc.strdup.restype = ctypes.c_void_p
class Msg(ctypes.Structure): _fields_ = [('msg_style', ctypes.c_int), ('msg', ctypes.c_char_p)]
class Resp(ctypes.Structure): _fields_ = [('resp', ctypes.c_void_p), ('resp_retcode', ctypes.c_int)]
CONV = ctypes.CFUNCTYPE(ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.POINTER(Msg)), ctypes.POINTER(ctypes.POINTER(Resp)), ctypes.c_void_p)
class Conv(ctypes.Structure): _fields_ = [('conv', CONV), ('data', ctypes.c_void_p)]
def conv(n, msgs, resp, data):
    arr = libc.calloc(n, ctypes.sizeof(Resp))
    r = ctypes.cast(arr, ctypes.POINTER(Resp))
    for i in range(n):
        if msgs[i].contents.msg_style in (1, 2):
            r[i].resp = libc.strdup(answer)
    resp[0] = r
    return 0
cb = CONV(conv)
c = Conv(cb, None)
h = ctypes.c_void_p()
if uid:
    os.setgroups([]); os.setgid(uid); os.setuid(uid)
pam.pam_start.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(Conv), ctypes.POINTER(ctypes.c_void_p)]
if pam.pam_start(service.encode(), user.encode(), ctypes.byref(c), ctypes.byref(h)) != 0:
    sys.exit(2)
sys.exit(0 if pam.pam_authenticate(h, 0) == 0 else 1)
PYEND
# pamok SERVICE USER UID ANSWER: success when the stack authenticates (an empty ANSWER is an empty password)
pamok() { printf '%s\n' "$4" | python3 /tmp/pamauth.py "$1" "$2" "$3" >/dev/null 2>&1; }

shadow() { getent shadow "$1" | cut -d: -f"$2"; }
ingroup() { id -nG "$1" 2>/dev/null | tr ' ' '\n' | grep -qx nopasswdlogin; }
reconcile() { runuser -u wexaccounts -- "${START[@]}" wex-accounts-blank.service replace >/dev/null 2>&1; sleep 3; }
umode() { runuser -u wexaccounts -- busctl --system --timeout=20 call org.freedesktop.Accounts "/org/freedesktop/Accounts/User$(id -u zzaccd 2>/dev/null)" org.freedesktop.Accounts.User "$@" >/dev/null 2>&1; }

# the blank account: SetPasswordMode(NONE) empties the field; the reconcile puts the user in the group
runuser -u wexaccounts -- "${CREATE[@]}" >/dev/null 2>&1
if id zzaccd >/dev/null 2>&1; then
    umode SetPasswordMode i 2
    field=$(shadow zzaccd 2)
    if [ -z "$field" ]; then ok accountsservice_none_mode_empties_the_shadow_field; else notok accountsservice_none_mode_empties_the_shadow_field "field [$field]"; fi
    echo "note none_mode_leaves: shadow field [$(shadow zzaccd 2)] expire [$(shadow zzaccd 8)] inactive [$(shadow zzaccd 7)]"
    if ingroup zzaccd; then echo "note accountsservice_keeps_nopasswdlogin_itself: yes (the reconcile is then redundant here)"
    else echo "note accountsservice_keeps_nopasswdlogin_itself: no, as the design assumes (Debian 13: accounts-daemon runs passwd, chage and usermod only)"; fi
    reconcile
    ingroup zzaccd && ok reconcile_puts_the_blank_user_in_nopasswdlogin || notok reconcile_puts_the_blank_user_in_nopasswdlogin "groups: $(id -nG zzaccd)"
    # the real stacks, for the blank account as the account itself: refused everywhere but the greeter
    ZG=$(id -g zzaccd); ZU=$(id -u zzaccd)
    for svc in su sudo login sshd polkit-1 passwd chsh; do
        [ -f "/etc/pam.d/$svc" ] || { echo "note blank_account_refused_by_$svc: no such PAM service here"; continue; }
        if pamok "$svc" zzaccd "$ZU" ""; then notok "blank_account_refused_by_$svc" "the stack authenticated an empty password"; else ok "blank_account_refused_by_$svc"; fi
    done
    if pamok lightdm zzaccd 0 ""; then ok blank_account_accepted_by_the_greeter_stack_without_a_password; else notok blank_account_accepted_by_the_greeter_stack_without_a_password "lightdm refused"; fi
    # a member who has a password must still type it (the reconcile can be late)
    runuser -u wexaccounts -- busctl --system --timeout=20 call org.freedesktop.Accounts "/org/freedesktop/Accounts/User$ZU" org.freedesktop.Accounts.User SetPassword ss "$SECHASH" '' >/dev/null 2>&1
    if ingroup zzaccd; then
        pamok lightdm zzaccd 0 "" && notok member_with_a_password_is_refused_at_the_greeter_without_it "lightdm took an empty answer" || ok member_with_a_password_is_refused_at_the_greeter_without_it
    else echo "note member_with_a_password_is_refused_at_the_greeter_without_it: the group was already corrected (the path unit ran)"; fi
    reconcile
    ingroup zzaccd && notok a_hash_takes_the_user_out_of_nopasswdlogin "groups: $(id -nG zzaccd)" || ok a_hash_takes_the_user_out_of_nopasswdlogin
    # delete and re-create: the group follows the empty fields
    runuser -u wexaccounts -- busctl --system --timeout=20 call org.freedesktop.Accounts /org/freedesktop/Accounts org.freedesktop.Accounts DeleteUser xb "$ZU" 0 >/dev/null 2>&1
    reconcile
    getent group nopasswdlogin | cut -d: -f4 | tr , '\n' | grep -qx zzaccd && notok group_has_no_deleted_user "still listed" || ok group_has_no_deleted_user
    runuser -u wexaccounts -- "${CREATE[@]}" >/dev/null 2>&1
    umode SetPasswordMode i 2; reconcile
    ingroup zzaccd && ok recreated_blank_user_is_in_the_group_again || notok recreated_blank_user_is_in_the_group_again "groups: $(id -nG zzaccd)"
    # disable and enable a blank account: what the real tools do (these decide the helper's enable path)
    umode SetLocked b true; reconcile
    echo "note locked_blank: shadow field [$(shadow zzaccd 2)] expire [$(shadow zzaccd 8)]"
    ingroup zzaccd && notok locked_blank_user_leaves_nopasswdlogin "field [$(shadow zzaccd 2)], groups: $(id -nG zzaccd)" || ok locked_blank_user_leaves_nopasswdlogin
    if umode SetLocked b false; then echo "note enable_blank_account_directly: SetLocked(false) worked: field [$(shadow zzaccd 2)] expire [$(shadow zzaccd 8)]"
    else echo "note enable_blank_account_directly: SetLocked(false) was refused (usermod -U): field [$(shadow zzaccd 2)] expire [$(shadow zzaccd 8)]"; fi
    umode SetPasswordMode i 2
    echo "note locked_then_none: field [$(shadow zzaccd 2)] expire [$(shadow zzaccd 8)]"
    if umode SetLocked b false; then echo "note enable_after_none_again: SetLocked(false) worked: field [$(shadow zzaccd 2)] expire [$(shadow zzaccd 8)]"
    else echo "note enable_after_none_again: SetLocked(false) was refused: field [$(shadow zzaccd 2)] expire [$(shadow zzaccd 8)]"; fi
    reconcile
    echo "note enabled_blank_state: in group [$(ingroup zzaccd && echo yes || echo no)] chage: $(chage -l zzaccd 2>/dev/null | grep -i 'account expires' | cut -d: -f2)"
    if pamok lightdm zzaccd 0 ""; then echo "note enabled_blank_logon_at_the_greeter: accepted"; else echo "note enabled_blank_logon_at_the_greeter: refused"; fi
    runuser -u wexaccounts -- busctl --system --timeout=20 call org.freedesktop.Accounts /org/freedesktop/Accounts org.freedesktop.Accounts DeleteUser xb "$(id -u zzaccd)" 0 >/dev/null 2>&1
else
    notok blank_test_user_created "zzaccd could not be created"
fi

finish
