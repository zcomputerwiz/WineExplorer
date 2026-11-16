#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# The GPO VM lab (docs/GPO_LAB.md): one entry point for the lab domain of Group Policy tests.
#
#   gpo-lab.sh up                 the controller (dc-create.sh), then the fixtures
#   gpo-lab.sh fixtures           make or update the OU, the domain user and the two test objects on the controller
#   gpo-lab.sh check              show what the fixtures made (links, extension names, files, the template)
#   gpo-lab.sh member             a plain Debian member that joins and checks the join detection (client-create.sh)
#   gpo-lab.sh parse              on the member: wex-gpupdate.sh's reading of the real "net ads gpo list"
#   gpo-lab.sh machine SOURCE_VM [TEST_VM]
#                                 Group Policy end to end on an installed WineExplorer machine (machine.sh; run
#                                 where the installed disks are, with VMTEST_DIR)
#   gpo-lab.sh status             which lab guests run, and the objects in the domain
#   gpo-lab.sh down               stop the lab guests of this distribution
#   gpo-lab.sh ssh CMD            a command on the controller
#
# As root. The controller and the member run in the WSL distribution that holds $WEX_AD_LAB (default
# /var/tmp/wex-ad-lab; today Debian); an installed machine runs where its disk is (wexbuild) and reaches the
# controller over QEMU's multicast network, which crosses WSL distributions. "machine" needs
# $WEX_AD_LAB/admin_password.txt in its own distribution: copy that one file over.
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../../.." && pwd)
LAB=${WEX_AD_LAB:-/var/tmp/wex-ad-lab}
export WEX_AD_LAB=$LAB

lab_scp() { scp -q -i "$LAB/lab_key" -P 2241 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR "$@"; }
dc() { bash "$HERE/dc-create.sh" ssh "$@"; }
pid_running() { [ -s "$1" ] && kill -0 "$(cat "$1")" 2>/dev/null; }

fixtures()
{
    local t
    [ -s "$LAB/wexuser1_password.txt" ] ||
        (umask 077; echo "Wu$(head -c 9 /dev/urandom | base64 | tr -d '/+=')7b" > "$LAB/wexuser1_password.txt")
    [ -s "$LAB/wexadmin1_password.txt" ] ||
        (umask 077; echo "Wa$(head -c 9 /dev/urandom | base64 | tr -d '/+=')7c" > "$LAB/wexadmin1_password.txt")
    t=$(mktemp -d)
    python3 -I "$REPO/scripts/lab/make-preg.py" "$t/domain-machine.pol" 'Software\Policies\WexLab' Limit 7 \
        'Software\Policies\WexLab' DomainOnly 1
    python3 -I "$REPO/scripts/lab/make-preg.py" "$t/domain-user.pol" 'Software\Policies\WexLab' UserLimit 3
    python3 -I "$REPO/scripts/lab/make-preg.py" "$t/ou-machine.pol" 'Software\Policies\WexLab' Limit 9 \
        'Software\Policies\WexLab' OuOnly 1
    dc 'rm -rf /root/fixtures && mkdir -m 700 /root/fixtures'
    lab_scp "$t"/*.pol root@127.0.0.1:/root/fixtures/
    lab_scp "$HERE/fixtures-guest.sh" root@127.0.0.1:/root/fixtures-guest.sh
    lab_scp "$LAB/wexuser1_password.txt" root@127.0.0.1:/root/wexuser1pw
    lab_scp "$LAB/wexadmin1_password.txt" root@127.0.0.1:/root/wexadmin1pw
    rm -rf "$t"
    dc 'chmod 600 /root/wexuser1pw /root/wexadmin1pw; bash /root/fixtures-guest.sh'
}

case "${1:-}" in
up)
    bash "$HERE/dc-create.sh"
    fixtures ;;
fixtures)
    fixtures ;;
check)
    lab_scp "$HERE/fixtures-check.sh" root@127.0.0.1:/root/fixtures-check.sh
    dc 'bash /root/fixtures-check.sh' ;;
member)
    bash "$HERE/client-create.sh" ;;
parse)
    # wex-gpupdate.sh's reading of "net ads gpo list" against the real thing, on the member (running, joined)
    lab_scp_member() { scp -q -i "$LAB/lab_key" -P 2242 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null -o LogLevel=ERROR "$@"; }
    lab_scp_member "$REPO/installer/scripts/wex-gpupdate.sh" "$HERE/parse-guest.sh" root@127.0.0.1:/root/
    bash "$HERE/client-create.sh" ssh 'bash /root/parse-guest.sh' ;;
machine)
    shift
    bash "$HERE/machine.sh" "$@" ;;
status)
    for g in dc1 member1; do
        if pid_running "$LAB/$g.pid"; then echo "$g running"; else echo "$g stopped"; fi
    done
    if pid_running "$LAB/dc1.pid"; then
        dc "samba-tool gpo listall | grep -E '^(GPO|display name)'; samba-tool ou list; samba-tool user list | sort"
    fi ;;
down)
    bash "$HERE/client-create.sh" stop || true
    bash "$HERE/dc-create.sh" stop ;;
ssh)
    shift
    dc "$@" ;;
*)
    sed -n '3,20p' "$0" | sed 's/^# \{0,1\}//'
    exit 2 ;;
esac
