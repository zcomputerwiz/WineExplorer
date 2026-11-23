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
#                                                              --user-domain NAME: the user's own domain (DOMAIN\user);
#                                                              a user@suffix name is a user principal name
#   wex-domain-join.sh leave --user NAME --password-stdin      the machine account is removed from the domain
#   wex-domain-join.sh leave --force                           only this machine's side (the controller is out of
#                                                              reach): the machine account stays in the domain
#   wex-domain-join.sh rename NEWNAME --user NAME --password-stdin
#                                                              the machine account takes the computer's new name
#   wex-domain-join.sh status                                  what the machine is joined to and what is set up
#   wex-domain-join.sh serve                                   one request on standard input (the socket unit)
#   wex-domain-join.sh serve-query                             one question on standard input (the users' socket unit):
#                                                              validate, dsgetdc, dclist, scquery and status only
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
RENAME_PACKAGES="ldap-utils libsasl2-modules-gssapi-mit"

# serve mode (a request on standard input from the socket, docs/DOMAIN_JOIN.md, "How the API reaches root") answers with
# status=N (a Win32 error code) and message=TEXT; the command line says the text and ends with status 1
SERVE=0
NOTES=
REQ_LOG=
# the answer's own descriptors: a signal's trap (a stopped rename) may run while a command's output is sent to
# /dev/null (a function called with >/dev/null redirects the shell itself), and its answer must still reach the caller
exec 3>&1 4>&2
reply()   # CODE MESSAGE: control characters become spaces (the names come from outside), the end is trimmed
{
    local m=${2//[[:cntrl:]]/ }
    m=${m%"${m##*[![:space:]]}"}
    printf 'status=%s\nmessage=%s\n' "$1" "$m" >&3
}
# one change of the machine's authentication at a time (smb.conf, krb5.conf, nsswitch.conf, PAM): the socket service and
# the root command line wait for each other; the lock lasts until the process ends. Without the lock nothing is changed.
# A request from the socket waits a few seconds only and is then told ERROR_BUSY: its caller's wait
# (WEX_JOIN_WAIT_SECONDS in wexjoin.h) must cover the operation, not another one's queue. The command line waits longer.
take_lock()
{
    local wait=300
    [ "$SERVE" != 1 ] || wait=${WEX_JOIN_LOCK_WAIT:-5}
    command -v flock >/dev/null 2>&1 || fail 50 "missing: flock (on Debian: util-linux)"
    { exec 9>"${WEX_JOIN_LOCK:-/run/wex-domain-join.lock}"; } 2>/dev/null || die "the lock file cannot be opened"
    flock -w "$wait" 9 || fail 170 "another change of the domain membership is in progress"   # ERROR_BUSY
}
# Once the host's configuration has changed (STATE_DIRTY=1), Windows programs are told on every way out, a failure
# included: a join that testjoin does not confirm yet is still a join
STATE_DIRTY=0
fail()   # CODE MESSAGE
{
    [ "$STATE_DIRTY" != 1 ] || { STATE_DIRTY=0; refresh_state; }
    if [ "$SERVE" = 1 ]; then logger -t wex-domain-join "request $REQ_LOG: failed $1" 2>/dev/null || true
                              reply "$1" "${2//[[:cntrl:]]/ }"; exit 0; fi
    echo "wex-domain-join: $2" >&4
    exit 1
}
die() { fail 31 "$*"; }   # ERROR_GEN_FAILURE
note() { if [ "$SERVE" = 1 ]; then NOTES="$NOTES$* "; else echo "wex-domain-join: $*"; fi; }
# the questions (serve-query) need no root: their unit runs them as a user of its own (wex-domain-query@.service)
[ "${WEX_JOIN_NOROOT:-0}" = 1 ] || [ "${1:-}" = serve-query ] || [ "$(id -u)" -eq 0 ] || fail 5 "run as root"

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
# a computer (NetBIOS) name, the one rule of the join, the rename and NetValidateName: 1 to 15 letters, digits and
# hyphens, not a hyphen at either end, not only digits (Windows refuses those)
valid_computer_name() { [[ $1 =~ ^[A-Za-z0-9]([A-Za-z0-9-]{0,13}[A-Za-z0-9])?$ ]] && [[ ! $1 =~ ^[0-9]+$ ]]; }

# what the joined machine is: the operations below need a Winbind join
require_winbind()
{
    local kind
    kind=$(join_kind)
    [ "$kind" = winbind ] || fail 2692 "this machine is not joined with Winbind (kind: ${kind:-none})"
}
# the joined domain's realm (upper case) and the controller Samba talks to: the state tool's answer is the one source
join_realm() { "$STATE_TOOL" --print 2>/dev/null | awk '{ print toupper($2) }'; }
dc_name() { net ads info </dev/null 2>/dev/null | sed -n 's/^LDAP server name: *//p' | head -n 1 | tr -d '|[:cntrl:]'; }

# the Kerberos principal of the administrator: a user principal name (user@suffix) is an enterprise name, which the
# controller resolves whatever its case or suffix (kinit -E); a plain name is the user in the realm. A user of another
# domain (DOMAIN\user from Windows, the "userdomain" field) is refused unless the domain is the realm's: the engine
# cannot find the realm of a NetBIOS name, and taking the user as one of the joined realm would be a different account.
PRINCIPAL_OPT=
principal_of()   # USER REALM [USERDOMAIN [FLAT]]: sets PRINCIPAL and PRINCIPAL_OPT, or fails
{
    local user=$1 realm=$2 udom=${3:-} flat=${4:-}
    PRINCIPAL_OPT=
    if [[ $user == *@* ]]; then PRINCIPAL=$user; PRINCIPAL_OPT=-E; return 0; fi
    if [ -n "$udom" ]; then
        udom=$(printf '%s' "$udom" | tr 'a-z' 'A-Z')
        if [ "$udom" != "$realm" ] && [ "$udom" != "${flat^^}" ] && [ "$udom" != "${realm%%.*}" ]; then
            valid_domain "$udom" || fail 1355 "the account's domain $udom is not $realm: give the user as user@dns.domain.name"
            realm=$udom
        fi
    fi
    PRINCIPAL=$user@$realm
}

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
    local out
    [ -z "${WEX_NO_STATE_REFRESH:-}" ] || return 0
    id -u wexsys >/dev/null 2>&1 && command -v systemd-run >/dev/null 2>&1 || return 0
    out=$(mktemp) || { note "warning: the state for Windows programs was not updated (no temporary file)"; return 0; }
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

# a file replaced as a whole by standard input: a new file in the same directory, renamed over the old one, so that
# a reader (Samba, Kerberos, the state tool, Group Policy) never sees half of it; the mode is the old file's, or 0644
replace_file()   # FILE
{
    local tmp
    tmp=$(mktemp "$1.XXXXXX") || return 1
    cat > "$tmp" || { rm -f "$tmp"; return 1; }
    chmod --reference="$1" "$tmp" 2>/dev/null || chmod 0644 "$tmp" || { rm -f "$tmp"; return 1; }
    mv -f "$tmp" "$1" || { rm -f "$tmp"; return 1; }
}

# winbind on the passwd and group lines of nsswitch.conf, once; and off again (sed -i replaces the file by a rename).
# What the leave takes out is what the join put in, so the join's first copy is not needed after a leave
nsswitch_add() { sed -i -E '/^(passwd|group):/ { /winbind/! s/$/ winbind/ }' "$ETC/nsswitch.conf"; }
nsswitch_remove() { sed -i -E '/^(passwd|group):/ s/[[:space:]]+winbind([[:space:]]|$)/\1/g' "$ETC/nsswitch.conf"; }
nsswitch_has() { grep -Eq '^(passwd|group):.*[[:space:]]winbind' "$ETC/nsswitch.conf" 2>/dev/null; }

# the PAM profiles the join turned on, so that the leave turns off those and no others (mkhomedir may have been on
# before the join, for local users)
ADDED=${WEX_JOIN_ADDED:-$ETC/wineexplorer/domain-join-added}
mkhomedir_on() { grep -qs pam_mkhomedir "$ETC/pam.d/common-session"; }

write_smb_conf()   # WORKGROUP REALM NETBIOS-NAME
{
    replace_file "$ETC/samba/smb.conf" <<EOF
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
    replace_file "$ETC/krb5.conf" <<EOF
[libdefaults]
    default_realm = $1
    dns_lookup_realm = false
    dns_lookup_kdc = true
EOF
}

# a Kerberos ticket of the administrator in a cache of its own, from the password on standard input
CCDIR=
get_ticket()   # NAME@REALM (with PRINCIPAL_OPT from principal_of)
{
    local pw
    if [ -n "${JOIN_PW+x}" ]; then pw=$JOIN_PW; JOIN_PW=; else IFS= read -r pw || [ -n "$pw" ] || fail 87 "no password on standard input"; fi
    [ -n "$pw" ] || fail 87 "an empty password"
    CCDIR=$(mktemp -d) || die "no temporary directory"
    chmod 0700 "$CCDIR"
    export KRB5CCNAME="FILE:$CCDIR/cc"
    printf '%s\n' "$pw" | kinit ${PRINCIPAL_OPT:+"$PRINCIPAL_OPT"} "$1" >/dev/null 2>&1 || { pw=; drop_ticket; return 1; }
    pw=
}
drop_ticket() { [ -z "$CCDIR" ] || { KRB5CCNAME="FILE:$CCDIR/cc" kdestroy >/dev/null 2>&1; rm -rf "$CCDIR"; CCDIR=; }; unset KRB5CCNAME; }
trap drop_ticket EXIT

cmd_join()
{
    local domain=${1:-} user= udom= workgroup= ou= stdin=0 realm kind netbios guessed found joinout oupath ouparts i part osver
    local mkhome
    [ -n "$domain" ] || usage
    take_lock
    shift
    while [ $# -gt 0 ]; do
        case $1 in
            --user) user=${2:-}; shift 2 || usage ;;
            --user-domain) udom=${2:-}; shift 2 || usage ;;
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
    valid_computer_name "$netbios" || fail 1210 "the host name '$netbios' cannot be a computer name (letters, digits and hyphens, not only digits)"
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
    principal_of "$user" "$realm" "$udom" "$workgroup"

    mkdir -p "$ETC/samba"
    keep_first "$ETC/samba/smb.conf"; keep_first "$ETC/krb5.conf"; keep_first "$ETC/nsswitch.conf"
    write_smb_conf "$workgroup" "$realm" "$netbios" || die "cannot write smb.conf"
    write_krb5_conf "$realm" || die "cannot write krb5.conf"
    if ! get_ticket "$PRINCIPAL"; then
        undo_files
        fail 1326 "the controller did not accept $PRINCIPAL (a wrong password, or the clock or DNS)"
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
    # the operating system the account tells the directory (AD Users and Computers shows it): this system's own name and version
    osver=$(. "${WEX_OS_RELEASE:-/etc/os-release}" 2>/dev/null; printf '%s' "${VERSION_ID:-}")
    joinout=$(net ads join "$(krb_opt)" ${ou:+createcomputer="$ou"} "osName=WineExplorer" "osVer=${osver:-1}" 2>&1) || {
        drop_ticket; undo_files
        die "net ads join failed (the account may not join computers, or ${ou:-the OU} does not exist): $(tail -n 2 <<<"$joinout" | cut -c1-200)"
    }
    drop_ticket
    # the machine account exists: from here the setup is finished, not undone, and Windows programs are told
    STATE_DIRTY=1
    nsswitch_add || note "warning: nsswitch.conf was not changed: domain users are not known to the system"
    # the profiles the join turns on are written down, for the leave
    mkhome=mkhomedir; ! mkhomedir_on || mkhome=
    mkdir -p "$(dirname -- "$ADDED")" && printf 'winbind%s\n' "${mkhome:+ $mkhome}" > "$ADDED" ||
        note "warning: $ADDED was not written: a leave will keep mkhomedir"
    DEBIAN_FRONTEND=noninteractive pam-auth-update --enable winbind mkhomedir >/dev/null 2>&1 || note "warning: pam-auth-update did not enable winbind and mkhomedir"
    install -d -m 0755 "/home/$workgroup"
    systemctl enable --now winbind >/dev/null 2>&1 || note "warning: winbind did not start"
    systemctl restart winbind >/dev/null 2>&1
    net ads testjoin >/dev/null 2>&1 || fail 31 "joined, but net ads testjoin does not confirm it yet"
    STATE_DIRTY=0; refresh_state
    note "joined to $domain ($workgroup)"
}

