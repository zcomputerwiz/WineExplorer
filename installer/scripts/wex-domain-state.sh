#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# The host's Active Directory join, told to Windows programs (docs/GROUP_POLICY.md, step 2).
#
# Run by wex-session0-services.service as session 0's account after Windows has started there. It asks the host
# whether it is joined to a domain and writes the answer to HKLM\Software\WineExplorer\Domain (FlatName, Name);
# wkscli (NetGetJoinInformation) and dsrole (DsRoleGetPrimaryDomainInformation) read it. Session 0 holds HKLM for
# every session, so all users' programs see it at once. A host that is not joined gets the key removed.
#
#   wex-domain-state.sh           detect and write
#   wex-domain-state.sh --print   detect and print "FLAT dns.name", or nothing; writes nothing
#
# Detection uses no network and no root rights:
#   realmd (SSSD or Winbind joins made with "realm join"):  realm list
#   Samba (joins made with "net ads join"):                 smb.conf has security = ads and a realm, and the
#                                                           machine has a keytab
# The flat (NetBIOS) name is smb.conf's workgroup when Samba is configured, else the first label of the DNS name.

dns=
flat=

if command -v realm >/dev/null 2>&1; then
    dns=$(realm list 2>/dev/null | sed -n 's/^ *domain-name: *//p' | head -n 1)
fi

if command -v testparm >/dev/null 2>&1; then
    security=$(testparm -s --parameter-name=security 2>/dev/null | tr 'A-Z' 'a-z')
    realm=$(testparm -s --parameter-name=realm 2>/dev/null | tr 'A-Z' 'a-z')
    workgroup=$(testparm -s --parameter-name=workgroup 2>/dev/null | tr 'a-z' 'A-Z')
    if [ -z "$dns" ] && [ "$security" = ads ] && [ -n "$realm" ] && [ -e /etc/krb5.keytab ]; then
        dns=$realm
    fi
    if [ -n "$dns" ] && [ -n "$workgroup" ] && [ "$workgroup" != WORKGROUP ]; then
        flat=$workgroup
    fi
fi

if [ -n "$dns" ] && [ -z "$flat" ]; then
    flat=$(printf '%s' "${dns%%.*}" | tr 'a-z' 'A-Z' | cut -c 1-15)
fi

# the names become registry values and are shown to programs: only what a domain name may contain
case "$dns$flat" in
    *[!A-Za-z0-9._-]*) echo "wex-domain-state: unexpected characters in the domain names; not joined is assumed" >&2
                       dns=; flat= ;;
esac

if [ "$1" = --print ]; then
    [ -n "$dns" ] && echo "$flat $dns"
    exit 0
fi

WINE=${WINE:-/usr/local/bin/wine}
KEY='HKLM\Software\WineExplorer\Domain'
if [ -n "$dns" ]; then
    "$WINE" reg add "$KEY" /v FlatName /d "$flat" /f >/dev/null &&
    "$WINE" reg add "$KEY" /v Name /d "$dns" /f >/dev/null || { echo "wex-domain-state: writing the join state failed" >&2; exit 1; }
    echo "wex-domain-state: joined to $dns ($flat)"
else
    "$WINE" reg delete "$KEY" /f >/dev/null 2>&1
    echo "wex-domain-state: not joined to a domain"
fi
exit 0
