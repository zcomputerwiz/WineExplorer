#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# GPO lab (docs/GPO_LAB.md): runs on the joined Debian member, sent by gpo-lab.sh parse. Feeds the real
# "net ads gpo list" of this member and of the domain user wexuser1 to wex-gpupdate.sh's gpo_list
# (/root/wex-gpupdate.sh) and checks what it makes of them against the lab's fixtures.
set -uo pipefail

eval "$(sed -n '/^gpo_list()$/,/^}$/p' /root/wex-gpupdate.sh)"
H=$(hostname -s | tr 'a-z' 'A-Z')
m=$(gpo_list "$H\$" MACHINE)
u=$(gpo_list wexuser1 USER)
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
# nine fields on every line
awk -F '\t' 'NF != 9 { bad = 1 } END { exit bad }' <<<"$m
$u" || { echo "not ok: a line without nine fields"; r=FAIL; }
echo "$r: wex-gpupdate.sh reads net ads gpo list"
