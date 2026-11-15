#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Domain Group Policy for Windows programs (docs/GROUP_POLICY.md, step 4).
#
# Samba fetches the policy; session 0's writer applies it. Run as root by wex-gpupdate.service (at start and
# every 90 minutes), on a machine joined with Samba (net ads join, Winbind):
#
#   1. samba-gpupdate --force brings Samba's cache of the domain's policy objects up to date
#      (/var/cache/samba/gpo_cache/<REALM>/POLICIES/{GUID}/...).
#   2. "net ads gpo list" names the objects that apply to this computer, and to each user of the accounts map
#      who is a domain user, in the order Samba applies them.
#   3. Their Registry.pol files are copied to a directory that session 0's account can read, and
#      "wexpolicy apply" writes them: /machine for the computer, /user:SID for each user (the SID is the one
#      of the accounts map; a user who is not logged on gets the policy at the next logon).
#   An account with no Registry.pol at all gets "wexpolicy clear".
#
#   wex-gpupdate.sh           do it
#   wex-gpupdate.sh --print   steps 1 and 2 only: print "machine FILE" and "user NAME FILE" lines, write nothing
#
# This is a shell script on Samba's command line tools on purpose: an extension inside samba-gpupdate would be
# a Python file under Samba's licence (GPL-3.0-or-later).
set -u

PRINT=0
[ "${1:-}" = --print ] && PRINT=1

MACHINE_ROOT=${WEX_MACHINE_ROOT:-/var/lib/wineexplorer/machine}
S0_USER=${WEX_SESSION0_USER:-wexsys}
WINE=${WINE:-/usr/local/bin/wine}
MAP=${WEX_ACCOUNTS_MAP:-/etc/wineexplorer/accounts.map}
STAGE=/run/wineexplorer-policy

command -v net >/dev/null 2>&1 && command -v samba-gpupdate >/dev/null 2>&1 || { echo "wex-gpupdate: Samba's tools are not installed"; exit 0; }
REALM=$(testparm -s --parameter-name=realm 2>/dev/null | tr 'a-z' 'A-Z')
[ -n "$REALM" ] && net ads testjoin >/dev/null 2>&1 || { echo "wex-gpupdate: not joined to a domain"; exit 0; }

CACHE=$(testparm -s --parameter-name='cache directory' 2>/dev/null)/gpo_cache/$REALM/POLICIES
samba-gpupdate --force >/dev/null 2>&1 || echo "wex-gpupdate: samba-gpupdate failed; the cache may be old" >&2

