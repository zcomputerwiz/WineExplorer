#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Runs INSIDE an installed session 0 machine as root (sent there by installer/tests/ad/machine.sh with
# WEX_GUEST=join-guest.sh). The domain join of docs/DOMAIN_JOIN.md against the lab domain, from the user's side: the
# machine is not joined at the start, Samba is only installed (the engine's packages), and netdom does the joining.
# One line per check, "ok NAME" or "not ok NAME: detail" ("note NAME: text" for a finding), then AD-GUEST-DONE.
#
#   join-guest.sh [INSTALLED_USER]     (default wineuser)
#
# /root/adminpw holds the lab domain's Administrator password. The elevated commands run the way the policy editor's
# do (consent dialog, answered by the host: machine.sh, host step "consent").
set -uo pipefail

INSTALLED_USER=${1:-wineuser}
ADDR=10.71.0.30
DC=10.71.0.10
LAB_MAC=52:54:00:71:00:30
DOM=ad.wexlab.example
export DEBIAN_FRONTEND=noninteractive
PROBE=/usr/local/share/wex-lab-probe/tierprobe64.exe
PW=$(cat /root/adminpw)

ok() { echo "ok $1"; }
notok() { echo "not ok $1: $(printf '%s' "$2" | tr '\n' ' ' | cut -c1-300)"; }
finish() { ip route del blackhole "$DC/32" 2>/dev/null; rm -f /root/adminpw /tmp/join-pw.txt; echo "AD-GUEST-DONE"; exit 0; }
wine_cmd() { su - "$1" -c "cd /tmp; DISPLAY=:0 WINEDEBUG=-all timeout ${3:-120} $2" 2>&1; }
host_step()
{
    local name=$1 i
    rm -f "/root/host-done-$name"
    echo "AD-GUEST-HOST-STEP $*"
    for i in $(seq 1 300); do [ -e "/root/host-done-$name" ] && break; sleep 5; done
    HOST_ANSWER=$(cat "/root/host-done-$name" 2>/dev/null)
    [ "$HOST_ANSWER" = ok ]
}
s0_reg() { (cd / && runuser -u wexsys -- env -i WINEPREFIX=/var/lib/wineexplorer/machine HOME=/var/lib/wineexplorer/machine \
    PATH=/usr/bin:/bin WINEDEBUG=-all /usr/local/bin/wine "$@" </dev/null 2>/dev/null); }

# the lab network and the engine's packages (the join itself is netdom's)
LAB_IF=$(ip -o link | grep -i "$LAB_MAC" | awk -F': ' '{ print $2 }')
[ -n "$LAB_IF" ] || { notok lab_network "no card $LAB_MAC"; finish; }
lab_up()
{
    command -v nmcli >/dev/null 2>&1 && nmcli device set "$LAB_IF" managed no >/dev/null 2>&1
    ip link set "$LAB_IF" up
    ip addr show dev "$LAB_IF" | grep -q "$ADDR/" || ip addr add "$ADDR/24" dev "$LAB_IF" 2>/dev/null
}
lab_up
rm -f /etc/resolv.conf
printf 'search %s\nnameserver %s\n' "$DOM" "$DC" > /etc/resolv.conf
for i in $(seq 1 30); do getent hosts "dc1.$DOM" >/dev/null 2>&1 && break; sleep 2; done
getent hosts "dc1.$DOM" >/dev/null 2>&1 && ok lab_network || { notok lab_network "the controller's name does not resolve"; finish; }
apt-get update > /root/ad-apt.log 2>&1
apt-get install -y samba winbind libnss-winbind libpam-winbind krb5-user dnsutils >> /root/ad-apt.log 2>&1
command -v net >/dev/null 2>&1 && ok packages_installed || { notok packages_installed "$(tail -3 /root/ad-apt.log)"; finish; }
# the installation may put the old name server back
lab_up
rm -f /etc/resolv.conf
printf 'search %s\nnameserver %s\n' "$DOM" "$DC" > /etc/resolv.conf
grep -q "dc1.$DOM" /etc/hosts || echo "$DC dc1.$DOM dc1" >> /etc/hosts

