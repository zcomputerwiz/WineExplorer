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
#   wex-domain-join.sh serve                                   one request on standard input (the socket unit)
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

# serve mode (a request on standard input from the socket, docs/DOMAIN_JOIN.md, "How the API reaches root") answers with
# status=N (a Win32 error code) and message=TEXT; the command line says the text and ends with status 1
SERVE=0
NOTES=
REQ_LOG=
reply()   # CODE MESSAGE: control characters become spaces (the names come from outside), the end is trimmed
{
    local m=${2//[[:cntrl:]]/ }
    m=${m%"${m##*[![:space:]]}"}
    printf 'status=%s\nmessage=%s\n' "$1" "$m"
}
# one change of the machine's authentication at a time (smb.conf, krb5.conf, nsswitch.conf, PAM): the socket service and
# the root command line wait for each other; the lock lasts until the process ends
take_lock()
{
    command -v flock >/dev/null 2>&1 || return 0
    { exec 9>"${WEX_JOIN_LOCK:-/run/wex-domain-join.lock}"; } 2>/dev/null || return 0
    flock -w 300 9 || fail 170 "another change of the domain membership is in progress"   # ERROR_BUSY
}
fail()   # CODE MESSAGE
{
    if [ "$SERVE" = 1 ]; then logger -t wex-domain-join "request $REQ_LOG: failed $1" 2>/dev/null || true
                              reply "$1" "${2//[[:cntrl:]]/ }"; exit 0; fi
    echo "wex-domain-join: $2" >&2
    exit 1
}
die() { fail 31 "$*"; }   # ERROR_GEN_FAILURE
note() { if [ "$SERVE" = 1 ]; then NOTES="$NOTES$* "; else echo "wex-domain-join: $*"; fi; }
[ "${WEX_JOIN_NOROOT:-0}" = 1 ] || [ "$(id -u)" -eq 0 ] || fail 5 "run as root"

usage()
{
    [ "$SERVE" != 1 ] || fail 87 "invalid request"
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

# the option that tells net to use the ticket: --use-kerberos=required where this Samba has it (-k is deprecated there and
# says so in every answer), else -k
krb_opt()
{
    if net --help 2>&1 | grep -q -- '--use-kerberos'; then echo '--use-kerberos=required'; else echo '-k'; fi
}

need_tools()
{
    local missing= t
    for t in net kinit kdestroy pam-auth-update systemctl; do command -v "$t" >/dev/null 2>&1 || missing="$missing $t"; done
    [ -z "$missing" ] || fail 50 "missing:$missing (on Debian: apt-get install $PACKAGES)"
}

# the kind of the join now: winbind, sssd, other, or nothing
join_kind() { "$STATE_TOOL" --kind 2>/dev/null; }

# tell Windows programs (HKLM\Software\WineExplorer\Domain): the state tool runs as session 0's account in a unit of
# its own, as it does when session 0's services start (wex-session0-services.service); the engine's own unit has a
# private /tmp, in which the Wine server of session 0 is not to be found. A failure is a note, not a failed join
refresh_state()
{
    local out=/tmp/wex-state-refresh.$$
    [ -z "${WEX_NO_STATE_REFRESH:-}" ] || return 0
    id -u wexsys >/dev/null 2>&1 && command -v systemd-run >/dev/null 2>&1 || return 0
    timeout 90 systemd-run --quiet --wait --collect --pipe -p User=wexsys -p Group=wexsys -p WorkingDirectory=/ \
        -p Environment=WINEPREFIX=/var/lib/wineexplorer/machine -p Environment=HOME=/var/lib/wineexplorer/machine \
        -p Environment=WINEDEBUG=-all -p Environment=WINEDLLOVERRIDES=mscoree,mshtml=\;winemenubuilder.exe=d \
        "$STATE_TOOL" > "$out" 2>&1 ||
        note "warning: the state for Windows programs was not updated: $(tr '\n' ' ' < "$out" | cut -c1-300)"
    rm -f "$out"
}

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
    if [ -n "${JOIN_PW+x}" ]; then pw=$JOIN_PW; JOIN_PW=; else IFS= read -r pw || fail 87 "no password on standard input"; fi
    [ -n "$pw" ] || fail 87 "an empty password"
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
    local domain=${1:-} user= workgroup= ou= stdin=0 realm kind netbios principal guessed found joinout oupath ouparts i part
    [ -n "$domain" ] || usage
    take_lock
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
    valid_domain "$domain" || fail 87 "'$domain' is not a domain name"
    valid_user "$user" || fail 87 "--user: a user name is needed (letters, digits and . _ @ -)"
    [ "$stdin" = 1 ] || fail 87 "the password is read from standard input: give --password-stdin"
    realm=$(printf '%s' "$domain" | tr 'a-z' 'A-Z')
    guessed=0
    if [ -z "$workgroup" ]; then guessed=1; workgroup=$(printf '%s' "${domain%%.*}" | tr 'a-z' 'A-Z' | cut -c 1-15); fi
    # the computer's NetBIOS name is its host name, cut to 15 characters as Windows does
    netbios=$(printf '%s' "${WEX_JOIN_HOSTNAME:-$(hostname -s)}" | tr 'a-z' 'A-Z' | cut -c 1-15)
    [[ $netbios =~ ^[A-Z0-9]([A-Z0-9-]*[A-Z0-9])?$ ]] || fail 1210 "the host name '$netbios' cannot be a computer name (letters, digits and hyphens)"
    [ "$netbios" != "$workgroup" ] || fail 1210 "the computer name and the domain's NetBIOS name are both $netbios: give --workgroup"
    valid_workgroup "$workgroup" || fail 87 "--workgroup: the NetBIOS name is 1 to 15 letters, digits and hyphens"
    [ -z "$ou" ] || valid_ou "$ou" || fail 87 "--ou: not a distinguished name"
    need_tools
    # the domain's NetBIOS name is the controller's to say (a domain "corp.example.com" may be "CORPORATE"): asked with a
    # CLDAP ping before the first label is trusted
    if [ "$guessed" = 1 ]; then
        found=$(net ads lookup -S "$domain" 2>/dev/null | sed -n 's/^Pre-Win2k Domain: *//p' | head -n 1 | tr 'a-z' 'A-Z')
        if [ -n "$found" ] && valid_workgroup "$found"; then
            workgroup=$found
            [ "$netbios" != "$workgroup" ] || fail 1210 "the computer name and the domain's NetBIOS name are both $netbios: rename the computer"
        fi
    fi
    kind=$(join_kind)
    case $kind in
        "") ;;
        winbind) fail 2691 "this machine is joined already (status shows to what): leave first" ;;
        *) fail 2691 "this machine is joined with $kind (realm); Winbind is the supported join: leave that one first (realm leave)" ;;
    esac
    # the domain's controllers are found in DNS, or nothing below can work
    if command -v host >/dev/null 2>&1; then
        host -t SRV "_ldap._tcp.$domain" >/dev/null 2>&1 || fail 1355 "DNS does not know the controllers of $domain (_ldap._tcp.$domain): is the resolver the domain's?"
    else
        getent hosts "$domain" >/dev/null 2>&1 || fail 1355 "$domain does not resolve: is the resolver the domain's?"
    fi

    mkdir -p "$ETC/samba"
    keep_first "$ETC/samba/smb.conf"; keep_first "$ETC/krb5.conf"; keep_first "$ETC/nsswitch.conf"
    write_smb_conf "$workgroup" "$realm" "$netbios" || die "cannot write smb.conf"
    write_krb5_conf "$realm" || die "cannot write krb5.conf"
    principal=$user; [[ $user == *@* ]] || principal=$user@$realm
    if ! get_ticket "$principal"; then
        undo_files
        fail 1326 "the controller did not accept $user@$realm (a wrong password, or the clock or DNS)"
    fi
    # Windows takes the OU as a whole distinguished name (netdom /OU:"OU=X,DC=corp,DC=example"); Samba wants it without
    # the domain's own DC= parts
    # and as a path, the outermost container first, without the OU=: "OU=Laptops,OU=Staff" is "Staff/Laptops"
    if [ -n "$ou" ]; then
        ou=${ou%%,[Dd][Cc]=*}
        oupath=
        IFS=, read -r -a ouparts <<<"$ou"
        for ((i = ${#ouparts[@]} - 1; i >= 0; i--)); do
            part=${ouparts[i]#[Oo][Uu]=}; part=${part#[Cc][Nn]=}
            oupath=${oupath:+$oupath/}$part
        done
        ou=$oupath
    fi
    joinout=$(net ads join "$(krb_opt)" ${ou:+createcomputer="$ou"} 2>&1) || {
        drop_ticket; undo_files
        die "net ads join failed (the account may not join computers, or ${ou:-the OU} does not exist): $(tail -n 2 <<<"$joinout" | cut -c1-200)"
    }
    drop_ticket
    # the machine account exists: from here the setup is finished, not undone
    nsswitch_add
    DEBIAN_FRONTEND=noninteractive pam-auth-update --enable winbind mkhomedir >/dev/null 2>&1 || note "warning: pam-auth-update did not enable winbind and mkhomedir"
    install -d -m 0755 "/home/$workgroup"
    systemctl enable --now winbind >/dev/null 2>&1 || note "warning: winbind did not start"
    systemctl restart winbind >/dev/null 2>&1
    if net ads testjoin >/dev/null 2>&1; then refresh_state; note "joined to $domain ($workgroup)"
    else fail 31 "joined, but net ads testjoin does not confirm it yet"; fi
}

# put the files back as they were before this run changed them
undo_files()
{
    restore_first "$ETC/samba/smb.conf"; restore_first "$ETC/krb5.conf"; restore_first "$ETC/nsswitch.conf"
}

cmd_leave()
{
    local user= stdin=0 force=0 keep= kind realm principal
    take_lock
    while [ $# -gt 0 ]; do
        case $1 in
            --user) user=${2:-}; shift 2 || usage ;;
            --password-stdin) stdin=1; shift ;;
            --force) force=1; shift ;;
            --keep-account) keep=1; shift ;;
            *) usage ;;
        esac
    done
    kind=$(join_kind)
    [ "$kind" = winbind ] || fail 2692 "this machine is not joined with Winbind (kind: ${kind:-none})"
    need_tools
    if [ "$force" = 0 ]; then
        valid_user "$user" || fail 87 "--user: a user name is needed (or --force to leave on this machine only)"
        [ "$stdin" = 1 ] || fail 87 "the password is read from standard input: give --password-stdin"
        realm=$(testparm -s --parameter-name=realm 2>/dev/null | tr 'a-z' 'A-Z')
        [ -n "$realm" ] || realm=$(sed -n 's/^[[:space:]]*realm[[:space:]]*=[[:space:]]*//p' "$ETC/samba/smb.conf" | head -n 1 | tr 'a-z' 'A-Z')
        principal=$user; [[ $user == *@* ]] || principal=$user@$realm
        get_ticket "$principal" || fail 1326 "the controller did not accept $principal"
        net ads leave "$(krb_opt)" ${keep:+--keep-account} >/dev/null 2>&1 || { drop_ticket; die "net ads leave failed: nothing was changed (--force leaves on this machine only)"; }
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
    refresh_state
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

