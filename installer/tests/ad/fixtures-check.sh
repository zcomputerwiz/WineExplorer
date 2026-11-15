#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
#
# GPO lab (docs/GPO_LAB.md): runs on the controller, sent by gpo-lab.sh check. Prints what the fixtures made, so
# a person (or a test) can see it: the links, the objects' extension names and versions, the files in SYSVOL,
# and the security template as Samba reads it.
set -uo pipefail

BASE='DC=ad,DC=wexlab,DC=example'
AUTH="Administrator%$(cat /root/adminpw)"
SYSVOL=/var/lib/samba/sysvol/ad.wexlab.example/Policies

echo "== links"
samba-tool gpo getlink "$BASE" -U "$AUTH" 2>&1 | grep -iE 'GPO|options' | sed 's/^/domain: /'
samba-tool gpo getlink "OU=WexMachines,$BASE" -U "$AUTH" 2>&1 | grep -iE 'GPO|options' | sed 's/^/ou: /'
echo "== objects"
ldbsearch -H /var/lib/samba/private/sam.ldb -b "CN=Policies,CN=System,$BASE" '(objectClass=groupPolicyContainer)' \
    displayName versionNumber gPCMachineExtensionNames gPCUserExtensionNames 2>/dev/null |
    grep -E '^(displayName|versionNumber|gPCMachineExtensionNames|gPCUserExtensionNames):'
echo "== files"
for name in 'WexLab Domain' 'WexLab OU'; do
    g=$(samba-tool gpo listall | awk -v n="$name" '$1 == "GPO" { g = $3 } $1 == "display" { sub(/^display name *: */, ""); if ($0 == n) print g }' | head -n 1)
    echo "$name $g"
    (cd "$SYSVOL/$g" && find . -type f | sort | sed 's/^/  /')
    t="$SYSVOL/$g/Machine/Microsoft/Windows NT/SecEdit/GptTmpl.inf"
    if [ -f "$t" ]; then
        echo "  template: $(head -c 2 "$t" | od -An -tx1 | tr -d ' ') BOM, $(iconv -f UTF-16 -t UTF-8 "$t" | grep -c .) lines"
        samba-tool gpo manage security list "$g" -U "$AUTH" 2>&1 | sed 's/^/  samba: /' | head -8
    fi
done
echo "== users"
samba-tool user list | grep -i wex