# The machine's keys in the system keytab ("kerberos method = secrets and keytab" keeps them in /etc/krb5.keytab) are
# no use after a leave. "net ads keytab flush" cannot take them out then: it needs the machine account, which the leave
# has deleted, and it takes the names off the account in the domain, which a leave on this machine only must not do.
# So they go here, on this machine: the file is removed when every key in it is one the join made (NAME$,
# host/NAME[.dns], restrictedkrbhost/NAME[.dns]); a key of anything else keeps the file, and the caller warns.
clear_keytab()   # NETBIOSNAME
{
    local kt=$ETC/krb5.keytab name=$1 list p inst
    [ -e "$kt" ] || return 0
    command -v klist >/dev/null 2>&1 || return 1
    list=$(klist -k "$kt" 2>/dev/null) || return 1
    while read -r p; do
        p=${p%@*}; p=${p^^}
        case $p in
            "$name\$") ;;
            HOST/*|RESTRICTEDKRBHOST/*) inst=${p#*/}; [ "${inst%%.*}" = "$name" ] || return 1 ;;
            *) return 1 ;;
        esac
    done < <(awk 'f { print $2 } /^-+ -+/ { f = 1 }' <<<"$list")
    rm -f "$kt"
}

# put the files back as they were before this run changed them
undo_files()
{
    restore_first "$ETC/samba/smb.conf"; restore_first "$ETC/krb5.conf"; restore_first "$ETC/nsswitch.conf"
}