# The objects that apply to an account ($1), in the order Samba applies them, as the lines that
# "wexpolicy record" takes: GUID, version, link type, options, display name, file system path, DS path, link and
# the extensions of $2 (MACHINE or USER), separated by tabs. Samba's dump_gpo prints them on the error stream,
# one block per object starting with "name:"; the extension details below it are indented and skipped.
gpo_list()
{
    net ads gpo list "$1" -P 2>&1 | awk -v kind="$2" '
        function val(s) { sub(/^[a-z_]+:[ \t]*/, "", s); gsub(/\t/, " ", s); return s }
        function first(s) { s = val(s); sub(/[ \t].*/, "", s); return s + 0 }
        function flush() {
            # Windows gives the two directory paths as LDAP URLs
            if (ds != "" && ds !~ /^LDAP:/) ds = "LDAP://" ds
            if (ln != "" && ln !~ /^LDAP:/) ln = "LDAP://" ln
            if (guid ~ /^\{[0-9A-F-]+\}$/)
                printf "%s\t%.0f\t%d\t%d\t%s\t%s\t%s\t%s\t%s\n", guid, ver, lt, opt, dn, fs, ds, ln, ext
            guid = ""
        }
        /^name:/ { flush(); guid = toupper(val($0)); ver = lt = opt = 0; dn = fs = ds = ln = ext = ""; next }
        guid == "" { next }
        /^displayname:/ { dn = val($0) }
        /^version:/ { ver = first($0); if (ver < 0) ver += 4294967296 }
        /^filesyspath:/ { fs = val($0) }
        /^dspath:/ { ds = val($0) }
        /^options:/ { opt = first($0) }
        /^link:/ { ln = val($0) }
        /^link_type:/ { lt = first($0); if (lt < 0 || lt > 4) lt = 0 }
        /^machine_extensions:/ { if (kind == "MACHINE") { ext = val($0); if (ext !~ /^\[/) ext = "" } }
        /^user_extensions:/ { if (kind == "USER") { ext = val($0); if (ext !~ /^\[/) ext = "" } }
        END { flush() }'
}

# the files of the objects in a list of gpo_list ($1), in order; $2 is MACHINE or USER, $3 the file's path below
# it (Registry.pol, or the security template)
pol_files()
{
    local guid file
    cut -f1 "$1" | while read -r guid; do
        file=$(find "$CACHE/$guid/$2" -ipath "$CACHE/$guid/$2/${3:-registry.pol}" -type f 2>/dev/null | head -n 1)
        [ -n "$file" ] && [ -s "$file" ] && echo "$file"
    done
}

# the list of applied objects for GetAppliedGPOList: "/machine" or "/user:SID", the gpo_list file
record()
{
    local copy="$STAGE/$(basename "$2")"
    install -m 0644 "$2" "$copy"
    s0_wexpolicy record "$1" "Z:${copy//\//\\}"
}

s0_wexpolicy()
{
    (cd / && runuser -u "$S0_USER" -- env -i WINEPREFIX="$MACHINE_ROOT" HOME="$MACHINE_ROOT" PATH=/usr/bin:/bin \
        WINEDEBUG=-all "$WINE" wexpolicy "$@" </dev/null 2>/dev/null) | tr -d '\r'
}

# apply the files of one target: "/machine" or "/user:SID"
apply()
{
    local target=$1 name=$2 n=0 args=() file copy
    shift 2
    for file in "$@"; do
        n=$((n + 1))
        copy="$STAGE/$name-$n.pol"
        install -m 0644 "$file" "$copy"
        args+=("Z:${copy//\//\\}")
    done
    if [ "$n" -gt 0 ]; then s0_wexpolicy apply "$target" "${args[@]}"
    else s0_wexpolicy clear "$target" >/dev/null; echo "wexpolicy: $target has no domain policy"; fi
}

HOST=$(hostname -s | tr 'a-z' 'A-Z')
LISTS=$(mktemp -d)
trap 'rm -rf "$LISTS"' EXIT
gpo_list "$HOST\$" MACHINE > "$LISTS/machine.list"
mapfile -t MACHINE_FILES < <(pol_files "$LISTS/machine.list" MACHINE)
# the security templates: the account lockout policy and the security options (wexpolicy's sectmpl.c)
mapfile -t TEMPLATES < <(pol_files "$LISTS/machine.list" MACHINE 'microsoft/windows nt/secedit/gpttmpl.inf')

if [ "$PRINT" = 1 ]; then
    sed 's/^/object machine /' "$LISTS/machine.list"
    for f in "${MACHINE_FILES[@]}"; do echo "machine $f"; done
    for f in "${TEMPLATES[@]}"; do echo "security $f"; done
else
    rm -rf "$STAGE"
    install -d -m 0755 "$STAGE"
    apply /machine machine "${MACHINE_FILES[@]}"
    record /machine "$LISTS/machine.list"
    # security settings stay when an object no longer sets them, as on Windows: no template, nothing to do
    if [ "${#TEMPLATES[@]}" -gt 0 ]; then
        args=(); n=0
        for f in "${TEMPLATES[@]}"; do
            n=$((n + 1)); install -m 0644 "$f" "$STAGE/security-$n.inf"; args+=("Z:${STAGE//\//\\}\\security-$n.inf")
        done
        s0_wexpolicy security /machine "${args[@]}"
    fi
fi

# the users of the accounts map: name in column 1, "active" in column 4, the SID's last four parts in 6 to 9
[ -r "$MAP" ] || exit 0
awk '$4 == "active" { print $1, "S-1-5-21-" $6 "-" $7 "-" $8 "-" $9 }' "$MAP" | while read -r name sid; do
    # only domain users have policy objects; a local account is not known to the domain ("net ads gpo list"
    # does not fail for an unknown name, so Winbind is asked)
    wbinfo --name-to-sid "$name" 2>/dev/null | grep -q 'SID_USER' || continue
    gpo_list "$name" USER > "$LISTS/user-$name.list"
    mapfile -t USER_FILES < <(pol_files "$LISTS/user-$name.list" USER)
    if [ "$PRINT" = 1 ]; then
        sed "s/^/object user $name /" "$LISTS/user-$name.list"
        for f in "${USER_FILES[@]}"; do echo "user $name $f"; done
    else
        apply "/user:$sid" "user-$name" "${USER_FILES[@]}"
        record "/user:$sid" "$LISTS/user-$name.list"
    fi
done
exit 0
