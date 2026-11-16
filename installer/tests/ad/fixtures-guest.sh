#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# GPO lab (docs/GPO_LAB.md): runs on the controller, sent by gpo-lab.sh fixtures. Makes, or brings up to date,
# what the end-to-end checks expect in the domain:
#
#   OU=WexMachines              the members join into it (net ads join createcomputer=WexMachines)
#   user wexuser1               a domain user; the throwaway password is /root/wexuser1pw
#   user wexadmin1              a member of Domain Admins; the throwaway password is /root/wexadmin1pw (when it is there)
#   GPO "WexLab Domain"         linked at the domain:
#       Machine/Registry.pol    HKLM\Software\Policies\WexLab Limit=7, DomainOnly=1
#       User/Registry.pol       HKCU\Software\Policies\WexLab UserLimit=3
#       GptTmpl.inf             [System Access] LockoutBadCount=7, LockoutDuration=30, ResetLockoutCount=30,
#                               MinimumPasswordLength=8 (not applicable); [Registry Values]
#                               ...\CurrentVersion\Policies\System\WexLabOption=4,3
#   GPO "WexLab OU"             linked at OU=WexMachines, applied after the domain's:
#       Machine/Registry.pol    HKLM\Software\Policies\WexLab Limit=9 (wins), OuOnly=1
#
# Each object gets its extension names (gPCMachineExtensionNames, gPCUserExtensionNames), as the Windows editor
# writes them, and a new version, so clients see a change. The files come from the host in /root/fixtures/.
set -euo pipefail

BASE='DC=ad,DC=wexlab,DC=example'
REALM_DNS=ad.wexlab.example
AUTH="Administrator%$(cat /root/adminpw)"
SYSVOL=/var/lib/samba/sysvol/$REALM_DNS/Policies
SAM=/var/lib/samba/private/sam.ldb
F=/root/fixtures

# client-side extensions and the editor parts that go with them
REG='{35378EAC-683F-11D2-A89A-00C04FBBCFA2}'
SEC='{827D319E-6EAC-11D2-A4EA-00C04F79F83A}'
REG_TOOL_M='{D02B1F72-3407-48AE-BA88-E8213C6761F1}'
REG_TOOL_U='{D02B1F73-3407-48AE-BA88-E8213C6761F1}'
SEC_TOOL='{803E14A0-B4FB-11D0-A0D0-00A0C90F574B}'

guid_of() { samba-tool gpo listall | awk -v n="$1" '$1 == "GPO" { g = $3 } $1 == "display" { sub(/^display name *: */, ""); if ($0 == n) print g }' | head -n 1; }

# make_gpo NAME LINK_DN: the object's GUID; made and linked if it does not exist
make_gpo()
{
    local guid
    guid=$(guid_of "$1")
    if [ -z "$guid" ]; then
        samba-tool gpo create "$1" -U "$AUTH" >/dev/null
        guid=$(guid_of "$1")
    fi
    [ -n "$guid" ] || { echo "cannot make $1" >&2; exit 1; }
    samba-tool gpo getlink "$2" -U "$AUTH" 2>/dev/null | grep -qi "$guid" ||
        samba-tool gpo setlink "$2" "$guid" -U "$AUTH" >/dev/null
    echo "$guid"
}

# set_object GUID MACHINE_EXT USER_EXT: the extension names and a new version (both halves up by one)
set_object()
{
    local dir="$SYSVOL/$1" old new
    old=$(sed -n 's/^Version=//p' "$dir/GPT.INI" 2>/dev/null | tr -d '\r' | head -n 1)
    new=$(( ${old:-0} + 65537 ))
    printf '[General]\r\nVersion=%s\r\n' "$new" > "$dir/GPT.INI"
    {
        echo "dn: CN=$1,CN=Policies,CN=System,$BASE"
        echo "changetype: modify"
        echo "replace: versionNumber"
        echo "versionNumber: $new"
        if [ -n "$2" ]; then echo "-"; echo "replace: gPCMachineExtensionNames"; echo "gPCMachineExtensionNames: $2"; fi
        if [ -n "$3" ]; then echo "-"; echo "replace: gPCUserExtensionNames"; echo "gPCUserExtensionNames: $3"; fi
    } > /root/fixture.ldif
    ldbmodify -H "$SAM" /root/fixture.ldif >/dev/null
    echo "$new"
}

# the OU and the user
samba-tool ou list 2>/dev/null | grep -qx 'OU=WexMachines' || samba-tool ou create "OU=WexMachines,$BASE" >/dev/null
samba-tool user show wexuser1 >/dev/null 2>&1 ||
    samba-tool user create wexuser1 "$(cat /root/wexuser1pw)" --given-name=Wex --surname=User >/dev/null
if [ -s /root/wexadmin1pw ]; then
    samba-tool user show wexadmin1 >/dev/null 2>&1 ||
        samba-tool user create wexadmin1 "$(cat /root/wexadmin1pw)" --given-name=Wex --surname=Admin >/dev/null
    samba-tool group listmembers 'Domain Admins' | grep -qx wexadmin1 ||
        samba-tool group addmembers 'Domain Admins' wexadmin1 >/dev/null
fi

# the object linked at the domain
G1=$(make_gpo 'WexLab Domain' "$BASE")
D="$SYSVOL/$G1"
mkdir -p "$D/Machine/Microsoft/Windows NT/SecEdit" "$D/User"
cp "$F/domain-machine.pol" "$D/Machine/Registry.pol"
cp "$F/domain-user.pol" "$D/User/Registry.pol"
# the security template: UTF-16LE with a byte order mark, CRLF line ends
{
    printf '\xff\xfe'
    printf '%s\r\n' '[Unicode]' 'Unicode=yes' '[Version]' 'signature="$CHICAGO$"' 'Revision=1' \
        '[System Access]' 'MinimumPasswordLength = 8' 'LockoutBadCount = 7' 'LockoutDuration = 30' 'ResetLockoutCount = 30' \
        '[Registry Values]' 'MACHINE\Software\Microsoft\Windows\CurrentVersion\Policies\System\WexLabOption=4,3' |
        iconv -f UTF-8 -t UTF-16LE
} > "$D/Machine/Microsoft/Windows NT/SecEdit/GptTmpl.inf"
V1=$(set_object "$G1" "[$REG$REG_TOOL_M][$SEC$SEC_TOOL]" "[$REG$REG_TOOL_U]")

# the object linked at the OU
G2=$(make_gpo 'WexLab OU' "OU=WexMachines,$BASE")
D="$SYSVOL/$G2"
mkdir -p "$D/Machine"
cp "$F/ou-machine.pol" "$D/Machine/Registry.pol"
V2=$(set_object "$G2" "[$REG$REG_TOOL_M]" "")

samba-tool ntacl sysvolreset >/dev/null 2>&1 || true
echo "fixture domain $G1 version $V1"
echo "fixture ou $G2 version $V2"
echo "FIXTURES-DONE"
