#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Lab Active Directory controller (docs/GROUP_POLICY.md, "Test setup"): runs once inside the controller guest,
# started by cloud-init (dc-create.sh writes it into the seed). It makes the guest a Samba AD domain controller
# for the realm AD.WEXLAB.EXAMPLE (flat name WEXLAB) on the lab network 10.71.0.0/24, address 10.71.0.10.
# The Administrator password is the throwaway lab password in /root/adminpw (mode 600), put there by the seed.
set -euo pipefail
exec > /root/provision.log 2>&1
export DEBIAN_FRONTEND=noninteractive

REALM=AD.WEXLAB.EXAMPLE
DOMAIN=WEXLAB
ADDR=10.71.0.10
LAB_MAC=52:54:00:71:00:10

# the lab network is the second card: a fixed address, no gateway, no DNS from it
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
apt-get install -y samba samba-ad-dc samba-ad-provision krb5-user winbind smbclient ldb-tools dnsutils python3-setproctitle ||
    apt-get install -y samba samba-ad-dc krb5-user winbind smbclient ldb-tools dnsutils

systemctl disable --now smbd nmbd winbind 2>/dev/null || true
systemctl disable --now systemd-resolved 2>/dev/null || true
rm -f /etc/samba/smb.conf

# The forwarder is QEMU's user-mode DNS on the first card, for names outside the lab. Samba serves the lab card
# only: otherwise it registers the first card's address (10.0.2.15, which every guest has) for the controller's
# name, and a member that looks the controller up reaches itself.
LAB_IF=$(ip -o link | grep -i "$LAB_MAC" | awk -F': ' '{ print $2 }')
[ -n "$LAB_IF" ]
samba-tool domain provision --realm="$REALM" --domain="$DOMAIN" --server-role=dc --dns-backend=SAMBA_INTERNAL \
    --adminpass="$(cat /root/adminpw)" --host-ip="$ADDR" --option="dns forwarder = 10.0.2.3" \
    --option="interfaces = lo $LAB_IF" --option="bind interfaces only = yes"
cp /var/lib/samba/private/krb5.conf /etc/krb5.conf

rm -f /etc/resolv.conf
printf 'search ad.wexlab.example\nnameserver 127.0.0.1\n' > /etc/resolv.conf

systemctl unmask samba-ad-dc
systemctl enable samba-ad-dc
systemctl restart samba-ad-dc
for i in $(seq 1 60); do host -t SRV _ldap._tcp.ad.wexlab.example 127.0.0.1 >/dev/null 2>&1 && break; sleep 1; done

samba-tool domain level show
host -t SRV _ldap._tcp.ad.wexlab.example 127.0.0.1
echo PROVISION-DONE
touch /root/provision.done