# 1. the service: the socket is listening, for the system group only
sock=/run/wex-domain-join.sock
if systemctl is-active wex-domain-join.socket >/dev/null 2>&1 && [ -S "$sock" ]; then ok socket_active
else notok socket_active "$(systemctl status wex-domain-join.socket 2>&1 | head -6) $(ls -l $sock 2>&1)"; fi
perm=$(stat -c '%a %U:%G' "$sock" 2>/dev/null)
if [ "$perm" = '660 root:wexsys' ]; then ok socket_permissions; else notok socket_permissions "[$perm]"; fi
echo "note socket_status: $(systemctl status wex-domain-join.socket 2>&1 | sed -n '1,5p' | tr '\n' '|')"
if [ -z "$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)" ]; then ok not_joined_before; else notok not_joined_before "$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)"; fi

# 2. elevation enforced (a default install does not enforce it), a new session for the user
s0_reg reg add 'HKLM\Software\Wine\Elevation' /v Helper /t REG_SZ /d 'C:\windows\system32\consent.exe' /f >/dev/null
# (the user's session stays: an installed image has no automatic logon after a session ends)
sleep 5

# 3. a user's limited token is refused: netdom reaches the socket from a process outside the system group
LIM=$(wine_cmd "$INSTALLED_USER" "wine netdom verify" 60 | tr -d '\r')
echo "note limited_netdom_verify: $(tr '\n' '|' <<<"$LIM")"
if grep -qi 'access.*denied' <<<"$LIM"; then ok limited_token_refused
else notok limited_token_refused "$LIM"; fi

# 4. elevated: netdom join, verify, query dc, remove, in one batch behind one consent dialog
rm -f /tmp/join-pw.txt
printf '%s\n' "$PW" > /root/join-pw.src
install -m 0600 -o "$INSTALLED_USER" /root/join-pw.src /tmp/join-pw.txt; rm -f /root/join-pw.src
EL=/tmp/join-elevated.out
rm -f "$EL"; : > "$EL"; chmod 666 "$EL"
cat > /tmp/join-el.bat <<'BAT'
@echo off
set O=Z:\tmp\join-elevated.out
echo === join >> %O%
netdom join /Domain:ad.wexlab.example /UserD:Administrator /PasswordD:* /OU:"OU=WexMachines,DC=ad,DC=wexlab,DC=example" < Z:\tmp\join-pw.txt >> %O% 2>&1
echo rc=%ERRORLEVEL% >> %O%
echo === verify >> %O%
netdom verify >> %O% 2>&1
echo rc=%ERRORLEVEL% >> %O%
echo === querydc >> %O%
netdom query dc >> %O% 2>&1
echo rc=%ERRORLEVEL% >> %O%
echo === joined >> %O%
BAT
sed -i 's/$/\r/' /tmp/join-el.bat; chmod 644 /tmp/join-el.bat
su - "$INSTALLED_USER" -c "cd /tmp; DISPLAY=:0 WINEDEBUG=-all timeout 400 wine $PROBE runas cmd.exe '/c Z:\\tmp\\join-el.bat'" > /tmp/join-runas.out 2>&1 &
if host_step consent; then
    for i in $(seq 1 120); do grep -q '^=== joined' "$EL" && break; sleep 3; done
else notok consent "$HOST_ANSWER"; finish; fi
tr -d '\r' < "$EL" | sed 's/^/note elevated: /'
section() { tr -d '\r' < "$EL" | sed 's/ *$//' | awk -v s="=== $1" '$0 == s { on = 1; next } /^=== / { on = 0 } on'; }
J=$(section join); V=$(section verify); Q=$(section querydc)
if grep -qx 'rc=0' <<<"$J" && grep -q 'The command completed successfully' <<<"$J"; then ok netdom_join
else notok netdom_join "$J (runas: $(tr -d '\r' < /tmp/join-runas.out | tail -2 | tr '\n' ' '))"; fi
if grep -qx 'rc=0' <<<"$V" && grep -q "has been verified" <<<"$V"; then ok netdom_verify; else notok netdom_verify "$V"; fi
if grep -qx 'rc=0' <<<"$Q" && grep -qi 'dc1' <<<"$Q"; then ok netdom_query_dc; else notok netdom_query_dc "$Q"; fi
# the machine side
net ads testjoin >/dev/null 2>&1 && ok samba_testjoin || notok samba_testjoin "$(net ads testjoin 2>&1)"
det=$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)
[ "$det" = "WEXLAB $DOM" ] && ok join_detected || notok join_detected "[$det]"
ACCOUNT_DN=$(samba-tool computer show "$(hostname -s)" -H "ldap://dc1.$DOM" -U "Administrator%$PW" --attributes=distinguishedName 2>/dev/null | sed -n 's/^distinguishedName: //p')
case "$ACCOUNT_DN" in *OU=WexMachines,*) ok account_in_ou ;; *) notok account_in_ou "[$ACCOUNT_DN]" ;; esac
wbinfo -t >/dev/null 2>&1 && ok winbind_trust || notok winbind_trust "$(wbinfo -t 2>&1)"
getent passwd 'WEXLAB\wexuser1' >/dev/null 2>&1 && ok domain_user_resolves || notok domain_user_resolves "$(wbinfo -i 'WEXLAB\wexuser1' 2>&1)"
# a second join is refused (2691)
systemctl restart wex-session0-services.service 2>/dev/null
sleep 5

