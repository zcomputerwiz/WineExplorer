#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# GPO lab (docs/GPO_LAB.md): runs on the joined Debian member, sent by gpo-lab.sh parse. Feeds the real
# "net ads gpo list" of this member and of the domain user wexuser1 to wex-gpupdate.sh's gpo_list
# (/root/wex-gpupdate.sh) and checks what it makes of them against the lab's fixtures.
set -uo pipefail

eval "$(sed -n '/^gpo_list()$/,/^}$/p; /^win_order()$/,/^}$/p' /root/wex-gpupdate.sh)"
H=$(hostname -s | tr 'a-z' 'A-Z')
gpo_list "$H\$" MACHINE > /root/parse-machine.list
gpo_list wexuser1 USER > /root/parse-user.list
samba_order=$(cut -f5 /root/parse-machine.list | tr '\n' '|')
win_order /root/parse-machine.list "$H\$"
win_order /root/parse-user.list wexuser1
m=$(cat /root/parse-machine.list)
u=$(cat /root/parse-user.list)
echo "samba's order: $samba_order"
echo "windows order: $(cut -f5 /root/parse-machine.list | tr '\n' '|')"
printf '%s\n' "$m" | sed 's/^/machine /'
printf '%s\n' "$u" | sed 's/^/user /'

r=PASS
# the domain's object, linked at the domain (3), with the machine's registry and security extensions
awk -F '\t' '$5 == "WexLab Domain" && $3 == 3 && $9 ~ /^\[\{35378EAC-683F-11D2-A89A-00C04FBBCFA2\}\{D02B1F72-/ &&
             $9 ~ /\{827D319E-6EAC-11D2-A4EA-00C04F79F83A\}/ && $1 ~ /^\{[0-9A-F-]+\}$/ { f = 1 } END { exit !f }' <<<"$m" ||
    { echo "not ok: the machine's list"; r=FAIL; }
# for the user: the user's registry extension only
awk -F '\t' '$5 == "WexLab Domain" && $9 == "[{35378EAC-683F-11D2-A89A-00C04FBBCFA2}{D02B1F73-3407-48AE-BA88-E8213C6761F1}]" { f = 1 }
             END { exit !f }' <<<"$u" || { echo "not ok: the user's list"; r=FAIL; }
# Samba's own "Local Policy" entry is not an object of the domain
! grep -q 'Local Policy' <<<"$m$u" || { echo "not ok: Local Policy listed"; r=FAIL; }
# the order (MS-GPOL 3.2.5.1): samba-tool gpo setlink writes a new link at the front of gPLink, which on Windows
# is link order 1, applied last; so of the domain's three objects "WexLab Domain", linked last, comes last
last=$(awk -F '\t' '$5 == "WexLab Domain" || $5 == "WexLab" || $5 == "Default Domain Policy" { n = $5 } END { print n }' <<<"$m")
[ "$last" = "WexLab Domain" ] || { echo "not ok: the domain's links in Windows' order (last: $last)"; r=FAIL; }
# nine fields on every line
awk -F '\t' 'NF != 9 { bad = 1 } END { exit bad }' <<<"$m
$u" || { echo "not ok: a line without nine fields"; r=FAIL; }
echo "$r: wex-gpupdate.sh reads net ads gpo list"