cmd_leave()
{
    local user= udom= stdin=0 force=0 keep= realm flat profiles nb
    take_lock
    while [ $# -gt 0 ]; do
        case $1 in
            --user) user=${2:-}; shift 2 || usage ;;
            --user-domain) udom=${2:-}; shift 2 || usage ;;
            --password-stdin) stdin=1; shift ;;
            --force) force=1; shift ;;
            --keep-account) keep=1; shift ;;
            *) usage ;;
        esac
    done
    require_winbind
    need_tools
    if [ "$force" = 0 ]; then
        valid_user "$user" || fail 87 "--user: a user name is needed (or --force to leave on this machine only)"
        [ "$stdin" = 1 ] || fail 87 "the password is read from standard input: give --password-stdin"
        read -r flat realm < <("$STATE_TOOL" --print 2>/dev/null)
        realm=${realm^^}
        [ -n "$realm" ] || fail 2692 "the realm of the join is not known"
        principal_of "$user" "$realm" "$udom" "$flat"
        get_ticket "$PRINCIPAL" || fail 1326 "the controller did not accept $PRINCIPAL"
        net ads leave "$(krb_opt)" ${keep:+--keep-account} >/dev/null 2>&1 || { drop_ticket; die "net ads leave failed: nothing was changed (--force leaves on this machine only)"; }
        drop_ticket
    else
        note "leaving on this machine only: the machine account stays in the domain"
    fi
    # from here the machine is leaving: what the join set up is taken out, and Windows programs are told on every way out
    STATE_DIRTY=1
    nb=$(sed -n 's/^[[:space:]]*netbios name[[:space:]]*=[[:space:]]*//p' "$ETC/samba/smb.conf" 2>/dev/null | head -n 1 | tr -d ' \t' | tr 'a-z' 'A-Z')
    [ -n "$nb" ] || nb=$(printf '%s' "${WEX_JOIN_HOSTNAME:-$(hostname -s)}" | tr 'a-z' 'A-Z' | cut -c 1-15)
    clear_keytab "$nb" || note "warning: $ETC/krb5.keytab stays: it holds keys other than this machine's, or klist cannot read it"
    nsswitch_remove || note "warning: nsswitch.conf still names winbind"
    rm -f "$ETC/nsswitch.conf.wex-pre-join"
    # the profiles the join turned on (winbind, and mkhomedir when it was off before); a join of an older engine
    # wrote nothing down: winbind only, as it did
    profiles=winbind
    [ ! -s "$ADDED" ] || profiles=$(tr -cd 'a-z \n' < "$ADDED")
    # shellcheck disable=SC2086 # the profile names are words
    DEBIAN_FRONTEND=noninteractive pam-auth-update --remove $profiles >/dev/null 2>&1 || note "warning: pam-auth-update did not remove $profiles"
    rm -f "$ADDED"
    systemctl disable --now winbind >/dev/null 2>&1
    if [ -e "$ETC/samba/smb.conf.wex-pre-join" ]; then restore_first "$ETC/samba/smb.conf"
    else printf '[global]\n    workgroup = WORKGROUP\n' | replace_file "$ETC/samba/smb.conf" || note "warning: smb.conf was not reset"; fi
    restore_first "$ETC/krb5.conf"
    STATE_DIRTY=0; refresh_state
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
        machine) valid_computer_name "$name" || fail 1210 "'$name' is not a valid computer name (1 to 15 letters, digits and hyphens, not only digits)" ;;
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
    local realm dc host
    [ "$1" != reset ] || take_lock
    require_winbind
    command -v net >/dev/null 2>&1 || fail 50 "missing: net (on Debian: apt-get install $PACKAGES)"
    realm=$(join_realm)
    if [ "$1" = reset ]; then
        net ads changetrustpw >/dev/null 2>&1 || fail 1789 "the machine password could not be changed: the trust relationship between this workstation and the primary domain failed"
    fi
    if net ads testjoin >/dev/null 2>&1 && { ! command -v wbinfo >/dev/null 2>&1 || wbinfo --check-secret >/dev/null 2>&1; }; then
        # as NETDOM prints it: the channel, then the controller it is with; "||" ends a line of the message
        dc=$(dc_name)
        host=$(printf '%s' "${WEX_JOIN_HOSTNAME:-$(hostname -s)}" | tr 'a-z' 'A-Z')
        note "The secure channel from $host to the domain $realm has been $([ "$1" = reset ] && echo reset || echo verified).${dc:+||The connection is with the machine $dc.}"
    else
        fail 1789 "the trust relationship between this workstation and the primary domain failed"
    fi
}