# 5. the journal: a line per request, never the password
journalctl --no-pager -b 0 -t wex-domain-join 2>/dev/null | sed 's/^/note journal: /' | cut -c1-200
if journalctl --no-pager -b 0 -t wex-domain-join 2>/dev/null | grep -q 'op=join'; then ok journal_has_request_lines; else notok journal_has_request_lines "none"; fi
if journalctl --no-pager -b 0 2>/dev/null | grep -qF -- "$PW"; then notok journal_has_no_password "the password is in the journal"
else ok journal_has_no_password; fi
if grep -rqF -- "$PW" /etc/samba /etc/krb5.conf /var/log/samba 2>/dev/null; then notok no_password_in_config "found in /etc/samba, krb5.conf or /var/log/samba"; else ok no_password_in_config; fi

# 6. the second elevated batch: a join again is refused, then remove
cat > /tmp/join-el2.bat <<'BAT'
@echo off
set O=Z:\tmp\join-elevated2.out
echo === again >> %O%
netdom join /Domain:ad.wexlab.example /UserD:Administrator /PasswordD:* < Z:\tmp\join-pw.txt >> %O% 2>&1
echo rc=%ERRORLEVEL% >> %O%
echo === remove >> %O%
netdom remove /Domain:ad.wexlab.example /UserD:Administrator /PasswordD:* < Z:\tmp\join-pw.txt >> %O% 2>&1
echo rc=%ERRORLEVEL% >> %O%
echo === done >> %O%
BAT
EL2=/tmp/join-elevated2.out; rm -f "$EL2"; : > "$EL2"; chmod 666 "$EL2"
sed -i 's/$/\r/' /tmp/join-el2.bat; chmod 644 /tmp/join-el2.bat
su - "$INSTALLED_USER" -c "cd /tmp; DISPLAY=:0 WINEDEBUG=-all timeout 400 wine $PROBE runas cmd.exe '/c Z:\\tmp\\join-el2.bat'" > /tmp/join-runas2.out 2>&1 &
if host_step consent; then
    for i in $(seq 1 120); do grep -q '^=== done' "$EL2" && break; sleep 3; done
else notok consent2 "$HOST_ANSWER"; finish; fi
tr -d '\r' < "$EL2" | sed 's/^/note elevated2: /'
section2() { tr -d '\r' < "$EL2" | sed 's/ *$//' | awk -v s="=== $1" '$0 == s { on = 1; next } /^=== / { on = 0 } on'; }
A=$(section2 again); R=$(section2 remove)
if grep -qx 'rc=2691' <<<"$A"; then ok second_join_refused; else notok second_join_refused "$A"; fi
if grep -qx 'rc=0' <<<"$R"; then ok netdom_remove; else notok netdom_remove "$R"; fi
if [ -z "$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)" ]; then ok left_detected; else notok left_detected "$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)"; fi
if net ads testjoin >/dev/null 2>&1; then notok left_testjoin "still joined"; else ok left_testjoin; fi
if samba-tool computer show "$(hostname -s)" -H "ldap://dc1.$DOM" -U "Administrator%$PW" >/dev/null 2>&1; then
    echo "note account_after_remove: still in the directory (disabled or kept)"
else echo "note account_after_remove: deleted"; fi

s0_reg reg delete 'HKLM\Software\Wine\Elevation' /v Helper /f >/dev/null
finish
