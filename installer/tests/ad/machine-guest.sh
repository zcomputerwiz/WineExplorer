#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Runs INSIDE an installed session 0 machine as root (sent there by installer/tests/ad/machine.sh). It joins
# the machine to the lab domain and checks Group Policy end to end (docs/GROUP_POLICY.md): the join is told to
# Windows programs, and the domain's registry policy reaches them. One line per check, "ok NAME" or
# "not ok NAME: detail" ("note NAME: text" for a finding that is not a check), then AD-GUEST-DONE; the host
# script decides.
#
#   machine-guest.sh [INSTALLED_USER]     (default wineuser)
#
# /root/adminpw holds the lab domain's Administrator password (put there by the host script).
set -uo pipefail

INSTALLED_USER=${1:-wineuser}
ADDR=10.71.0.30
DC=10.71.0.10
LAB_MAC=52:54:00:71:00:30
export DEBIAN_FRONTEND=noninteractive

HKLM_DOMAIN='HKLM\Software\WineExplorer\Domain'
HKLM_POLICY='HKLM\Software\Policies\WexLab'

ok() { echo "ok $1"; }
notok() { echo "not ok $1: $(printf '%s' "$2" | tr '\n' ' ' | cut -c1-240)"; }
finish() { echo "AD-GUEST-DONE"; exit 0; }
wine_cmd() { su - "$1" -c "cd /tmp; DISPLAY=:0 WINEDEBUG=-all timeout 90 $2" 2>&1; }
value() { wine_cmd "$INSTALLED_USER" "wine reg query \"$1\" /v $2" | tr -d '\r' | awk -v n="$2" '$1 == n { print $NF }'; }

# 1. not joined: the detection says nothing and Windows programs see no domain
before=$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)
if [ -z "$before" ] && [ -z "$(value "$HKLM_DOMAIN" FlatName)" ]; then ok not_joined_before
else notok not_joined_before "detection [$before] registry [$(value "$HKLM_DOMAIN" FlatName)]"; fi

# the lab network: the card with the lab address, names from the controller
LAB_IF=$(ip -o link | grep -i "$LAB_MAC" | awk -F': ' '{ print $2 }')
if [ -z "$LAB_IF" ]; then notok lab_network "no card $LAB_MAC"; finish; fi
lab_up()
{
    # the installed system's network service would try DHCP on the card and take the address away again
    command -v nmcli >/dev/null 2>&1 && nmcli device set "$LAB_IF" managed no >/dev/null 2>&1
    ip link set "$LAB_IF" up
    ip addr show dev "$LAB_IF" | grep -q "$ADDR/" || ip addr add "$ADDR/24" dev "$LAB_IF" 2>/dev/null
}
lab_up
rm -f /etc/resolv.conf
printf 'search ad.wexlab.example\nnameserver %s\n' "$DC" > /etc/resolv.conf
found=
for i in $(seq 1 30); do
    if getent hosts dc1.ad.wexlab.example >/dev/null 2>&1; then found=1; break; fi
    sleep 2
done
if [ -n "$found" ]; then ok lab_network; else notok lab_network "the controller's name does not resolve"; finish; fi

# 2. Samba and the join
apt-get update > /root/ad-apt.log 2>&1
apt-get install -y samba winbind krb5-user >> /root/ad-apt.log 2>&1
if ! command -v net >/dev/null 2>&1 || ! command -v samba-gpupdate >/dev/null 2>&1; then
    notok samba_installed "$(tail -3 /root/ad-apt.log)"; finish
fi
ok samba_installed
cat > /etc/samba/smb.conf <<EOF
[global]
    workgroup = WEXLAB
    realm = AD.WEXLAB.EXAMPLE
    security = ads
    kerberos method = secrets and keytab
    idmap config * : backend = tdb
    idmap config * : range = 10000-19999
    idmap config WEXLAB : backend = rid
    idmap config WEXLAB : range = 20000-99999
    winbind use default domain = yes
EOF
cat > /etc/krb5.conf <<EOF
[libdefaults]
    default_realm = AD.WEXLAB.EXAMPLE
    dns_lookup_realm = false
    dns_lookup_kdc = true

