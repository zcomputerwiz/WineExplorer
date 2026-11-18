#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# The supported way to join this machine to an Active Directory domain, and to leave it (docs/DOMAIN_LOGON.md, "The
# supported join"). Winbind is the supported join: domain logon (the admission step), the accounts map, domain groups in
# tokens and Group Policy (wex-gpupdate.sh) are built on Samba's tools. A machine joined with SSSD (realm join) is told
# so and is not touched.
#
#   wex-domain-join.sh join DOMAIN --user NAME --password-stdin [--workgroup NAME] [--ou DN]
#   wex-domain-join.sh leave --user NAME --password-stdin      the machine account is removed from the domain
#   wex-domain-join.sh leave --force                           only this machine's side (the controller is out of
#                                                              reach): the machine account stays in the domain
#   wex-domain-join.sh status                                  what the machine is joined to and what is set up
#
# The administrator's password is read from standard input only, never from the command line (which every user can
# read in the process list), and is given to kinit; the join itself uses the ticket (net ads join -k), whose cache is a
# temporary file that is removed. A join that fails before the machine account exists puts back the files it changed
# (smb.conf, krb5.conf, nsswitch.conf); the first versions are kept as *.wex-pre-join and put back at "leave".
# Needs, on Debian: samba, winbind, libnss-winbind, libpam-winbind, krb5-user (and dnsutils for the DNS check), and DNS
# that finds the domain's controllers (the machine's resolver pointed at the domain's DNS).
set -u

ETC=${WEX_JOIN_ETC:-/etc}
HERE=$(dirname -- "$(readlink -f -- "$0")")
STATE_TOOL=${WEX_DOMAIN_STATE:-/usr/local/bin/wex-domain-state.sh}
[ -e "$STATE_TOOL" ] || STATE_TOOL=$HERE/wex-domain-state.sh
PACKAGES="samba winbind libnss-winbind libpam-winbind krb5-user"

die() { echo "wex-domain-join: $*" >&2; exit 1; }
note() { echo "wex-domain-join: $*"; }
[ "${WEX_JOIN_NOROOT:-0}" = 1 ] || [ "$(id -u)" -eq 0 ] || die "run as root"

usage()
{
    sed -n '/^#   wex-domain-join.sh join/,/^#   wex-domain-join.sh status/p' "$0" | sed 's/^# \{0,3\}//' >&2
    exit 2
}

