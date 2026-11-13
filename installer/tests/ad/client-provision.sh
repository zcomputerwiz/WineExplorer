#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Lab domain member (docs/GROUP_POLICY.md, "Test setup"): runs once inside the member guest, started by
# cloud-init (client-create.sh writes it into the seed). It puts the guest on the lab network (10.71.0.20, DNS
# from the controller 10.71.0.10), runs the join detection before the join, joins AD.WEXLAB.EXAMPLE with Samba
# (net ads join, Winbind), and runs the detection again. The detection script is
# installer/scripts/wex-domain-state.sh, copied to /root by the seed; its two answers are in /root/detect.before
# and /root/detect.after.
set -euo pipefail
exec > /root/provision.log 2>&1
export DEBIAN_FRONTEND=noninteractive

ADDR=10.71.0.20
DC=10.71.0.10
LAB_MAC=52:54:00:71:00:20

cat > /etc/systemd/network/20-wexlab.network <<EOF
[Match]
MACAddress=$LAB_MAC

[Network]
Address=$ADDR/24
EOF
systemctl restart systemd-networkd
for i in $(seq 1 30); do ip -4 addr | grep -q "$ADDR" && break; sleep 1; done
ip -4 addr | grep -q "$ADDR"

apt-get update
apt-get install -y samba winbind libnss-winbind libpam-winbind krb5-user smbclient dnsutils

bash /root/wex-domain-state.sh --print > /root/detect.before

# names come from the controller from here on; it forwards the others
systemctl disable --now systemd-resolved 2>/dev/null || true
rm -f /etc/resolv.conf
printf 'search ad.wexlab.example\nnameserver %s\n' "$DC" > /etc/resolv.conf
for i in $(seq 1 30); do host -t SRV _ldap._tcp.ad.wexlab.example >/dev/null 2>&1 && break; sleep 2; done
host -t SRV _ldap._tcp.ad.wexlab.example

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
    template shell = /bin/bash
    template homedir = /home/%U
EOF
cat > /etc/krb5.conf <<EOF
[libdefaults]
    default_realm = AD.WEXLAB.EXAMPLE
    dns_lookup_realm = false
    dns_lookup_kdc = true
EOF

net ads join -U "Administrator%$(cat /root/adminpw)"
systemctl restart winbind
net ads testjoin
kinit Administrator < /root/adminpw
klist | head -5

bash /root/wex-domain-state.sh --print > /root/detect.after
echo "before=[$(cat /root/detect.before)] after=[$(cat /root/detect.after)]"
echo PROVISION-DONE
touch /root/provision.done