[realms]
    AD.WEXLAB.EXAMPLE = {
        kdc = dc1.ad.wexlab.example
        admin_server = dc1.ad.wexlab.example
    }
EOF
# the package installation, or the network service of the installed system, may have put the old name server
# back: the controller is the name server again, and its name is known without one
lab_up
rm -f /etc/resolv.conf
printf 'search ad.wexlab.example\nnameserver %s\n' "$DC" > /etc/resolv.conf
grep -q dc1.ad.wexlab.example /etc/hosts || echo "$DC dc1.ad.wexlab.example dc1" >> /etc/hosts
grep -q "$ADDR" /etc/hosts || echo "$ADDR $(hostname -s).ad.wexlab.example $(hostname -s)" >> /etc/hosts
join=$(net ads join -S dc1.ad.wexlab.example -U "Administrator%$(cat /root/adminpw)" 2>&1)
systemctl restart winbind 2>/dev/null
if net ads testjoin >/dev/null 2>&1; then ok joined
else
    notok joined "$join | addr: $(ip -4 -o addr show dev "$LAB_IF" | awk '{ print $4 }') | ping: $(ping -c 1 -W 2 "$DC" 2>&1 | tail -1) | host: $(hostname -f 2>&1)"
    finish
fi

# 3. the join is told to Windows programs when session 0's services start
detected=$(bash /usr/local/bin/wex-domain-state.sh --print 2>&1)
systemctl restart wex-session0-services.service
sleep 5
flat=$(value "$HKLM_DOMAIN" FlatName)
name=$(value "$HKLM_DOMAIN" Name)
if [ "$detected" = "WEXLAB ad.wexlab.example" ] && [ "$flat" = WEXLAB ] && [ "$name" = ad.wexlab.example ]; then ok join_told_to_programs
else notok join_told_to_programs "detection [$detected] FlatName [$flat] Name [$name]"; fi

# 4. the domain's registry policy reaches a user's program
gp=$(systemctl start wex-gpupdate.service 2>&1; journalctl --no-pager -b 0 -u wex-gpupdate.service 2>/dev/null | tail -6)
limit=$(value "$HKLM_POLICY" Limit)
if [ "$limit" = 0x7 ] || [ "$limit" = 0x9 ]; then ok domain_policy_applied
else notok domain_policy_applied "Limit [$limit]; $gp"; fi

# 5. Can the user change it? Session 0 refuses the write only while elevation is enforced, which an installed
# system does not do by default (every user's token is unrestricted there). Reported as a note, not a check:
# the answer follows from that setting, not from Group Policy. Then the timer.
if wine_cmd "$INSTALLED_USER" "wine reg add \"$HKLM_POLICY\" /v Limit /t REG_DWORD /d 1 /f" >/dev/null 2>&1 &&
   [ "$(value "$HKLM_POLICY" Limit)" = 0x1 ]; then
    echo "note user_write_to_machine_policy: taken (elevation is not enforced on this system)"
    systemctl start wex-gpupdate.service 2>/dev/null
    if [ "$(value "$HKLM_POLICY" Limit)" = 0x7 ] || [ "$(value "$HKLM_POLICY" Limit)" = 0x9 ]; then ok refresh_puts_policy_back
    else notok refresh_puts_policy_back "Limit [$(value "$HKLM_POLICY" Limit)] after a refresh"; fi
else echo "note user_write_to_machine_policy: refused"; fi
if [ "$(systemctl is-enabled wex-gpupdate.timer 2>/dev/null)" = enabled ] && [ "$(systemctl is-active wex-gpupdate.timer)" = active ]; then ok timer_enabled
else notok timer_enabled "$(systemctl is-enabled wex-gpupdate.timer 2>&1) $(systemctl is-active wex-gpupdate.timer 2>&1)"; fi

# 6. the policy service runs once in session 0, and nothing failed
n=$(pgrep -u wexsys -f 'wexpolicy' 2>/dev/null | wc -l)
if [ "$n" -eq 1 ]; then ok policy_service_runs; else notok policy_service_runs "wexpolicy processes of wexsys: $n"; fi
failed=$(systemctl --failed --no-legend 2>/dev/null | grep -i wex || true)
if [ -z "$failed" ]; then ok no_failed_units; else notok no_failed_units "$failed"; fi

finish
