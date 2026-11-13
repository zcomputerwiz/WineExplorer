#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Lab Active Directory controller (docs/GROUP_POLICY.md, "Test setup"): makes and starts the controller guest.
#
#   dc-create.sh          make the guest if it does not exist, start it, wait until the domain answers
#   dc-create.sh stop     shut the guest down
#   dc-create.sh ssh CMD  run CMD in the guest as root
#
# The guest is a Debian 12 cloud image (from cloud.debian.org) set up by cloud-init, which runs dc-provision.sh:
# a Samba AD domain controller for AD.WEXLAB.EXAMPLE (flat name WEXLAB).
#
#   directory   $WEX_AD_LAB (default /var/tmp/wex-ad-lab): disks, logs, the SSH key and the throwaway
#               Administrator password (admin_password.txt, mode 600, never printed)
#   card 1      QEMU user-mode network, for packages; SSH to the guest is forwarded from 127.0.0.1:2241
#   card 2      the lab network 10.71.0.0/24, a QEMU multicast socket (230.0.0.71:17071); the controller is
#               10.71.0.10. Another guest joins it with
#                 -netdev socket,id=lab,mcast=230.0.0.71:17071 -device virtio-net-pci,netdev=lab,mac=52:54:00:71:00:NN
#               takes an address in 10.71.0.0/24 and uses 10.71.0.10 as its DNS server.
#
# Requires root, QEMU/KVM, and cloud-localds (cloud-image-utils), genisoimage or xorriso. Nothing here touches other guests.
set -euo pipefail

LAB=${WEX_AD_LAB:-/var/tmp/wex-ad-lab}
HERE=$(cd "$(dirname "$0")" && pwd)
BASE=$LAB/debian-12-genericcloud-amd64.qcow2
BASE_URL=https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-genericcloud-amd64.qcow2
DISK=$LAB/dc1.qcow2
PIDFILE=$LAB/dc1.pid
SSH_PORT=2241

lab_ssh() { ssh -i "$LAB/lab_key" -p "$SSH_PORT" -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null \
                -o LogLevel=ERROR -o ConnectTimeout=5 root@127.0.0.1 "$@"; }
running() { [ -s "$PIDFILE" ] && kill -0 "$(cat "$PIDFILE")" 2>/dev/null; }

case "${1:-}" in
stop)
    if running; then
        lab_ssh poweroff 2>/dev/null || true
        for i in $(seq 1 30); do running || break; sleep 1; done
        if running; then kill "$(cat "$PIDFILE")"; fi
    fi
    rm -f "$PIDFILE"
    echo "dc1 stopped"
    exit 0 ;;
ssh)
    shift
    lab_ssh "$@"
    exit $? ;;
esac

mkdir -p "$LAB"
chmod 700 "$LAB"
[ -s "$LAB/lab_key" ] || ssh-keygen -q -t ed25519 -N '' -f "$LAB/lab_key"
if [ ! -s "$LAB/admin_password.txt" ]; then
    (umask 077; echo "Wx$(head -c 9 /dev/urandom | base64 | tr -d '/+=')9a" > "$LAB/admin_password.txt")
fi
[ -s "$BASE" ] || curl -fL -o "$BASE" "$BASE_URL"

[ -s "$DISK" ] || qemu-img create -q -f qcow2 -b "$BASE" -F qcow2 "$DISK" 20G
if [ ! -s "$LAB/dc1-seed.iso" ]; then
    SEED=$LAB/dc1-seed
    rm -rf "$SEED"; mkdir -m 700 "$SEED"
    printf 'instance-id: wexlab-dc1\nlocal-hostname: dc1\n' > "$SEED/meta-data"
    {
        echo '#cloud-config'
        echo 'hostname: dc1'
        echo 'fqdn: dc1.ad.wexlab.example'
        echo 'manage_etc_hosts: false'
        echo 'disable_root: false'
        echo 'users:'
        echo '  - name: root'
        echo '    ssh_authorized_keys:'
        echo "      - $(cat "$LAB/lab_key.pub")"
        echo 'write_files:'
        echo '  - path: /root/adminpw'
        echo "    permissions: '0600'"
        echo "    content: $(cat "$LAB/admin_password.txt")"
        echo '  - path: /etc/hosts'
        echo '    content: |'
        echo '      127.0.0.1 localhost'
        echo '      10.71.0.10 dc1.ad.wexlab.example dc1'
        echo '  - path: /root/dc-provision.sh'
        echo "    permissions: '0700'"
        echo '    encoding: b64'
        echo "    content: $(base64 -w0 "$HERE/dc-provision.sh")"
        echo 'runcmd:'
        echo '  - [ bash, /root/dc-provision.sh ]'
    } > "$SEED/user-data"
    chmod 600 "$SEED/user-data"
    if command -v cloud-localds >/dev/null 2>&1; then
        cloud-localds "$LAB/dc1-seed.iso" "$SEED/user-data" "$SEED/meta-data"
    elif command -v genisoimage >/dev/null 2>&1; then
        genisoimage -quiet -output "$LAB/dc1-seed.iso" -volid cidata -joliet -rock "$SEED/user-data" "$SEED/meta-data"
    else
        xorriso -as mkisofs -quiet -output "$LAB/dc1-seed.iso" -volid cidata -joliet -rock "$SEED/user-data" "$SEED/meta-data"
    fi
    chmod 600 "$LAB/dc1-seed.iso"
fi

if ! running; then
    qemu-system-x86_64 -accel kvm -machine q35 -cpu host -smp 2 -m 2048 -display none \
        -drive file="$DISK",format=qcow2,if=virtio \
        -drive file="$LAB/dc1-seed.iso",format=raw,if=virtio,readonly=on \
        -netdev user,id=out,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22 -device virtio-net-pci,netdev=out,mac=52:54:00:71:01:10 \
        -netdev socket,id=lab,mcast=230.0.0.71:17071 -device virtio-net-pci,netdev=lab,mac=52:54:00:71:00:10 \
        -serial file:"$LAB/dc1-serial.log" -pidfile "$PIDFILE" -daemonize
fi

# the first boot installs Samba and provisions the domain: some minutes
for i in $(seq 1 180); do
    if lab_ssh test -e /root/provision.done 2>/dev/null; then
        lab_ssh "samba-tool domain level show | head -3; host -t SRV _ldap._tcp.ad.wexlab.example 127.0.0.1 | tail -1"
        echo "dc1 ready"
        exit 0
    fi
    running || { echo "dc1 is not running; see $LAB/dc1-serial.log" >&2; exit 1; }
    sleep 5
done
echo "dc1 did not finish its setup in 15 minutes; see $LAB/dc1-serial.log and /root/provision.log in the guest" >&2
lab_ssh tail -5 /root/provision.log 2>/dev/null || true
exit 1