# a name by the rules of NetValidateName (NetSetupMachine, Workgroup, Domain, NonExistentDomain, DnsMachine)
cmd_validate()   # TYPE NAME
{
    local type=$1 name=$2 flat
    case $type in
        machine) [[ $name =~ ^[A-Za-z0-9]([A-Za-z0-9-]{0,13}[A-Za-z0-9])?$ ]] && [[ ! $name =~ ^[0-9]+$ ]] || fail 1210 "'$name' is not a valid computer name (1 to 15 letters, digits and hyphens, not only digits)" ;;
        dnsmachine) [[ $name =~ ^[A-Za-z0-9]([A-Za-z0-9-]{0,61}[A-Za-z0-9])?$ ]] || fail 1210 "'$name' is not a valid DNS host name" ;;
        workgroup) valid_workgroup "$name" || fail 1212 "'$name' is not a valid workgroup name" ;;
        domain|nonexistentdomain)
            valid_domain "$name" || [[ $name =~ ^[A-Za-z0-9-]{1,15}$ ]] || fail 1212 "'$name' is not a valid domain name"
            flat=$("$STATE_TOOL" --print 2>/dev/null | awk '{ print tolower($1) }')
            if [ "$(printf '%s' "$name" | tr 'A-Z' 'a-z')" = "$flat" ] ||
               { command -v host >/dev/null 2>&1 && host -t SRV "_ldap._tcp.$name" >/dev/null 2>&1; }; then
                [ "$type" = domain ] || fail 52 "the name '$name' is a domain already"
            else
                [ "$type" = nonexistentdomain ] || fail 1355 "the domain '$name' does not exist or its controllers cannot be found in DNS"
            fi ;;
        *) fail 87 "unknown name type" ;;
    esac
}