# DOMAIN: a DNS name (labels of letters, digits and hyphens, at least two of them); NAME: what a Windows user name holds
valid_domain()
{
    [ "${#1}" -le 253 ] && [[ $1 =~ ^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?(\.[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?)+$ ]]
}
valid_user() { [[ $1 =~ ^[A-Za-z0-9._@-]{1,64}$ ]] && [[ $1 != -* ]]; }
valid_workgroup() { [[ $1 =~ ^[A-Za-z0-9-]{1,15}$ ]] && [[ $1 != -* ]]; }
valid_ou() { [[ $1 =~ ^[A-Za-z0-9=,._\ -]{1,255}$ ]] && [[ $1 != -* ]]; }

need_tools()
{
    local missing= t
    for t in net kinit kdestroy pam-auth-update systemctl; do command -v "$t" >/dev/null 2>&1 || missing="$missing $t"; done
    [ -z "$missing" ] || die "missing:$missing (on Debian: apt-get install $PACKAGES)"
}

# the kind of the join now: winbind, sssd, other, or nothing
join_kind() { "$STATE_TOOL" --kind 2>/dev/null; }

# one file's first version is kept once, and put back by "leave"
keep_first() { [ -e "$1" ] && [ ! -e "$1.wex-pre-join" ] && cp -p "$1" "$1.wex-pre-join"; return 0; }
restore_first() { [ -e "$1.wex-pre-join" ] && mv -f "$1.wex-pre-join" "$1"; return 0; }

# winbind on the passwd and group lines of nsswitch.conf, once; and off again
nsswitch_add() { sed -i -E '/^(passwd|group):/ { /winbind/! s/$/ winbind/ }' "$ETC/nsswitch.conf"; }
nsswitch_remove() { sed -i -E '/^(passwd|group):/ s/[[:space:]]+winbind([[:space:]]|$)/\1/g' "$ETC/nsswitch.conf"; }
nsswitch_has() { grep -Eq '^(passwd|group):.*[[:space:]]winbind' "$ETC/nsswitch.conf" 2>/dev/null; }

write_smb_conf()   # WORKGROUP REALM NETBIOS-NAME
{
    cat > "$ETC/samba/smb.conf" <<EOF
[global]
    workgroup = $1
    realm = $2
    netbios name = $3
    security = ads
    kerberos method = secrets and keytab
    idmap config * : backend = tdb
    idmap config * : range = 10000-19999
    idmap config $1 : backend = rid
    idmap config $1 : range = 20000-99999
    winbind use default domain = no
    winbind offline logon = yes
    template shell = /bin/bash
    template homedir = /home/%D/%U
EOF
}

write_krb5_conf()   # REALM
{
    cat > "$ETC/krb5.conf" <<EOF
[libdefaults]
    default_realm = $1
    dns_lookup_realm = false
    dns_lookup_kdc = true
EOF
}

# a Kerberos ticket of the administrator in a cache of its own, from the password on standard input
CCDIR=
get_ticket()   # NAME@REALM
{
    local pw
    IFS= read -r pw || die "no password on standard input"
    [ -n "$pw" ] || die "an empty password"
    CCDIR=$(mktemp -d) || die "no temporary directory"
    chmod 0700 "$CCDIR"
    export KRB5CCNAME="FILE:$CCDIR/cc"
    printf '%s\n' "$pw" | kinit "$1" >/dev/null 2>&1 || { pw=; drop_ticket; return 1; }
    pw=
}
drop_ticket() { [ -z "$CCDIR" ] || { KRB5CCNAME="FILE:$CCDIR/cc" kdestroy >/dev/null 2>&1; rm -rf "$CCDIR"; CCDIR=; }; unset KRB5CCNAME; }
trap drop_ticket EXIT

cmd_join()
{
    local domain=${1:-} user= workgroup= ou= stdin=0 realm kind netbios
    [ -n "$domain" ] || usage
    shift
    while [ $# -gt 0 ]; do
        case $1 in
            --user) user=${2:-}; shift 2 || usage ;;
            --workgroup) workgroup=${2:-}; shift 2 || usage ;;
            --ou) ou=${2:-}; shift 2 || usage ;;
            --password-stdin) stdin=1; shift ;;
            *) usage ;;
        esac
    done
    valid_domain "$domain" || die "'$domain' is not a domain name"
    valid_user "$user" || die "--user: a user name is needed (letters, digits and . _ @ -)"
    [ "$stdin" = 1 ] || die "the password is read from standard input: give --password-stdin"
    realm=$(printf '%s' "$domain" | tr 'a-z' 'A-Z')
    if [ -z "$workgroup" ]; then workgroup=$(printf '%s' "${domain%%.*}" | tr 'a-z' 'A-Z' | cut -c 1-15); fi
    # the computer's NetBIOS name is its host name, cut to 15 characters as Windows does
    netbios=$(printf '%s' "${WEX_JOIN_HOSTNAME:-$(hostname -s)}" | tr 'a-z' 'A-Z' | cut -c 1-15)
    [[ $netbios =~ ^[A-Z0-9]([A-Z0-9-]*[A-Z0-9])?$ ]] || die "the host name '$netbios' cannot be a computer name (letters, digits and hyphens)"
    [ "$netbios" != "$workgroup" ] || die "the computer name and the domain's NetBIOS name are both $netbios: give --workgroup"
    valid_workgroup "$workgroup" || die "--workgroup: the NetBIOS name is 1 to 15 letters, digits and hyphens"
    [ -z "$ou" ] || valid_ou "$ou" || die "--ou: not a distinguished name"
    need_tools
    kind=$(join_kind)
    case $kind in
        "") ;;
        winbind) die "this machine is joined already (status shows to what): leave first" ;;
        *) die "this machine is joined with $kind (realm); Winbind is the supported join: leave that one first (realm leave)" ;;
    esac
    # the domain's controllers are found in DNS, or nothing below can work
    if command -v host >/dev/null 2>&1; then
        host -t SRV "_ldap._tcp.$domain" >/dev/null 2>&1 || die "DNS does not know the controllers of $domain (_ldap._tcp.$domain): is the resolver the domain's?"
    else
        getent hosts "$domain" >/dev/null 2>&1 || die "$domain does not resolve: is the resolver the domain's?"
    fi

    mkdir -p "$ETC/samba"
    keep_first "$ETC/samba/smb.conf"; keep_first "$ETC/krb5.conf"; keep_first "$ETC/nsswitch.conf"
    write_smb_conf "$workgroup" "$realm" "$netbios" || die "cannot write smb.conf"
    write_krb5_conf "$realm" || die "cannot write krb5.conf"
    if ! get_ticket "$user@$realm"; then
        undo_files
        die "the controller did not accept $user@$realm (a wrong password, or the clock or DNS)"
    fi
    if ! net ads join -k ${ou:+createcomputer="$ou"} >/dev/null 2>&1; then
        drop_ticket; undo_files
        die "net ads join failed (the account may not join computers, or $ou does not exist)"
    fi
    drop_ticket
    # the machine account exists: from here the setup is finished, not undone
    nsswitch_add
    DEBIAN_FRONTEND=noninteractive pam-auth-update --enable winbind mkhomedir >/dev/null 2>&1 || note "warning: pam-auth-update did not enable winbind and mkhomedir"
    install -d -m 0755 "/home/$workgroup"
    systemctl enable --now winbind >/dev/null 2>&1 || note "warning: winbind did not start"
    systemctl restart winbind >/dev/null 2>&1
    net ads testjoin >/dev/null 2>&1 && note "joined to $domain ($workgroup)" || { note "joined, but net ads testjoin does not confirm it yet"; exit 1; }
}