# the domain controllers that DNS lists for the joined domain
cmd_dclist()
{
    local realm list
    require_winbind
    realm=$(join_realm)
    command -v host >/dev/null 2>&1 || fail 50 "missing: host (on Debian: apt-get install dnsutils)"
    list=$(host -t SRV "_ldap._tcp.dc._msdcs.$realm" 2>/dev/null | awk '/has SRV record/ { sub(/\.$/, "", $NF); print $NF }' | sort -u | tr '\n' ' ')
    [ -n "$list" ] || fail 1355 "no domain controller of $realm is listed in DNS"
    NOTES="$list"
}

# nltest /dsgetdc: what the controller says about itself (a CLDAP ping, net ads lookup); the answer is lines of
# name=value separated by "||" (nltest prints them as DsGetDcName's fields; the flags are DS_..._FLAG as a hex number)
cmd_dsgetdc()
{
    local realm out dc addr guid dom dnsdom forest dcsite oursite flags=0 line bit
    require_winbind
    command -v net >/dev/null 2>&1 || fail 50 "missing: net (on Debian: apt-get install $PACKAGES)"
    realm=$(join_realm)
    out=$(net ads lookup -S "$realm" 2>/dev/null) || fail 1355 "no domain controller of $realm answered"
    dc=$(sed -n 's/^Domain Controller: *//p' <<<"$out" | head -n 1)
    addr=$(sed -n 's/^Information for Domain Controller: *//p' <<<"$out" | head -n 1)
    guid=$(sed -n 's/^GUID: *//p' <<<"$out" | head -n 1)
    dom=$(sed -n 's/^Pre-Win2k Domain: *//p' <<<"$out" | head -n 1)
    forest=$(sed -n 's/^Forest: *//p' <<<"$out" | head -n 1)
    dnsdom=$(sed -n 's/^Domain: *//p' <<<"$out" | head -n 1)
    dcsite=$(sed -n 's/^Server Site Name: *//p' <<<"$out" | head -n 1)
    oursite=$(sed -n 's/^Client Site Name: *//p' <<<"$out" | head -n 1)
    # the controller's own words go into a "||"-separated answer: no separator and no control character may pass
    dc=$(tr -d '|[:cntrl:]' <<<"$dc"); addr=$(tr -d '|[:cntrl:]' <<<"$addr"); guid=$(tr -d '|[:cntrl:]' <<<"$guid")
    dom=$(tr -d '|[:cntrl:]' <<<"$dom"); forest=$(tr -d '|[:cntrl:]' <<<"$forest"); dnsdom=$(tr -d '|[:cntrl:]' <<<"$dnsdom")
    dcsite=$(tr -d '|[:cntrl:]' <<<"$dcsite"); oursite=$(tr -d '|[:cntrl:]' <<<"$oursite")
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
# nltest /sc_query: the controller the secure channel is with, and whether the channel works. The channel is Winbind's
# NETLOGON connection with the machine account, which wbinfo --ping-dc tries and any user may ask (testjoin would read
# the machine secret, root's file): so the question needs no root, as on Windows
cmd_sc_query()
{
    local dc status=0
    require_winbind
    command -v net >/dev/null 2>&1 && command -v wbinfo >/dev/null 2>&1 || fail 50 "missing: net or wbinfo (on Debian: apt-get install $PACKAGES)"
    dc=$(dc_name)   # the controller's own words: dc_name lets no separator and no control character pass
    wbinfo --ping-dc >/dev/null 2>&1 || status=1722
    note "dc=${dc:-}||status=$status"
}

# Renaming the computer in the domain (NetRenameMachineInDomain, netdom renamecomputer, the Change dialog): the account is
# renamed over LDAP with the administrator's ticket, as Windows does (the object, sAMAccountName, dNSHostName, the service
# principal names), then smb.conf takes the new NetBIOS name, the machine password is changed, the keytab is made again and
# Winbind restarts. The caller has set the host name already. A failure puts the account and smb.conf back as they were.
# The names come from the request: letters, digits and hyphens only (valid_workgroup), so they are safe in an LDIF.
ldap_run()   # LDIF on standard input
{
    LDAPCONF="$LDAPCONF_FILE" ldapmodify -Y GSSAPI -H "ldap://$RN_DC" -Q >/dev/null 2>&1
}
ldap_find()   # NAME: the distinguished name of the computer account
{
    LDAPCONF="$LDAPCONF_FILE" ldapsearch -Y GSSAPI -H "ldap://$RN_DC" -Q -LLL -b "$RN_BASE" "(sAMAccountName=$1\$)" dn 2>/dev/null |
        sed -n 's/^dn: //p' | head -n 1
}
# the attributes that carry the computer's name, set for NAME in the object that has the DN
ldap_set_names()   # DN NAME
{
    ldap_run <<LDIF
dn: $1
changetype: modify
replace: sAMAccountName
sAMAccountName: $2\$
-
replace: dNSHostName
dNSHostName: $(printf '%s' "$2" | tr 'A-Z' 'a-z').$RN_DNS
LDIF
}
ldap_set_spns()   # DN NAME
{
    ldap_run <<LDIF
dn: $1
changetype: modify
replace: servicePrincipalName
servicePrincipalName: HOST/$2.$RN_DNS
servicePrincipalName: RestrictedKrbHost/$2.$RN_DNS
servicePrincipalName: HOST/$2
servicePrincipalName: RestrictedKrbHost/$2
LDIF
}
ldap_rdn()   # DN NAME
{
    ldap_run <<LDIF
dn: $1
changetype: modrdn
newrdn: CN=$2
deleteoldrdn: 1
LDIF
}
set_netbios_name()   # NAME
{
    local conf=$ETC/samba/smb.conf
    if grep -q '^[[:space:]]*netbios name[[:space:]]*=' "$conf"; then
        sed "s/^\([[:space:]]*netbios name[[:space:]]*=\).*/\1 $1/" "$conf" | replace_file "$conf"
    else
        sed "/^\[global\]/a\\    netbios name = $1" "$conf" | replace_file "$conf"
    fi
}

# net with the machine account's own credentials: a ticket cache of its own, so that the administrator's ticket stays
mnet() { KRB5CCNAME="FILE:$CCDIR/machine" net "$@"; }

# the rename's progress, for the roll-back: RN_STEP 0 is nothing changed yet, 7 is done; the roll-back runs when a
# step fails and when the unit is stopped part-way (RuntimeMaxSec, systemctl stop: SIGTERM, then TimeoutStopSec to finish)
RN_STEP=0
RN_OLD= RN_NEWDN= RN_PARENT= RN_OLDHOST=
rename_undo()
{
    [ "$RN_STEP" -ge 1 ] && [ "$RN_STEP" -lt 7 ] || return 0
    # put it back: smb.conf, then the account under its old name, then the keytab
    [ ! -e "$ETC/samba/smb.conf.wex-rename" ] || replace_file "$ETC/samba/smb.conf" < "$ETC/samba/smb.conf.wex-rename"
    mnet ads keytab flush "$(krb_opt)" >/dev/null 2>&1
    ldap_rdn "$RN_NEWDN" "$RN_OLD" && ldap_set_names "CN=$RN_OLD,$RN_PARENT" "$RN_OLD" && ldap_set_spns "CN=$RN_OLD,$RN_PARENT" "$RN_OLD"
    mnet ads keytab create "$(krb_opt)" >/dev/null 2>&1
    systemctl restart winbind >/dev/null 2>&1
    [ -z "$RN_OLDHOST" ] || [ "$(hostname 2>/dev/null)" = "$RN_OLDHOST" ] || hostnamectl set-hostname "$RN_OLDHOST" >/dev/null 2>&1
    rm -f "$ETC/samba/smb.conf.wex-rename"
}
rename_stopped()
{
    local step=$RN_STEP
    trap - TERM INT
    rename_undo
    RN_STEP=0
    fail 1460 "the rename was stopped ($step of 7 steps done); it is back as $RN_OLD"   # ERROR_TIMEOUT
}

cmd_rename()
{
    local new=${1:-} user= udom= stdin=0 old realm olddn parent newdn workgroup t
    [ -n "$new" ] || usage
    take_lock
    shift
    while [ $# -gt 0 ]; do
        case $1 in
            --user) user=${2:-}; shift 2 || usage ;;
            --user-domain) udom=${2:-}; shift 2 || usage ;;
            --password-stdin) stdin=1; shift ;;
            *) usage ;;
        esac
    done
    new=$(printf '%s' "$new" | tr 'a-z' 'A-Z')
    valid_computer_name "$new" || fail 87 "the new name: 1 to 15 letters, digits and hyphens, not at the ends, not only digits"
    valid_user "$user" || fail 87 "--user: a user name is needed (letters, digits and . _ @ -)"
    [ "$stdin" = 1 ] || fail 87 "the password is read from standard input: give --password-stdin"
    require_winbind
    need_tools
    for t in ldapmodify ldapsearch; do
        command -v "$t" >/dev/null 2>&1 || fail 50 "missing: $t (on Debian: apt-get install $RENAME_PACKAGES)"
    done
    read -r workgroup realm < <("$STATE_TOOL" --print 2>/dev/null)
    realm=${realm^^}; workgroup=${workgroup^^}
    [ -n "$realm" ] || fail 2692 "the realm of the join is not known"
    [ "$new" != "$workgroup" ] || fail 1210 "the computer name cannot be the domain's NetBIOS name ($workgroup)"
    old=$(testparm -s --parameter-name='netbios name' 2>/dev/null | tr 'a-z' 'A-Z')
    [ -n "$old" ] || old=$(printf '%s' "$(hostname -s)" | tr 'a-z' 'A-Z' | cut -c 1-15)
    [ "$new" != "$old" ] || fail 87 "the computer has this name already"
    RN_OLD=$old
    RN_OLDHOST=$(hostname 2>/dev/null)
    RN_DNS=$(printf '%s' "$realm" | tr 'A-Z' 'a-z')
    RN_BASE=$(printf '%s' "$RN_DNS" | sed 's/\./,DC=/g; s/^/DC=/')
    RN_DC=$(dc_name)
    [ -n "$RN_DC" ] || RN_DC=$RN_DNS
    [[ $RN_DC =~ ^[A-Za-z0-9.-]+$ ]] || fail 1355 "no domain controller answered"
    principal_of "$user" "$realm" "$udom" "$workgroup"
    get_ticket "$PRINCIPAL" || fail 1326 "the controller did not accept $PRINCIPAL (a wrong password, or the clock or DNS)"
    LDAPCONF_FILE=$(mktemp) || die "no temporary file"
    printf 'SASL_NOCANON on\n' > "$LDAPCONF_FILE"
    trap 'rm -f "$LDAPCONF_FILE"; drop_ticket' EXIT
    olddn=$(ldap_find "$old")
    [ -n "$olddn" ] || fail 1787 "the account of $old was not found in the domain (or $user may not read it)"
    [ -z "$(ldap_find "$new")" ] || fail 52 "the domain has a computer named $new already"   # ERROR_DUP_NAME
    parent=${olddn#*,}
    newdn=CN=$new,$parent
    RN_NEWDN=$newdn RN_PARENT=$parent
    trap rename_stopped TERM INT
    ldap_rdn "$olddn" "$new" || fail 5 "$user may not rename the account of $old"
    RN_STEP=1
    STATE_DIRTY=1
    if ldap_set_names "$newdn" "$new"; then
        RN_STEP=2
        cp -p "$ETC/samba/smb.conf" "$ETC/samba/smb.conf.wex-rename" 2>/dev/null
        set_netbios_name "$new" && RN_STEP=3
    fi
    if [ "$RN_STEP" = 3 ]; then
        # the machine password is changed with the machine's own credentials (the secret stays valid after the rename)
        mnet ads changetrustpw >/dev/null 2>&1 && RN_STEP=4
    fi
    if [ "$RN_STEP" = 4 ]; then
        # the keytab is flushed first (that takes the service principal names off the account too, as a password
        # change does), then the names are set, then the keytab is made from them
        mnet ads keytab flush "$(krb_opt)" >/dev/null 2>&1
        if ldap_set_spns "$newdn" "$new"; then
            mnet ads keytab create "$(krb_opt)" >/dev/null 2>&1 && RN_STEP=5
        fi
    fi
    if [ "$RN_STEP" = 5 ]; then
        systemctl restart winbind >/dev/null 2>&1
        mnet ads testjoin >/dev/null 2>&1 && RN_STEP=6
    fi
    if [ "$RN_STEP" = 6 ]; then
        # the host name follows the account (the engine runs as root: no second authorisation is asked of the user)
        hostnamectl set-hostname "$(printf '%s' "$new" | tr 'A-Z' 'a-z')" >/dev/null 2>&1 && RN_STEP=7
    fi
    trap - TERM INT
    if [ "$RN_STEP" != 7 ]; then
        t=$RN_STEP
        rename_undo
        RN_STEP=0
        fail 31 "the account could not be renamed ($t of 7 steps done); it is back as $old"
    fi
    rm -f "$ETC/samba/smb.conf.wex-rename"
    # the host record of the new name (best effort)
    timeout 30 mnet ads dns register "$(printf '%s' "$new" | tr 'A-Z' 'a-z').$RN_DNS" >/dev/null 2>&1 || note "warning: the DNS record of $new was not updated"
    drop_ticket
    STATE_DIRTY=0; refresh_state
    note "the computer account is $new now"
}