# the secure channel of the machine account (netdom verify and reset, nltest /sc_verify and /sc_reset): verify asks Samba
# (net ads testjoin) and Winbind (the trust secret); reset changes the machine password first (net ads changetrustpw)
cmd_secure_channel()   # verify|reset
{
    local kind realm
    [ "$1" != reset ] || take_lock
    kind=$(join_kind)
    [ "$kind" = winbind ] || fail 2692 "this machine is not joined with Winbind (kind: ${kind:-none})"
    command -v net >/dev/null 2>&1 || fail 50 "missing: net (on Debian: apt-get install $PACKAGES)"
    realm=$("$STATE_TOOL" --print 2>/dev/null | awk '{ print $2 }')
    if [ "$1" = reset ]; then
        net ads changetrustpw >/dev/null 2>&1 || fail 1789 "the machine password could not be changed: the trust relationship between this workstation and the primary domain failed"
    fi
    if net ads testjoin >/dev/null 2>&1 && { ! command -v wbinfo >/dev/null 2>&1 || wbinfo --check-secret >/dev/null 2>&1; }; then
        # as NETDOM prints it: the channel, then the controller it is with; "||" ends a line of the message
        dc=$(net ads info 2>/dev/null | sed -n 's/^LDAP server name: *//p' | head -n 1)
        host=$(printf '%s' "${WEX_JOIN_HOSTNAME:-$(hostname -s)}" | tr 'a-z' 'A-Z')
        note "The secure channel from $host to the domain $(printf '%s' "$realm" | tr 'a-z' 'A-Z') has been $([ "$1" = reset ] && echo reset || echo verified).${dc:+||The connection is with the machine $dc.}"
    else
        fail 1789 "the trust relationship between this workstation and the primary domain failed"
    fi
}