# put the files back as they were before this run changed them
undo_files()
{
    restore_first "$ETC/samba/smb.conf"; restore_first "$ETC/krb5.conf"; restore_first "$ETC/nsswitch.conf"
}

cmd_leave()
{
    local user= stdin=0 force=0 kind realm
    while [ $# -gt 0 ]; do
        case $1 in
            --user) user=${2:-}; shift 2 || usage ;;
            --password-stdin) stdin=1; shift ;;
            --force) force=1; shift ;;
            *) usage ;;
        esac
    done
    kind=$(join_kind)
    [ "$kind" = winbind ] || die "this machine is not joined with Winbind (kind: ${kind:-none})"
    need_tools
    if [ "$force" = 0 ]; then
        valid_user "$user" || die "--user: a user name is needed (or --force to leave on this machine only)"
        [ "$stdin" = 1 ] || die "the password is read from standard input: give --password-stdin"
        realm=$(testparm -s --parameter-name=realm 2>/dev/null | tr 'a-z' 'A-Z')
        [ -n "$realm" ] || realm=$(sed -n 's/^[[:space:]]*realm[[:space:]]*=[[:space:]]*//p' "$ETC/samba/smb.conf" | head -n 1 | tr 'a-z' 'A-Z')
        get_ticket "$user@$realm" || die "the controller did not accept $user@$realm"
        net ads leave -k >/dev/null 2>&1 || { drop_ticket; die "net ads leave failed: nothing was changed (--force leaves on this machine only)"; }
        drop_ticket
    else
        note "leaving on this machine only: the machine account stays in the domain"
    fi
    net ads keytab flush >/dev/null 2>&1
    nsswitch_remove
    DEBIAN_FRONTEND=noninteractive pam-auth-update --remove winbind >/dev/null 2>&1 || note "warning: pam-auth-update did not remove winbind"
    systemctl disable --now winbind >/dev/null 2>&1
    if [ -e "$ETC/samba/smb.conf.wex-pre-join" ]; then restore_first "$ETC/samba/smb.conf"
    else printf '[global]\n    workgroup = WORKGROUP\n' > "$ETC/samba/smb.conf"; fi
    restore_first "$ETC/krb5.conf"
    note "left the domain"
}

cmd_status()
{
    local kind domain flat
    kind=$(join_kind)
    echo "kind=${kind:-none}"
    if [ -n "$kind" ]; then
        read -r flat domain < <("$STATE_TOOL" --print 2>/dev/null)
        echo "domain=${domain:-}"
        echo "workgroup=${flat:-}"
    fi
    echo "winbind=$(systemctl is-active winbind 2>/dev/null || echo inactive)"
    if [ "$kind" = winbind ]; then
        net ads testjoin >/dev/null 2>&1 && echo "testjoin=ok" || echo "testjoin=failed"
    fi
    nsswitch_has && echo "nsswitch=winbind" || echo "nsswitch=no"
    case $kind in
        sssd|other) echo "note=joined with $kind: Winbind is the supported join; domain logon and Group Policy are not offered" ;;
    esac
}

case ${1:-} in
    join) shift; cmd_join "$@" ;;
    leave) shift; cmd_leave "$@" ;;
    status) cmd_status ;;
    *) usage ;;
esac