# The questions that any user may ask, as on Windows (NetValidateName, DsGetDcName, nltest /dclist and /sc_query need no
# rights there): they read and change nothing. They come through a second socket (wex-domain-query.socket), which every
# local user can reach; its unit runs the engine as a user of its own, not root. Everything else (a join, a leave, a
# rename, verify and reset, whose I_NetLogonControl2 codes need an administrator on Windows) stays on the system group's
# socket.
QUERY_OPS="validate dsgetdc dclist scquery status"

# one request on standard input: lines of key=value ended by an empty line; the answer is status= and message=.
# With "query" (serve-query, the users' socket): only the questions, and no password field.
cmd_serve()   # [query]
{
    local line key val n=0 op= domain= user= userdomain= password= ou= workgroup= name= nametype= force= flags= kind rc
    local query=${1:-}
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
            op|domain|user|userdomain|password|ou|workgroup|name|nametype|force) printf -v "$key" '%s' "$val" ;;
            flags) flags=$val ;;   # NETSETUP_ACCT_DELETE (4) of a leave: without it the account is kept
            *) fail 87 "unknown field" ;;
        esac
    done
    REQ_LOG="op=${op:-?} domain=${domain:-} user=${userdomain:+$userdomain\\}${user:-}"
    if [ -n "$query" ]; then
        REQ_LOG="query $REQ_LOG"
        # a change through the users' socket: Windows' "access denied" for a caller who is not an administrator
        [[ " $QUERY_OPS " == *" $op "* ]] || fail 5 "changing the domain membership needs an administrator"
        [ -z "$password" ] || fail 87 "a question takes no password"
    fi
    logger -t wex-domain-join "request $REQ_LOG" 2>/dev/null || true
    # the password goes to get_ticket (JOIN_PW, which it empties) and is not kept here
    JOIN_PW=$password; password=
    [ -n "$userdomain" ] || unset userdomain
    case $op in
        join) cmd_join "$domain" --user "$user" ${userdomain+--user-domain "$userdomain"} --password-stdin ${workgroup:+--workgroup "$workgroup"} ${ou:+--ou "$ou"} ;;
        leave) if [ "$force" = 1 ]; then cmd_leave --force
               else
                   # a leave without ACCT_DELETE keeps the machine account (Windows disables it; Samba leaves it as it is)
                   if [ -n "$flags" ] && [[ $flags =~ ^[0-9]+$ ]] && [ $(( 10#$flags & 4 )) = 0 ]; then
                       cmd_leave --user "$user" ${userdomain+--user-domain "$userdomain"} --password-stdin --keep-account
                   else cmd_leave --user "$user" ${userdomain+--user-domain "$userdomain"} --password-stdin; fi
               fi ;;
        rename) cmd_rename "$name" --user "$user" ${userdomain+--user-domain "$userdomain"} --password-stdin ;;
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
    serve-query) cmd_serve query ;;
    verify) cmd_secure_channel verify ;;
    reset) cmd_secure_channel reset ;;
    dclist) cmd_dclist; echo "$NOTES" ;;
    dsgetdc) cmd_dsgetdc; echo "$NOTES" ;;
    scquery) cmd_sc_query; echo "$NOTES" ;;
    join) shift; cmd_join "$@" ;;
    leave) shift; cmd_leave "$@" ;;
    rename) shift; cmd_rename "$@" ;;
    status) cmd_status ;;
    *) usage ;;
esac