# the domain controllers that DNS lists for the joined domain
cmd_dclist()
{
    local kind realm list
    kind=$(join_kind)
    [ "$kind" = winbind ] || fail 2692 "this machine is not joined with Winbind (kind: ${kind:-none})"
    realm=$("$STATE_TOOL" --print 2>/dev/null | awk '{ print $2 }')
    command -v host >/dev/null 2>&1 || fail 50 "missing: host (on Debian: apt-get install dnsutils)"
    list=$(host -t SRV "_ldap._tcp.dc._msdcs.$realm" 2>/dev/null | awk '/has SRV record/ { sub(/\.$/, "", $NF); print $NF }' | sort -u | tr '\n' ' ')
    [ -n "$list" ] || fail 1355 "no domain controller of $realm is listed in DNS"
    NOTES="$list"
}

# nltest /dsgetdc: what the controller says about itself (a CLDAP ping, net ads lookup); the answer is lines of
# name=value separated by "||" (nltest prints them as DsGetDcName's fields; the flags are DS_..._FLAG as a hex number)
cmd_dsgetdc()
{
    local kind realm out dc addr guid dom dnsdom forest dcsite oursite flags=0 line bit
    kind=$(join_kind)
    [ "$kind" = winbind ] || fail 2692 "this machine is not joined with Winbind (kind: ${kind:-none})"
    command -v net >/dev/null 2>&1 || fail 50 "missing: net (on Debian: apt-get install $PACKAGES)"
    realm=$("$STATE_TOOL" --print 2>/dev/null | awk '{ print $2 }')
    out=$(net ads lookup -S "$realm" 2>/dev/null) || fail 1355 "no domain controller of $realm answered"
    dc=$(sed -n 's/^Domain Controller: *//p' <<<"$out" | head -n 1)
    addr=$(sed -n 's/^Information for Domain Controller: *//p' <<<"$out" | head -n 1)
    guid=$(sed -n 's/^GUID: *//p' <<<"$out" | head -n 1)
    dom=$(sed -n 's/^Pre-Win2k Domain: *//p' <<<"$out" | head -n 1)
    forest=$(sed -n 's/^Forest: *//p' <<<"$out" | head -n 1)
    dnsdom=$(sed -n 's/^Domain: *//p' <<<"$out" | head -n 1)
    dcsite=$(sed -n 's/^Server Site Name: *//p' <<<"$out" | head -n 1)
    oursite=$(sed -n 's/^Client Site Name: *//p' <<<"$out" | head -n 1)
    [ -n "$dc" ] || fail 1355 "no domain controller of $realm answered"
    while IFS= read -r line; do
        case $line in
            *"Is a PDC:"*yes) bit=0x1 ;;
            *"Is a GC of the forest:"*yes) bit=0x4 ;;
            *"Is an LDAP server:"*yes) bit=0x8 ;;
            *"Supports DS:"*yes) bit=0x10 ;;
            *"Is running a KDC:"*yes) bit=0x20 ;;
            *"Is running time services:"*yes) bit=0x40 ;;
            *"Is the closest DC:"*yes) bit=0x80 ;;
            *"Is writable:"*yes) bit=0x100 ;;
            *"Has a hardware clock:"*yes) bit=0x200 ;;
            *"Is a non-domain NC serviced by LDAP server:"*yes) bit=0x400 ;;
            *"Is NT6 DC that has some secrets:"*yes) bit=0x800 ;;
            *"Is NT6 DC that has all secrets:"*yes) bit=0x1000 ;;
            *"Runs Active Directory Web Services:"*yes) bit=0x2000 ;;
            *"Runs on Windows 2012 or later:"*yes) bit=0x4000 ;;
            *) bit=0 ;;
        esac
        flags=$(( flags | bit ))
    done <<<"$out"
    # a controller found by its DNS name answers with the DNS names
    flags=$(( flags | 0xe0000000 ))
    note "dc=$dc||addr=$addr||guid=$guid||domain=$dom||dnsdomain=$dnsdom||forest=$forest||dcsite=$dcsite||oursite=${oursite:-$dcsite}||flags=$(printf '%x' "$flags")"
}
# nltest /sc_query: the controller the secure channel is with, and whether the channel works
cmd_sc_query()
{
    local kind dc status=0
    kind=$(join_kind)
    [ "$kind" = winbind ] || fail 2692 "this machine is not joined with Winbind (kind: ${kind:-none})"
    command -v net >/dev/null 2>&1 || fail 50 "missing: net (on Debian: apt-get install $PACKAGES)"
    dc=$(net ads info 2>/dev/null | sed -n 's/^LDAP server name: *//p' | head -n 1)
    net ads testjoin >/dev/null 2>&1 || status=1722
    note "dc=${dc:-}||status=$status"
}

