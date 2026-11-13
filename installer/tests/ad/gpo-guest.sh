#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Lab domain: the part of gpo-create.sh that runs on the controller. /root/gpo-machine.pol and
# /root/gpo-user.pol are the two Registry.pol files.
set -euo pipefail

NAME=WexLab
BASE='DC=ad,DC=wexlab,DC=example'
AUTH="Administrator%$(cat /root/adminpw)"
SYSVOL=/var/lib/samba/sysvol/ad.wexlab.example/Policies

GUID=$(samba-tool gpo listall | awk -v n="$NAME" '$1 == "GPO" { g = $3 } $1 == "display" && $4 == n { print g }' | head -n 1)
if [ -z "$GUID" ]; then
    samba-tool gpo create "$NAME" -U "$AUTH" >/dev/null
    GUID=$(samba-tool gpo listall | awk -v n="$NAME" '$1 == "GPO" { g = $3 } $1 == "display" && $4 == n { print g }' | head -n 1)
    samba-tool gpo setlink "$BASE" "$GUID" -U "$AUTH" >/dev/null
fi
[ -n "$GUID" ]

DIR="$SYSVOL/$GUID"
mkdir -p "$DIR/Machine" "$DIR/User"
cp /root/gpo-machine.pol "$DIR/Machine/Registry.pol"
cp /root/gpo-user.pol "$DIR/User/Registry.pol"

# the version: user changes in the high word, machine changes in the low word; both go up by one
OLD=$(sed -n 's/^Version=//p' "$DIR/GPT.INI" | tr -d '\r' | head -n 1)
NEW=$(( ${OLD:-0} + 65537 ))
printf '[General]\r\nVersion=%s\r\n' "$NEW" > "$DIR/GPT.INI"
cat > /root/gpo-version.ldif <<EOF
dn: CN=$GUID,CN=Policies,CN=System,$BASE
changetype: modify
replace: versionNumber
versionNumber: $NEW
EOF
ldbmodify -H /var/lib/samba/private/sam.ldb /root/gpo-version.ldif >/dev/null
samba-tool ntacl sysvolreset >/dev/null 2>&1 || true

echo "GPO $NAME $GUID version $NEW"
samba-tool gpo getlink "$BASE" -U "$AUTH" | tail -2
