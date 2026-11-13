#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Lab domain member (docs/GROUP_POLICY.md, "Test setup"): makes and starts a plain Debian guest that joins the
# lab domain of dc-create.sh, and checks the join detection (installer/scripts/wex-domain-state.sh) before and
# after the join. The controller has to be running.
#
#   client-create.sh          make the guest if it does not exist, start it, wait for the join, print the result
#   client-create.sh stop     shut the guest down
#   client-create.sh ssh CMD  run CMD in the guest as root
#
# Same directory, key and password as dc-create.sh. SSH is forwarded from 127.0.0.1:2242; the guest is
# 10.71.0.20 on the lab network. PASS is printed when the detection says nothing before the join and
# "WEXLAB ad.wexlab.example" after it.
set -euo pipefail

LAB=${WEX_AD_LAB:-/var/tmp/wex-ad-lab}
HERE=$(cd "$(dirname "$0")" && pwd)
BASE=$LAB/debian-12-genericcloud-amd64.qcow2
DISK=$LAB/member1.qcow2
PIDFILE=$LAB/member1.pid
SSH_PORT=2242

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
    echo "member1 stopped"
    exit 0 ;;
ssh)
    shift
    lab_ssh "$@"
    exit $? ;;
esac

[ -s "$BASE" ] && [ -s "$LAB/lab_key" ] && [ -s "$LAB/admin_password.txt" ] || { echo "run dc-create.sh first" >&2; exit 1; }

[ -s "$DISK" ] || qemu-img create -q -f qcow2 -b "$BASE" -F qcow2 "$DISK" 20G
if [ ! -s "$LAB/member1-seed.iso" ]; then
    SEED=$LAB/member1-seed
    rm -rf "$SEED"; mkdir -m 700 "$SEED"
    printf 'instance-id: wexlab-member1\nlocal-hostname: member1\n' > "$SEED/meta-data"
    {
        echo '#cloud-config'
        echo 'hostname: member1'
        echo 'fqdn: member1.ad.wexlab.example'
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
        echo '      10.71.0.20 member1.ad.wexlab.example member1'
        echo '  - path: /root/wex-domain-state.sh'
        echo "    permissions: '0755'"
        echo '    encoding: b64'
        echo "    content: $(base64 -w0 "$HERE/../../scripts/wex-domain-state.sh")"
        echo '  - path: /root/client-provision.sh'
        echo "    permissions: '0700'"
        echo '    encoding: b64'
        echo "    content: $(base64 -w0 "$HERE/client-provision.sh")"
        echo 'runcmd:'
        echo '  - [ bash, /root/client-provision.sh ]'
    } > "$SEED/user-data"
    chmod 600 "$SEED/user-data"
    if command -v cloud-localds >/dev/null 2>&1; then
        cloud-localds "$LAB/member1-seed.iso" "$SEED/user-data" "$SEED/meta-data"
    elif command -v genisoimage >/dev/null 2>&1; then
        genisoimage -quiet -output "$LAB/member1-seed.iso" -volid cidata -joliet -rock "$SEED/user-data" "$SEED/meta-data"
    else
        xorriso -as mkisofs -quiet -output "$LAB/member1-seed.iso" -volid cidata -joliet -rock "$SEED/user-data" "$SEED/meta-data"
    fi
    chmod 600 "$LAB/member1-seed.iso"
fi

if ! running; then
    qemu-system-x86_64 -accel kvm -machine q35 -cpu host -smp 2 -m 2048 -display none \
        -drive file="$DISK",format=qcow2,if=virtio \
        -drive file="$LAB/member1-seed.iso",format=raw,if=virtio,readonly=on \
        -netdev user,id=out,hostfwd=tcp:127.0.0.1:$SSH_PORT-:22 -device virtio-net-pci,netdev=out,mac=52:54:00:71:01:20 \
        -netdev socket,id=lab,mcast=230.0.0.71:17071 -device virtio-net-pci,netdev=lab,mac=52:54:00:71:00:20 \
        -serial file:"$LAB/member1-serial.log" -pidfile "$PIDFILE" -daemonize
fi

for i in $(seq 1 180); do
    if lab_ssh test -e /root/provision.done 2>/dev/null; then
        before=$(lab_ssh cat /root/detect.before)
        after=$(lab_ssh cat /root/detect.after)
        echo "before the join: [$before]"
        echo "after the join:  [$after]"
        if [ -z "$before" ] && [ "$after" = "WEXLAB ad.wexlab.example" ]; then echo "PASS: join detection"; exit 0; fi
        echo "FAIL: join detection" >&2
        exit 1
    fi
    running || { echo "member1 is not running; see $LAB/member1-serial.log" >&2; exit 1; }
    sleep 5
done
echo "member1 did not finish in 15 minutes; the end of its log:" >&2
lab_ssh tail -15 /root/provision.log 2>/dev/null || true
exit 1