# one request on standard input: lines of key=value ended by an empty line; the answer is status= and message=
cmd_serve()
{
    local line key val n=0 op= domain= user= password= ou= workgroup= name= nametype= force= flags= kind rc
    SERVE=1
    while :; do
        # a client that connects and says nothing must not keep the service for ever
        rc=0; IFS= read -r -t 30 line || rc=$?
        [ "$rc" -le 128 ] || fail 1460 "no request arrived in 30 seconds"   # ERROR_TIMEOUT
        [ -n "$line" ] || break
        n=$((n + 1)); [ "$n" -le 32 ] || fail 87 "too many lines"
        [[ $line == *=* ]] || fail 87 "a line without a ="
        key=${line%%=*}; val=${line#*=}
        [ "${#val}" -le 512 ] || fail 87 "a value is too long"
        [[ $val != *[[:cntrl:]]* ]] || fail 87 "a value holds a control character"
        case $key in
            op|domain|user|password|ou|workgroup|name|nametype|force) printf -v "$key" '%s' "$val" ;;
            flags) flags=$val ;;   # NETSETUP_ACCT_DELETE (4) of a leave: without it the account is kept
            *) fail 87 "unknown field" ;;
        esac
    done
    REQ_LOG="op=${op:-?} domain=${domain:-} user=${user:-}"
    logger -t wex-domain-join "request $REQ_LOG" 2>/dev/null || true
    case $op in
        join) JOIN_PW=$password; cmd_join "$domain" --user "$user" --password-stdin ${workgroup:+--workgroup "$workgroup"} ${ou:+--ou "$ou"} ;;
        leave) if [ "$force" = 1 ]; then cmd_leave --force
               else
                   JOIN_PW=$password
                   # a leave without ACCT_DELETE keeps the machine account (Windows disables it; Samba leaves it as it is)
                   if [ -n "$flags" ] && [[ $flags =~ ^[0-9]+$ ]] && [ $(( 10#$flags & 4 )) = 0 ]; then
                       cmd_leave --user "$user" --password-stdin --keep-account
                   else cmd_leave --user "$user" --password-stdin; fi
               fi ;;
        validate) cmd_validate "$nametype" "$name" ;;
        verify) cmd_secure_channel verify ;;
        reset) cmd_secure_channel reset ;;
        dclist) cmd_dclist ;;
        dsgetdc) cmd_dsgetdc ;;
        scquery) cmd_sc_query ;;
        status) kind=$(join_kind); NOTES="kind=${kind:-none}" ;;
        *) fail 87 "unknown operation" ;;
    esac
    logger -t wex-domain-join "request $REQ_LOG: done" 2>/dev/null || true
    reply 0 "$NOTES"
    exit 0
}

case ${1:-} in
    serve) cmd_serve ;;
    verify) cmd_secure_channel verify ;;
    reset) cmd_secure_channel reset ;;
    dclist) cmd_dclist; echo "$NOTES" ;;
    dsgetdc) cmd_dsgetdc; echo "$NOTES" ;;
    scquery) cmd_sc_query; echo "$NOTES" ;;
    join) shift; cmd_join "$@" ;;
    leave) shift; cmd_leave "$@" ;;
    status) cmd_status ;;
    *) usage ;;
esac
