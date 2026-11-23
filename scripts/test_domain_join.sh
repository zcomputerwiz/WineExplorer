#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# installer/scripts/wex-domain-join.sh (docs/DOMAIN_JOIN.md) with fake net, kinit, kdestroy, pam-auth-update, systemctl and
# host, and an /etc of its own in a temporary directory: what a join and a leave write and call, the refusals, that the
# password never reaches a command line, and that a join that fails before the machine account exists puts every file back.
# Needs bash only; writes only under a temporary directory.
set -euo pipefail
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
SCRIPT="$HERE/../installer/scripts/wex-domain-join.sh"
FX="$(mktemp -d)"
trap 'rm -rf -- "$FX"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
# no session 0 here: the state for Windows programs is not refreshed
export WEX_NO_STATE_REFRESH=1

mkdir -p "$FX/bin" "$FX/etc/samba" "$FX/home"
cat > "$FX/bin/net" <<EOF
#!/bin/sh
echo "net \$*" >> "$FX/calls.log"
case "\$*" in
    "--help") [ -f "$FX/newnet" ] && echo "  --use-kerberos=desired|required|off"; exit 0 ;;
    "ads join"*) exit \$(cat "$FX/join.rc" 2>/dev/null || echo 0) ;;
    "ads testjoin") exit \$(cat "$FX/testjoin.rc" 2>/dev/null || echo 0) ;;
    "ads leave"*) exit \$(cat "$FX/leave.rc" 2>/dev/null || echo 0) ;;
    "ads lookup"*) [ -f "$FX/lookup" ] && cat "$FX/lookup"; exit 0 ;;
    "ads keytab create"*) [ -e "$FX/kc.sleep" ] && { rm -f "$FX/kc.sleep"; touch "$FX/kc.started"; sleep 3; }
                          exit \$(cat "$FX/kc.rc" 2>/dev/null || echo 0) ;;
    "ads info") echo "LDAP server name: \$(cat "$FX/dcname" 2>/dev/null || echo dc1.ad.wexlab.example)"; exit 0 ;;
    "ads changetrustpw") exit \$(cat "$FX/ctp.rc" 2>/dev/null || echo 0) ;;
esac
exit 0
EOF
cat > "$FX/bin/kinit" <<EOF
#!/bin/sh
echo "kinit \$* cc=\$KRB5CCNAME" >> "$FX/calls.log"
cat > "$FX/kinit.stdin"
echo "\$KRB5CCNAME" > "$FX/kinit.cc"
exit \$(cat "$FX/kinit.rc" 2>/dev/null || echo 0)
EOF
cat > "$FX/bin/kdestroy" <<EOF
#!/bin/sh
echo "kdestroy" >> "$FX/calls.log"
EOF
cat > "$FX/bin/pam-auth-update" <<EOF
#!/bin/sh
echo "pam-auth-update \$*" >> "$FX/calls.log"
EOF
cat > "$FX/bin/systemctl" <<EOF
#!/bin/sh
echo "systemctl \$*" >> "$FX/calls.log"
[ "\$1" = is-active ] && { echo active; exit 0; }
exit 0
EOF
cat > "$FX/bin/host" <<EOF
#!/bin/sh
echo "host \$*" >> "$FX/calls.log"
case "\$*" in
    *dc._msdcs*) [ -e "$FX/dc.none" ] || printf '_ldap._tcp.dc._msdcs.ad.wexlab.example has SRV record 0 100 389 dc2.ad.wexlab.example.\n_ldap._tcp.dc._msdcs.ad.wexlab.example has SRV record 0 100 389 dc1.ad.wexlab.example.\n' ;;
esac
exit \$(cat "$FX/host.rc" 2>/dev/null || echo 0)
EOF
cat > "$FX/bin/wbinfo" <<EOF
#!/bin/sh
echo "wbinfo \$*" >> "$FX/calls.log"
exit \$(cat "$FX/wbinfo.rc" 2>/dev/null || echo 0)
EOF
cat > "$FX/bin/testparm" <<'EOF'
#!/bin/sh
case "$*" in
    *workgroup*) echo WEXLAB ;;
    *netbios*) echo WEXBOX ;;
    *) echo ad.wexlab.example ;;
esac
EOF
cat > "$FX/bin/ldapsearch" <<EOF
#!/bin/sh
# the computer accounts: WEXBOX exists, and NEWBOX only when the file "dup" says so
echo "ldapsearch \$*" >> "$FX/calls.log"
case "\$*" in
    *'sAMAccountName=WEXBOX$'*) [ -e "$FX/renamed" ] || echo "dn: CN=WEXBOX,CN=Computers,DC=ad,DC=wexlab,DC=example" ;;
    *'sAMAccountName=NEWBOX$'*) [ -e "$FX/dup" ] && echo "dn: CN=NEWBOX,CN=Computers,DC=ad,DC=wexlab,DC=example" ;;
esac
exit 0
EOF
cat > "$FX/bin/hostnamectl" <<EOF
#!/bin/sh
echo "hostnamectl \$*" >> "$FX/calls.log"
exit \$(cat "$FX/hn.rc" 2>/dev/null || echo 0)
EOF
cat > "$FX/bin/ldapmodify" <<EOF
#!/bin/sh
echo "ldapmodify \$*" >> "$FX/calls.log"
cat >> "$FX/ldif.log"
echo >> "$FX/ldif.log"
exit \$(cat "$FX/ldap.rc" 2>/dev/null || echo 0)
EOF
cat > "$FX/state" <<EOF
#!/bin/sh
# the fake wex-domain-state.sh: --kind from the file "kind", --print
case "\$1" in
    --kind) cat "$FX/kind" 2>/dev/null ;;
    --print) [ -s "$FX/kind" ] && echo "WEXLAB ad.wexlab.example" ;;
esac
exit 0
EOF
# the state refresh (refresh_state), seen only where a case turns it on (WEX_NO_STATE_REFRESH= ): session 0's account
# exists, and systemd-run is written down instead of run
cat > "$FX/bin/id" <<EOF
#!/bin/sh
[ "\$*" = "-u wexsys" ] && { echo 999; exit 0; }
exec /usr/bin/id "\$@"
EOF
cat > "$FX/bin/systemd-run" <<EOF
#!/bin/sh
echo "systemd-run" >> "$FX/calls.log"
exit 0
EOF
# the keytab: the fake file holds one principal a line, and klist -k lists them as MIT klist does
cat > "$FX/bin/klist" <<EOF
#!/bin/sh
[ "\$1" = -k ] && [ -r "\$2" ] || exit 1
echo "Keytab name: FILE:\$2"
echo "KVNO Principal"
echo "---- --------------------------------------------------------------------------"
sed 's/^/   1 /' "\$2"
EOF
chmod 0755 "$FX/bin/"* "$FX/state"

ORIG_SMB=$'[global]\n    workgroup = HOME\n'
ORIG_KRB=$'[libdefaults]\n    default_realm = OLD.EXAMPLE\n'
ORIG_NSS=$'passwd:         files systemd\ngroup:          files systemd\nshadow:         files\nhosts:          files dns\n'
reset_etc()
{
    printf '%s' "$ORIG_SMB" > "$FX/etc/samba/smb.conf"
    printf '%s' "$ORIG_KRB" > "$FX/etc/krb5.conf"
    printf '%s' "$ORIG_NSS" > "$FX/etc/nsswitch.conf"
    rm -f "$FX/etc/samba/smb.conf.wex-pre-join" "$FX/etc/krb5.conf.wex-pre-join" "$FX/etc/nsswitch.conf.wex-pre-join" \
          "$FX"/*.rc "$FX/kind" "$FX/calls.log" "$FX/kinit.stdin" "$FX/kinit.cc" "$FX/etc/krb5.keytab"
    : > "$FX/calls.log"
}
# runs the tool: the password on standard input, the fakes first in PATH
join()   # args after "join"; PW is the password ("-" for none)
{
    printf '%s\n' "${PW-s3cret-pw}" | PATH="$FX/bin:$PATH" WEX_JOIN_NOROOT=1 WEX_JOIN_ETC="$FX/etc" WEX_DOMAIN_STATE="$FX/state" \
        WEX_JOIN_HOSTNAME="${HOSTNAME_FOR_TEST:-wexbox}" bash "$SCRIPT" join "$@" 2>&1
}
leave() { printf '%s\n' "${PW-s3cret-pw}" | PATH="$FX/bin:$PATH" WEX_JOIN_NOROOT=1 WEX_JOIN_ETC="$FX/etc" WEX_DOMAIN_STATE="$FX/state" bash "$SCRIPT" leave "$@" 2>&1; }
status() { PATH="$FX/bin:$PATH" WEX_JOIN_NOROOT=1 WEX_JOIN_ETC="$FX/etc" WEX_DOMAIN_STATE="$FX/state" bash "$SCRIPT" status 2>&1; }
# a refused call: the exit status is not 0, nothing was changed, no kinit
refused()   # WHY ARGS...: runs join with the arguments and expects a refusal that mentions WHY
{
    local why=$1 out; shift
    reset_etc
    if out=$(join "$@"); then echo "$out" >&2; fail "join $* was not refused"; fi
    grep -q -- "$why" <<<"$out" || { echo "$out" >&2; fail "join $*: the refusal does not say '$why'"; }
    [ "$(cat "$FX/etc/samba/smb.conf")" = "${ORIG_SMB%$'\n'}" ] || fail "join $*: smb.conf changed"
    ! grep -q '^kinit\|^net ads join' "$FX/calls.log" || { cat "$FX/calls.log"; fail "join $*: went on to kinit or net"; }
}

# ---- a join
reset_etc
out=$(join ad.wexlab.example --user Administrator --password-stdin --ou 'OU=WexMachines,DC=ad,DC=wexlab,DC=example') || { echo "$out"; cat "$FX/calls.log"; fail 'the join failed'; }
grep -q '^realm = AD.WEXLAB.EXAMPLE$' <(sed 's/^ *//' "$FX/etc/samba/smb.conf") || { cat "$FX/etc/samba/smb.conf"; fail 'smb.conf has no realm'; }
sed 's/^ *//' "$FX/etc/samba/smb.conf" | grep -qx 'workgroup = AD' || fail 'the NetBIOS name of the domain is the first label'
sed 's/^ *//' "$FX/etc/samba/smb.conf" | grep -qx 'netbios name = WEXBOX' || { cat "$FX/etc/samba/smb.conf"; fail 'the computer name is not set'; }
sed 's/^ *//' "$FX/etc/samba/smb.conf" | grep -qx 'security = ads' || fail 'no security = ads'
sed 's/^ *//' "$FX/etc/samba/smb.conf" | grep -qx 'idmap config AD : range = 20000-99999' || fail 'no idmap range of the domain'
grep -q 'default_realm = AD.WEXLAB.EXAMPLE' "$FX/etc/krb5.conf" || fail 'krb5.conf has no default realm'
[ "$(grep -c '^passwd:.*winbind' "$FX/etc/nsswitch.conf")" = 1 ] && [ "$(grep -c '^group:.*winbind' "$FX/etc/nsswitch.conf")" = 1 ] || { cat "$FX/etc/nsswitch.conf"; fail 'nsswitch'; }
grep -q '^shadow:         files$' "$FX/etc/nsswitch.conf" || fail 'nsswitch: another line changed'
[ "$(cat "$FX/etc/samba/smb.conf.wex-pre-join")" = "${ORIG_SMB%$'\n'}" ] && [ -e "$FX/etc/krb5.conf.wex-pre-join" ] && [ -e "$FX/etc/nsswitch.conf.wex-pre-join" ] || fail 'the first versions were not kept'
grep -q '^kinit Administrator@AD.WEXLAB.EXAMPLE ' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'kinit was not given user@REALM'; }
[ "$(cat "$FX/kinit.stdin")" = 's3cret-pw' ] || fail 'kinit did not get the password on its standard input'
! grep -q 's3cret-pw' "$FX/calls.log" || fail 'the password is on a command line'
grep -q '^net ads join -k createcomputer=WexMachines osName=WineExplorer osVer=[^ ]*$' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'net ads join'; }
grep -q '^host -t SRV _ldap._tcp.ad.wexlab.example' "$FX/calls.log" || fail 'no DNS check'
grep -q '^pam-auth-update --enable winbind mkhomedir' "$FX/calls.log" && grep -q '^systemctl enable --now winbind' "$FX/calls.log" && grep -q '^net ads testjoin' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'the setup after the join'; }
cc=$(sed 's/^FILE://' "$FX/kinit.cc"); [ ! -e "$cc" ] && [ ! -e "$(dirname "$cc")" ] || fail 'the ticket cache was not removed'
grep -q '^kdestroy' "$FX/calls.log" || fail 'the ticket was not destroyed'
echo 'OK: wex-domain-join.sh join (files, calls, the password only on standard input, the cache removed)'

# the controller names the domain's NetBIOS name (a CLDAP ping), the first label is only the fallback
reset_etc
printf 'Domain: ad.wexlab.example
Pre-Win2k Domain: WEXLAB
' > "$FX/lookup"
out=$(join ad.wexlab.example --user Administrator --password-stdin) || { echo "$out"; cat "$FX/calls.log"; fail 'the join with a lookup failed'; }
sed 's/^ *//' "$FX/etc/samba/smb.conf" | grep -qx 'workgroup = WEXLAB' || { cat "$FX/etc/samba/smb.conf"; fail 'the NetBIOS name was not taken from the controller'; }
grep -q '^net ads lookup -S ad.wexlab.example' "$FX/calls.log" || fail 'the controller was not asked'
rm -f "$FX/lookup"
reset_etc

# ---- refusals
refused 'not a domain name' x --user a --password-stdin
refused 'not a domain name' 'a b.example' --user a --password-stdin
refused 'not a domain name' '-x.example' --user a --password-stdin
refused 'not a domain name' 'a..example' --user a --password-stdin
refused 'a user name is needed' ad.wexlab.example --password-stdin
refused 'a user name is needed' ad.wexlab.example --user '-oProxy' --password-stdin
refused 'a user name is needed' ad.wexlab.example --user 'a b' --password-stdin
refused 'standard input' ad.wexlab.example --user a
refused 'NetBIOS name' ad.wexlab.example --user a --password-stdin --workgroup 'WAYTOOLONGNAME1234'
refused 'not a distinguished name' ad.wexlab.example --user a --password-stdin --ou 'OU=x;rm'
reset_etc; echo winbind > "$FX/kind"
out=$(join ad.wexlab.example --user a --password-stdin) && fail 'a second join was not refused'; grep -q 'joined already' <<<"$out" || fail 'a second join: the refusal'
reset_etc; echo sssd > "$FX/kind"
out=$(join ad.wexlab.example --user a --password-stdin) && fail 'a join over an SSSD join was not refused'; grep -q 'joined with sssd' <<<"$out" && grep -q 'Winbind is the supported join' <<<"$out" || fail 'the SSSD refusal'
reset_etc; echo 1 > "$FX/host.rc"
out=$(join ad.wexlab.example --user a --password-stdin) && fail 'a domain not in DNS was not refused'; grep -q 'DNS does not know' <<<"$out" || fail 'the DNS refusal'
[ "$(cat "$FX/etc/samba/smb.conf")" = "${ORIG_SMB%$'\n'}" ] || fail 'the DNS refusal changed smb.conf'
HOSTNAME_FOR_TEST='bad_name' refused 'cannot be a computer name' ad.wexlab.example --user a --password-stdin
HOSTNAME_FOR_TEST='ad' refused 'both AD' ad.wexlab.example --user a --password-stdin
reset_etc; out=$(PW= join ad.wexlab.example --user a --password-stdin) && fail 'an empty password was taken'
# a missing tool is named
reset_etc; mv "$FX/bin/kinit" "$FX/bin/kinit.off"
out=$(printf 'x\n' | PATH="$FX/bin:/usr/bin:/bin" WEX_JOIN_NOROOT=1 WEX_JOIN_ETC="$FX/etc" WEX_DOMAIN_STATE="$FX/state" WEX_JOIN_HOSTNAME=wexbox bash "$SCRIPT" join ad.wexlab.example --user a --password-stdin 2>&1) && fail 'a missing kinit was not refused'
grep -q 'missing: kinit' <<<"$out" || { echo "$out"; fail 'the missing tool is not named'; }
mv "$FX/bin/kinit.off" "$FX/bin/kinit"
echo 'OK: wex-domain-join.sh refusals (names, user, password, kinds of join, DNS, host name, missing tools)'

# ---- a join that fails before the machine account exists puts the files back
for stage in kinit join; do
    reset_etc; echo 1 > "$FX/$stage.rc"
    out=$(join ad.wexlab.example --user Administrator --password-stdin) && fail "a failing $stage was not an error"
    [ "$(cat "$FX/etc/samba/smb.conf")" = "${ORIG_SMB%$'\n'}" ] && [ "$(cat "$FX/etc/krb5.conf")" = "${ORIG_KRB%$'\n'}" ] && [ "$(cat "$FX/etc/nsswitch.conf")" = "${ORIG_NSS%$'\n'}" ] ||
        { cat "$FX/etc/samba/smb.conf"; fail "a failing $stage left the files changed"; }
    ! grep -q '^pam-auth-update\|^systemctl enable' "$FX/calls.log" || fail "a failing $stage went on to set up winbind"
    [ "$stage" = join ] || ! grep -q '^net ads join' "$FX/calls.log" || fail 'net ads join ran after a failed kinit'
    cc=$(sed 's/^FILE://' "$FX/kinit.cc"); [ ! -e "$(dirname "$cc")" ] || fail "a failing $stage left the ticket cache"
done
echo 'OK: wex-domain-join.sh a failing kinit or join leaves smb.conf, krb5.conf and nsswitch.conf as they were'

# ---- leave
reset_etc
join ad.wexlab.example --user Administrator --password-stdin >/dev/null || fail 'the join before the leave'
echo winbind > "$FX/kind"; : > "$FX/calls.log"
printf '%s\n' 'WEXBOX$@AD.WEXLAB.EXAMPLE' host/WEXBOX@AD.WEXLAB.EXAMPLE host/wexbox.ad.wexlab.example@AD.WEXLAB.EXAMPLE \
    restrictedkrbhost/WEXBOX@AD.WEXLAB.EXAMPLE > "$FX/etc/krb5.keytab"
out=$(leave --user Administrator --password-stdin) || { echo "$out"; cat "$FX/calls.log"; fail 'the leave failed'; }
# the machine's keys go on this machine (net ads keytab flush would need the deleted account)
[ ! -e "$FX/etc/krb5.keytab" ] || fail 'the leave kept the machine keytab'
! grep -q 'keytab' <<<"$out" || { echo "$out"; fail 'the leave warned about the keytab'; }
! grep -q '^net ads keytab' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'the leave asked the controller to flush the keytab'; }
grep -q '^net ads leave -k' "$FX/calls.log" && grep -q '^kinit Administrator@AD.WEXLAB.EXAMPLE' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'the leave did not use the administrator'; }
! grep -q '^passwd:.*winbind' "$FX/etc/nsswitch.conf" && ! grep -q '^group:.*winbind' "$FX/etc/nsswitch.conf" || { cat "$FX/etc/nsswitch.conf"; fail 'nsswitch still has winbind'; }
grep -q '^shadow:         files$' "$FX/etc/nsswitch.conf" && grep -q '^passwd:         files systemd$' "$FX/etc/nsswitch.conf" || { cat "$FX/etc/nsswitch.conf"; fail 'nsswitch is not as it was'; }
grep -q '^pam-auth-update --remove winbind' "$FX/calls.log" && grep -q '^systemctl disable --now winbind' "$FX/calls.log" || fail 'winbind was not taken out'
[ "$(cat "$FX/etc/samba/smb.conf")" = "${ORIG_SMB%$'\n'}" ] && [ ! -e "$FX/etc/samba/smb.conf.wex-pre-join" ] || fail 'smb.conf was not put back'
[ "$(cat "$FX/etc/krb5.conf")" = "${ORIG_KRB%$'\n'}" ] || fail 'krb5.conf was not put back'
# the leave takes out what the join put in: mkhomedir too, and the join's first nsswitch.conf is not left for a later
# failed join to put back
grep -q '^pam-auth-update --remove winbind mkhomedir' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'the leave did not take out mkhomedir'; }
[ ! -e "$FX/etc/nsswitch.conf.wex-pre-join" ] && [ ! -e "$FX/etc/wineexplorer/domain-join-added" ] || fail 'the leave left the join records'
# mkhomedir that was on before the join (for local users) stays on
reset_etc; mkdir -p "$FX/etc/pam.d"; echo 'session optional pam_mkhomedir.so' > "$FX/etc/pam.d/common-session"
join ad.wexlab.example --user Administrator --password-stdin >/dev/null || fail 'the join with mkhomedir on'
echo winbind > "$FX/kind"; : > "$FX/calls.log"
leave --user Administrator --password-stdin >/dev/null || fail 'the leave with mkhomedir on'
grep -qx 'pam-auth-update --remove winbind' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'the leave took out a mkhomedir the join did not turn on'; }
rm -rf "$FX/etc/pam.d"
# the files are replaced, not written in place: the mode stays
reset_etc; chmod 0640 "$FX/etc/krb5.conf"
join ad.wexlab.example --user Administrator --password-stdin >/dev/null || fail 'the join for the mode'
[ "$(stat -c %a "$FX/etc/krb5.conf")" = 640 ] && [ "$(stat -c %a "$FX/etc/samba/smb.conf")" = 644 ] || { stat -c '%a %n' "$FX/etc/krb5.conf" "$FX/etc/samba/smb.conf"; fail 'a replaced file has another mode'; }
! ls "$FX/etc/samba/" | grep -q 'smb.conf\.[A-Za-z0-9]\{6\}$' || fail 'a temporary file was left'
# a controller that does not answer: nothing is changed without --force
reset_etc; join ad.wexlab.example --user Administrator --password-stdin >/dev/null; echo winbind > "$FX/kind"; echo 1 > "$FX/leave.rc"; : > "$FX/calls.log"
out=$(leave --user Administrator --password-stdin) && fail 'a failing net ads leave was not an error'; grep -q -- '--force' <<<"$out" || fail 'the --force hint'
grep -q '^passwd:.*winbind' "$FX/etc/nsswitch.conf" || fail 'a failed leave changed nsswitch'
: > "$FX/calls.log"
# a keytab with a key that is not the machine's stays, with a warning
printf '%s\n' 'WEXBOX$@AD.WEXLAB.EXAMPLE' nfs/wexbox.ad.wexlab.example@AD.WEXLAB.EXAMPLE > "$FX/etc/krb5.keytab"
out=$(leave --force) || { echo "$out"; fail 'leave --force failed'; }
! grep -q '^kinit\|^net ads' "$FX/calls.log" || { cat "$FX/calls.log"; fail '--force asked the controller'; }
[ -e "$FX/etc/krb5.keytab" ] && grep -q 'krb5.keytab stays' <<<"$out" || { echo "$out"; fail 'a keytab with another key was removed, or no warning'; }
! grep -q '^passwd:.*winbind' "$FX/etc/nsswitch.conf" || fail '--force did not take winbind out'
grep -q 'machine account stays' <<<"$out" || fail '--force did not say the account stays'
# not joined with Winbind: nothing to leave
reset_etc; out=$(leave --force) && fail 'a leave when not joined was not refused'; grep -q 'not joined with Winbind' <<<"$out" || fail 'the leave refusal'
echo sssd > "$FX/kind"; out=$(leave --force) && fail 'a leave of an SSSD join was not refused'
echo 'OK: wex-domain-join.sh leave (with the administrator, --force, a controller that does not answer, not joined)'

# ---- status
reset_etc; echo winbind > "$FX/kind"
out=$(status); grep -qx 'kind=winbind' <<<"$out" && grep -qx 'domain=ad.wexlab.example' <<<"$out" && grep -qx 'workgroup=WEXLAB' <<<"$out" && grep -qx 'testjoin=ok' <<<"$out" && grep -qx 'nsswitch=no' <<<"$out" || { echo "$out"; fail 'status'; }
echo 1 > "$FX/testjoin.rc"; out=$(status); grep -qx 'testjoin=failed' <<<"$out" || fail 'status: a failing testjoin'
rm -f "$FX/testjoin.rc"; echo sssd > "$FX/kind"; out=$(status); grep -qx 'kind=sssd' <<<"$out" && grep -q 'Winbind is the supported join' <<<"$out" || { echo "$out"; fail 'status of an SSSD join'; }
rm -f "$FX/kind"; out=$(status); grep -qx 'kind=none' <<<"$out" || fail 'status: not joined'
echo 'OK: wex-domain-join.sh status'

# ---- serve: one request on standard input (the socket unit), the answer is status= (a Win32 error code) and message=
serve()   # request lines...; the empty line that ends the request is added. SERVE_MODE=serve-query: the users' socket
{
    { printf '%s\n' "$@"; echo; } | PATH="$FX/bin:$PATH" WEX_JOIN_NOROOT=1 WEX_JOIN_ETC="$FX/etc" WEX_DOMAIN_STATE="$FX/state" \
        WEX_JOIN_HOSTNAME=wexbox WEX_JOIN_LOCK="$FX/lock" bash "$SCRIPT" "${SERVE_MODE:-serve}" 2>&1
}
status_of() { sed -n 's/^status=//p' <<<"$1" | head -n 1; }
expect()   # CODE WHAT request lines...
{
    local want=$1 what=$2 out; shift 2
    out=$(serve "$@")
    [ "$(status_of "$out")" = "$want" ] || { echo "$out" >&2; cat "$FX/calls.log" >&2; fail "serve: $what: status $(status_of "$out"), wanted $want"; }
    [ "$(grep -c '^status=' <<<"$out")" = 1 ] && [ "$(grep -c '^message=' <<<"$out")" = 1 ] || { echo "$out" >&2; fail "serve: $what: the answer is not one status and one message"; }
    ! grep -q 'p w\$d' <<<"$out" || fail "serve: $what: the password is in the answer"
}
PWSPEC='p w$d"`;\x'
reset_etc
expect 0 'a join' 'op=join' 'domain=ad.wexlab.example' 'user=Administrator' "password=$PWSPEC" 'ou=OU=WexMachines,DC=ad,DC=wexlab,DC=example' 'flags=3'
[ "$(cat "$FX/kinit.stdin")" = "$PWSPEC" ] || fail 'serve: the password reached kinit changed'
! grep -qF 'p w' "$FX/calls.log" || fail 'serve: the password is on a command line'
grep -q '^net ads join -k createcomputer=WexMachines' "$FX/calls.log" || fail 'serve: the join did not run with the OU'
out=$(serve op=join domain=ad.wexlab.example user=Administrator password=x); echo "$out" | grep -q 'message=' || fail 'serve: no message'
# a user principal name is used as it is, as an enterprise name (the controller resolves its case and suffix)
reset_etc; expect 0 'a join with a UPN' 'op=join' 'domain=ad.wexlab.example' 'user=admin@ad.wexlab.example' 'password=pw'
grep -q '^kinit -E admin@ad.wexlab.example ' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'serve: the UPN was changed'; }
# the user's own domain (DOMAIN\user): the joined domain's names are the realm; another DNS domain is that realm; another
# NetBIOS name is refused before anything is written (the engine cannot find its realm)
reset_etc; expect 0 'a user of the domain by its NetBIOS name' 'op=join' 'domain=ad.wexlab.example' 'user=admin' 'userdomain=ad' 'password=pw'
grep -q '^kinit admin@AD.WEXLAB.EXAMPLE ' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'serve: DOMAIN\\user of the joined domain'; }
reset_etc; expect 0 'a user of another DNS domain' 'op=join' 'domain=ad.wexlab.example' 'user=admin' 'userdomain=root.wexlab.example' 'password=pw'
grep -q '^kinit admin@ROOT.WEXLAB.EXAMPLE ' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'serve: DOMAIN\\user of another DNS domain'; }
reset_etc; expect 1355 'a user of another NetBIOS domain' 'op=join' 'domain=ad.wexlab.example' 'user=admin' 'userdomain=OTHER' 'password=pw'
! grep -q '^kinit' "$FX/calls.log" && [ "$(cat "$FX/etc/samba/smb.conf")" = "${ORIG_SMB%$'\n'}" ] || fail 'serve: another NetBIOS domain reached kinit or changed smb.conf'
# another change in progress: ERROR_BUSY at once, not a queue the caller's wait does not cover
reset_etc
( flock 8; sleep 5 ) 8>"$FX/lock" & holder=$!
sleep 0.5
out=$(WEX_JOIN_LOCK_WAIT=1 serve op=join domain=ad.wexlab.example user=a password=p)
[ "$(status_of "$out")" = 170 ] || { echo "$out"; fail 'serve: a held lock is not ERROR_BUSY'; }
kill "$holder" 2>/dev/null; wait "$holder" 2>/dev/null || true
# without flock nothing is changed
reset_etc; mkdir -p "$FX/noflock"
for t in bash sh cat sed tr cut head awk grep mktemp dirname readlink logger printf id; do
    p=$(command -v "$t" 2>/dev/null) && [ -x "$p" ] && ln -sf "$p" "$FX/noflock/$t"
done
out=$({ printf '%s\n' op=join domain=ad.wexlab.example user=a password=p; echo; } | PATH="$FX/bin:$FX/noflock" WEX_JOIN_NOROOT=1 WEX_JOIN_ETC="$FX/etc" WEX_DOMAIN_STATE="$FX/state" WEX_JOIN_HOSTNAME=wexbox WEX_JOIN_LOCK="$FX/lock" "$FX/noflock/bash" "$SCRIPT" serve 2>&1)
[ "$(status_of "$out")" = 50 ] && grep -q flock <<<"$out" || { echo "$out"; fail 'serve: a missing flock did not stop the change'; }
! grep -q '^kinit' "$FX/calls.log" || fail 'serve: without flock the join went on'
# a join that testjoin does not confirm is still a join: Windows programs are told (the state refresh runs)
reset_etc; echo 1 > "$FX/testjoin.rc"
WEX_NO_STATE_REFRESH= expect 31 'a testjoin that does not confirm, state told' 'op=join' 'domain=ad.wexlab.example' 'user=a' 'password=p'
grep -q '^systemd-run' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'serve: an unconfirmed join did not refresh the state'; }
# a refused join changes nothing on the host: no refresh
reset_etc; echo 1 > "$FX/kinit.rc"
WEX_NO_STATE_REFRESH= expect 1326 'a wrong password, no state change' 'op=join' 'domain=ad.wexlab.example' 'user=a' 'password=p'
! grep -q '^systemd-run' "$FX/calls.log" || fail 'serve: a refused join refreshed the state'
# bad requests
reset_etc
expect 87 'an unknown field' 'op=join' 'domain=ad.wexlab.example' 'user=a' 'password=p' 'evil=1'
expect 87 'a line without =' 'op=join' 'nonsense'
expect 87 'a control character' 'op=join' $'domain=ad.wexlab.example\001'
expect 87 'a long value' 'op=join' "domain=$(printf 'a%.0s' $(seq 1 600))"
expect 87 'no operation' 'domain=ad.wexlab.example'
expect 87 'an unknown operation' 'op=format'
expect 87 'a bad domain' 'op=join' 'domain=x' 'user=a' 'password=p'
expect 87 'a bad user' 'op=join' 'domain=ad.wexlab.example' 'user=-oX' 'password=p'
expect 87 'no password' 'op=join' 'domain=ad.wexlab.example' 'user=a'
expect 87 'an empty password' 'op=join' 'domain=ad.wexlab.example' 'user=a' 'password='
expect 87 'a bad OU' 'op=join' 'domain=ad.wexlab.example' 'user=a' 'password=p' 'ou=OU=x;y'
! grep -q '^kinit\|^net ads join' "$FX/calls.log" || fail 'serve: a bad request reached kinit or net'
reset_etc; echo winbind > "$FX/kind"; expect 2691 'joined already' 'op=join' 'domain=ad.wexlab.example' 'user=a' 'password=p'
echo sssd > "$FX/kind"; expect 2691 'joined with sssd' 'op=join' 'domain=ad.wexlab.example' 'user=a' 'password=p'
reset_etc; echo 1 > "$FX/host.rc"; expect 1355 'no controllers in DNS' 'op=join' 'domain=ad.wexlab.example' 'user=a' 'password=p'
reset_etc; echo 1 > "$FX/kinit.rc"; expect 1326 'a wrong password' 'op=join' 'domain=ad.wexlab.example' 'user=a' 'password=p'
[ "$(cat "$FX/etc/samba/smb.conf")" = "${ORIG_SMB%$'\n'}" ] || fail 'serve: a wrong password left smb.conf changed'
reset_etc; echo 1 > "$FX/join.rc"; expect 31 'a failing net ads join' 'op=join' 'domain=ad.wexlab.example' 'user=a' 'password=p'
reset_etc; echo 1 > "$FX/testjoin.rc"; expect 31 'a testjoin that does not confirm' 'op=join' 'domain=ad.wexlab.example' 'user=a' 'password=p'
reset_etc; mv "$FX/bin/kinit" "$FX/bin/kinit.off"
out=$({ printf '%s\n' op=join domain=ad.wexlab.example user=a password=p; echo; } | PATH="$FX/bin:/usr/bin:/bin" WEX_JOIN_NOROOT=1 WEX_JOIN_ETC="$FX/etc" WEX_DOMAIN_STATE="$FX/state" WEX_JOIN_HOSTNAME=wexbox bash "$SCRIPT" serve 2>&1)
[ "$(status_of "$out")" = 50 ] || { echo "$out"; fail 'serve: a missing tool is not 50'; }
mv "$FX/bin/kinit.off" "$FX/bin/kinit"
# leave
reset_etc; join ad.wexlab.example --user Administrator --password-stdin >/dev/null; echo winbind > "$FX/kind"; : > "$FX/calls.log"
expect 0 'a leave with the administrator' 'op=leave' 'user=Administrator' 'password=pw'
grep -q '^net ads leave -k' "$FX/calls.log" || fail 'serve: leave did not ask the controller'
! grep -q 'keep-account' "$FX/calls.log" || fail 'serve: a leave without flags kept the account'
# NETSETUP_ACCT_DELETE (4) absent: the account is kept; present: it goes. A Samba with --use-kerberos is given that
# (-k is deprecated there)
reset_etc; join ad.wexlab.example --user Administrator --password-stdin >/dev/null; echo winbind > "$FX/kind"; : > "$FX/calls.log"
expect 0 'a leave that keeps the account' 'op=leave' 'user=Administrator' 'password=pw' 'flags=0'
grep -q '^net ads leave -k --keep-account' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'serve: leave without ACCT_DELETE did not keep the account'; }
reset_etc; join ad.wexlab.example --user Administrator --password-stdin >/dev/null; echo winbind > "$FX/kind"; : > "$FX/calls.log"
expect 0 'a leave that deletes the account' 'op=leave' 'user=Administrator' 'password=pw' 'flags=4'
! grep -q 'keep-account' "$FX/calls.log" || fail 'serve: leave with ACCT_DELETE kept the account'
# a number with a leading zero is a decimal one ("04" is 4, "08" is not an octal error that falls through to a delete)
reset_etc; join ad.wexlab.example --user Administrator --password-stdin >/dev/null; echo winbind > "$FX/kind"; : > "$FX/calls.log"
expect 0 'a leave with flags 08 keeps the account' 'op=leave' 'user=Administrator' 'password=pw' 'flags=08'
grep -q 'keep-account' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'serve: flags=08 was not read as decimal'; }
reset_etc; join ad.wexlab.example --user Administrator --password-stdin >/dev/null; echo winbind > "$FX/kind"; : > "$FX/calls.log"
expect 0 'a leave with flags 04 deletes the account' 'op=leave' 'user=Administrator' 'password=pw' 'flags=04'
! grep -q 'keep-account' "$FX/calls.log" || fail 'serve: flags=04 kept the account'
touch "$FX/newnet"
reset_etc; : > "$FX/calls.log"
out=$(join ad.wexlab.example --user Administrator --password-stdin) || { echo "$out"; fail 'the join with a new Samba failed'; }
grep -q '^net ads join --use-kerberos=required' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'a new Samba was not given --use-kerberos'; }
rm -f "$FX/newnet"
reset_etc; join ad.wexlab.example --user Administrator --password-stdin >/dev/null; echo winbind > "$FX/kind"; : > "$FX/calls.log"
reset_etc; join ad.wexlab.example --user Administrator --password-stdin >/dev/null; echo winbind > "$FX/kind"; : > "$FX/calls.log"
expect 0 'a forced leave' 'op=leave' 'force=1'
! grep -q '^kinit\|^net ads leave' "$FX/calls.log" || fail 'serve: a forced leave asked the controller'
# each note is an item of the message ("||" between them), which netdom prints as a line
reset_etc; join ad.wexlab.example --user Administrator --password-stdin >/dev/null; echo winbind > "$FX/kind"
out=$(serve op=leave force=1)
grep -q '^message=leaving on this machine only: the machine account stays in the domain||left the domain$' <<<"$out" ||
    { echo "$out"; fail 'serve: the notes are not items of the message'; }
reset_etc; expect 2692 'a leave when not joined' 'op=leave' 'force=1'
reset_etc; echo winbind > "$FX/kind"; echo 1 > "$FX/kinit.rc"; expect 1326 'a leave with a wrong password' 'op=leave' 'user=a' 'password=p'
# validate
reset_etc
expect 0 'a computer name' 'op=validate' 'nametype=machine' 'name=WEXBOX-1'
expect 1210 'a name with an underscore' 'op=validate' 'nametype=machine' 'name=bad_name'
expect 1210 'only digits' 'op=validate' 'nametype=machine' 'name=12345'
expect 1210 'sixteen characters' 'op=validate' 'nametype=machine' 'name=ABCDEFGHIJKLMNOP'
expect 1210 'a hyphen at the end' 'op=validate' 'nametype=machine' 'name=abc-'
expect 0 'a DNS host name' 'op=validate' 'nametype=dnsmachine' 'name=long-host-name-over-fifteen-chars'
expect 0 'a workgroup' 'op=validate' 'nametype=workgroup' 'name=WORKGROUP'
expect 1212 'a bad workgroup' 'op=validate' 'nametype=workgroup' 'name=A B'
expect 0 'a domain that DNS knows' 'op=validate' 'nametype=domain' 'name=ad.wexlab.example'
expect 52 'a domain that exists, as a name that must not' 'op=validate' 'nametype=nonexistentdomain' 'name=ad.wexlab.example'
echo 1 > "$FX/host.rc"
expect 1355 'a domain that DNS does not know' 'op=validate' 'nametype=domain' 'name=nowhere.example'
expect 0 'a name that is no domain, where that is wanted' 'op=validate' 'nametype=nonexistentdomain' 'name=nowhere.example'
expect 1212 'a bad domain name' 'op=validate' 'nametype=domain' 'name=a..b'
expect 87 'an unknown name type' 'op=validate' 'nametype=other' 'name=x'
# status
reset_etc; echo winbind > "$FX/kind"
out=$(serve op=status); [ "$(status_of "$out")" = 0 ] && grep -q '^message=kind=winbind' <<<"$out" || { echo "$out"; fail 'serve: status'; }
# a client that sends nothing is answered with a time out, not kept for ever; a bad join request is 87, not a usage text
out=$( { sleep 40; } | PATH="$FX/bin:$PATH" WEX_JOIN_NOROOT=1 WEX_JOIN_ETC="$FX/etc" WEX_DOMAIN_STATE="$FX/state" WEX_JOIN_LOCK="$FX/lock" bash "$SCRIPT" serve 2>&1)
[ "$(status_of "$out")" = 1460 ] || { echo "$out"; fail 'serve: a silent client did not get 1460'; }
out=$(serve op=join domain=); [ "$(status_of "$out")" = 87 ] || { echo "$out"; fail 'serve: an empty domain is not 87'; }
echo 'OK: wex-domain-join.sh serve (a request, the answer, the codes, a password that reaches kinit unchanged and nothing else)'

# ---- serve-query, the socket every user reaches: the questions only, no password, nothing changed
reset_etc; echo winbind > "$FX/kind"
printf '[global]\n    workgroup = WEXLAB\n    realm = AD.WEXLAB.EXAMPLE\n    netbios name = WEXBOX\n' > "$FX/etc/samba/smb.conf"
before=$(cat "$FX/etc/samba/smb.conf" "$FX/etc/krb5.conf" "$FX/etc/nsswitch.conf" | md5sum)
export SERVE_MODE=serve-query
: > "$FX/calls.log"
expect 0 'a question: a computer name' 'op=validate' 'nametype=machine' 'name=newbox'
expect 1210 'a question: a bad computer name' 'op=validate' 'nametype=machine' 'name=12345'
expect 0 'a question: the controllers' 'op=dclist'
expect 0 'a question: the channel' 'op=scquery'
expect 0 'a question: the status' 'op=status'
for op in join leave rename verify reset; do
    expect 5 "a change through the users' socket ($op)" "op=$op" 'domain=ad.wexlab.example' 'user=Administrator' 'name=newbox' 'force=1'
done
expect 87 'a question with a password' 'op=validate' 'nametype=machine' 'name=newbox' 'password=x'
expect 87 'an unknown field' 'op=status' 'evil=1'
! grep -qE '^(kinit|net ads (join|leave|changetrustpw)|pam-auth-update|systemctl|hostnamectl)' "$FX/calls.log" ||
    { cat "$FX/calls.log"; fail "serve-query: a question changed something"; }
[ "$(cat "$FX/etc/samba/smb.conf" "$FX/etc/krb5.conf" "$FX/etc/nsswitch.conf" | md5sum)" = "$before" ] || fail 'serve-query: a file changed'
unset SERVE_MODE
echo "OK: wex-domain-join.sh serve-query (the questions answer; changes are access denied; no password; nothing changed)"

# ---- the secure channel and the controllers (netdom verify and reset, nltest /sc_verify and /sc_reset)
reset_etc; rm -f "$FX/dc.none"
expect 2692 'verify when not joined' 'op=verify'
expect 2692 'reset when not joined' 'op=reset'
expect 2692 'dclist when not joined' 'op=dclist'
echo winbind > "$FX/kind"
out=$(serve op=verify); [ "$(status_of "$out")" = 0 ] && grep -q 'message=The secure channel from WEXBOX to the domain AD.WEXLAB.EXAMPLE has been verified.||The connection is with the machine dc1.ad.wexlab.example.$' <<<"$out" || { echo "$out"; fail 'serve: verify'; }
grep -q '^net ads testjoin' "$FX/calls.log" && grep -q '^wbinfo --check-secret' "$FX/calls.log" || fail 'serve: verify did not ask Samba and Winbind'
! grep -q 'changetrustpw' "$FX/calls.log" || fail 'serve: verify changed the machine password'
echo 1 > "$FX/testjoin.rc"; expect 1789 'verify with a failing testjoin' op=verify
rm -f "$FX/testjoin.rc"; echo 1 > "$FX/wbinfo.rc"; expect 1789 'verify with a failing trust secret' op=verify
rm -f "$FX/wbinfo.rc"; : > "$FX/calls.log"
out=$(serve op=reset); [ "$(status_of "$out")" = 0 ] && grep -q 'to the domain AD.WEXLAB.EXAMPLE has been reset.' <<<"$out" || { echo "$out"; fail 'serve: reset'; }
[ "$(grep -n 'changetrustpw' "$FX/calls.log" | head -1 | cut -d: -f1)" -lt "$(grep -n 'testjoin' "$FX/calls.log" | head -1 | cut -d: -f1)" ] || fail 'serve: reset did not change the password before it verified'
echo 1 > "$FX/ctp.rc"; expect 1789 'reset with a failing password change' op=reset
rm -f "$FX/ctp.rc"
out=$(serve op=dclist); [ "$(status_of "$out")" = 0 ] && grep -q '^message=dc1.ad.wexlab.example||dc2.ad.wexlab.example$' <<<"$out" || { echo "$out"; fail 'serve: dclist is not the sorted names, one item each, without the trailing dot'; }
: > "$FX/dc.none"; expect 1355 'no controllers' op=dclist
# nltest's questions: the controller's own answer (dsgetdc), the secure channel's state (scquery)
cat > "$FX/lookup" <<'LK'
Information for Domain Controller: 10.71.0.10

Response Type: LOGON_SAM_LOGON_RESPONSE_EX
GUID: 0f2a204e-eb7a-4e88-9cf9-1504bb1b93d5
Flags:
	Is a PDC:                                   yes
	Is a GC of the forest:                      yes
	Is an LDAP server:                          yes
	Supports DS:                                yes
	Is running a KDC:                           yes
	Is running time services:                   yes
	Is the closest DC:                          yes
	Is writable:                                yes
	Has a hardware clock:                       yes
	Is a non-domain NC serviced by LDAP server: no
	Is NT6 DC that has some secrets:            no
	Is NT6 DC that has all secrets:             yes
	Runs Active Directory Web Services:         no
	Runs on Windows 2012 or later:              no
Forest: ad.wexlab.example
Domain: ad.wexlab.example
Domain Controller: dc1.ad.wexlab.example
Pre-Win2k Domain: WEXLAB
Pre-Win2k Hostname: DC1
Server Site Name: Default-First-Site-Name
Client Site Name: Default-First-Site-Name
LK
out=$(serve op=dsgetdc)
[ "$(status_of "$out")" = 0 ] && grep -q '^message=dc=dc1.ad.wexlab.example||addr=10.71.0.10||guid=0f2a204e-eb7a-4e88-9cf9-1504bb1b93d5||domain=WEXLAB||dnsdomain=ad.wexlab.example||forest=ad.wexlab.example||dcsite=Default-First-Site-Name||oursite=Default-First-Site-Name||flags=e00013fd$' <<<"$out" || { echo "$out"; fail 'serve: dsgetdc'; }
cp "$FX/lookup" "$FX/lookup.orig"
sed -i 's/^Domain Controller: .*/Domain Controller: dc1||flags=0/' "$FX/lookup"
out=$(serve op=dsgetdc)
grep -q '^message=dc=dc1flags=0||addr=' <<<"$out" && grep -q '||flags=e00013fd$' <<<"$out" || { echo "$out"; fail 'serve: dsgetdc lets the controller add a field'; }
mv "$FX/lookup.orig" "$FX/lookup"
: > "$FX/calls.log"
out=$(serve op=scquery)
[ "$(status_of "$out")" = 0 ] && grep -q '^message=dc=dc1.ad.wexlab.example||status=0$' <<<"$out" || { echo "$out"; fail 'serve: scquery'; }
# the channel is Winbind's NETLOGON ping, which any user may ask; testjoin (root's secret) is not used
grep -q '^wbinfo --ping-dc' "$FX/calls.log" && ! grep -q '^net ads testjoin' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'serve: scquery did not ask Winbind'; }
echo 1 > "$FX/wbinfo.rc"
out=$(serve op=scquery); grep -q '||status=1722$' <<<"$out" || { echo "$out"; fail 'serve: scquery with a channel that does not work'; }
echo 'x||status=0' > "$FX/dcname"
out=$(serve op=scquery); grep -q '^message=dc=xstatus=0||status=1722$' <<<"$out" || { echo "$out"; fail 'serve: scquery lets the controller add a status'; }
rm -f "$FX/wbinfo.rc" "$FX/dcname"
echo nowinbind > "$FX/kind"
expect 2692 'dsgetdc when not joined with winbind' 'op=dsgetdc'
expect 2692 'scquery when not joined with winbind' 'op=scquery'
echo winbind > "$FX/kind"
rm -f "$FX/lookup"
echo 'OK: wex-domain-join.sh serve verify, reset and dclist'

# the rename of the computer in the domain: the account over LDAP, smb.conf, the password, the keytab
printf '[global]\n    workgroup = WEXLAB\n    realm = AD.WEXLAB.EXAMPLE\n    netbios name = WEXBOX\n' > "$FX/etc/samba/smb.conf"
: > "$FX/calls.log"; rm -f "$FX/ldif.log" "$FX/renamed" "$FX/dup" "$FX/kc.rc" "$FX/ldap.rc"
echo winbind > "$FX/kind"
expect 0 'a rename' 'op=rename' 'name=newbox' 'user=Administrator' "password=$PWSPEC"
grep -q 'netbios name = NEWBOX' "$FX/etc/samba/smb.conf" || { cat "$FX/etc/samba/smb.conf"; fail 'rename: smb.conf has not the new name'; }
grep -q '^dn: CN=WEXBOX,CN=Computers,DC=ad,DC=wexlab,DC=example' "$FX/ldif.log" && grep -q '^newrdn: CN=NEWBOX$' "$FX/ldif.log" || { cat "$FX/ldif.log"; fail 'rename: the object was not renamed'; }
grep -q '^sAMAccountName: NEWBOX\$' "$FX/ldif.log" && grep -q '^dNSHostName: newbox.ad.wexlab.example$' "$FX/ldif.log" && grep -q '^servicePrincipalName: HOST/NEWBOX.ad.wexlab.example$' "$FX/ldif.log" || { cat "$FX/ldif.log"; fail 'rename: the names of the account'; }
[ "$(cat "$FX/kinit.stdin")" = "$PWSPEC" ] || fail 'rename: the password reached kinit changed'
grep -q '^hostnamectl set-hostname newbox$' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'rename: the host name was not set'; }
! grep -qF 'p w' "$FX/calls.log" "$FX/ldif.log" || fail 'rename: the password is on a command line or in the LDIF'
# the order: password, then the keytab flushed (it takes the names off), the names set, the keytab made
a=$(grep -n '^net ads changetrustpw' "$FX/calls.log" | head -n 1 | cut -d: -f1)
b=$(grep -n '^net ads keytab flush' "$FX/calls.log" | tail -n 1 | cut -d: -f1)
c=$(grep -n '^net ads keytab create' "$FX/calls.log" | tail -n 1 | cut -d: -f1)
[ -n "$a" ] && [ -n "$b" ] && [ -n "$c" ] && [ "$a" -lt "$b" ] && [ "$b" -lt "$c" ] || { cat "$FX/calls.log"; fail 'rename: the order of the password and the keytab'; }
# a name that is taken, a bad name, the same name, not joined, a refused ticket
printf '[global]\n    workgroup = WEXLAB\n    realm = AD.WEXLAB.EXAMPLE\n    netbios name = WEXBOX\n' > "$FX/etc/samba/smb.conf"
touch "$FX/dup"
expect 52 'a rename to a name that is taken' 'op=rename' 'name=newbox' 'user=Administrator' 'password=x'
rm -f "$FX/dup"
expect 87 'a rename to a bad name' 'op=rename' 'name=bad_name' 'user=Administrator' 'password=x'
expect 87 'a rename to the same name' 'op=rename' 'name=wexbox' 'user=Administrator' 'password=x'
expect 1210 'a rename to the domain name' 'op=rename' 'name=wexlab' 'user=Administrator' 'password=x'
echo 1 > "$FX/kinit.rc"
expect 1326 'a rename with a wrong password' 'op=rename' 'name=newbox' 'user=Administrator' 'password=x'
rm -f "$FX/kinit.rc"
echo nowinbind > "$FX/kind"
expect 2692 'a rename when not joined with winbind' 'op=rename' 'name=newbox' 'user=Administrator' 'password=x'
echo winbind > "$FX/kind"
# a keytab that cannot be made: the account and smb.conf are put back
: > "$FX/calls.log"; rm -f "$FX/ldif.log"
echo 1 > "$FX/kc.rc"
expect 31 'a rename whose keytab fails' 'op=rename' 'name=newbox' 'user=Administrator' 'password=x'
grep -q 'netbios name = WEXBOX' "$FX/etc/samba/smb.conf" && ! grep -q NEWBOX "$FX/etc/samba/smb.conf" || { cat "$FX/etc/samba/smb.conf"; fail 'rename: smb.conf was not put back'; }
grep -q '^newrdn: CN=WEXBOX$' "$FX/ldif.log" || { cat "$FX/ldif.log"; fail 'rename: the account was not put back'; }
! grep -q '^hostnamectl set-hostname newbox$' "$FX/calls.log" || fail 'rename: the host name was set after a failed keytab'
: > "$FX/calls.log"; rm -f "$FX/ldif.log" "$FX/kc.rc"
echo 1 > "$FX/hn.rc"
expect 31 'a rename whose host name cannot be set' 'op=rename' 'name=newbox' 'user=Administrator' 'password=x'
rm -f "$FX/hn.rc"
grep -q 'netbios name = WEXBOX' "$FX/etc/samba/smb.conf" && ! grep -q NEWBOX "$FX/etc/samba/smb.conf" || { cat "$FX/etc/samba/smb.conf"; fail 'rename: smb.conf was not put back after a host name failure'; }
rm -f "$FX/kc.rc" "$FX/ldif.log"
expect 87 'a rename to a name of digits only' 'op=rename' 'name=12345' 'user=Administrator' 'password=x'
# a rename stopped part-way (RuntimeMaxSec, systemctl stop): SIGTERM puts the account and smb.conf back, and the answer
# says so
: > "$FX/calls.log"; rm -f "$FX/ldif.log" "$FX/kc.started"; touch "$FX/kc.sleep"
{ printf '%s\n' op=rename name=newbox user=Administrator password=x; echo; } | PATH="$FX/bin:$PATH" WEX_JOIN_NOROOT=1 \
    WEX_JOIN_ETC="$FX/etc" WEX_DOMAIN_STATE="$FX/state" WEX_JOIN_HOSTNAME=wexbox WEX_JOIN_LOCK="$FX/lock" \
    bash "$SCRIPT" serve > "$FX/stopped.out" 2>&1 & engine=$!
for _ in $(seq 1 50); do [ -e "$FX/kc.started" ] && break; sleep 0.1; done
[ -e "$FX/kc.started" ] || fail 'rename: the stop case did not reach the keytab'
pkill -TERM -f "$SCRIPT serve" 2>/dev/null || kill -TERM "$engine"
wait "$engine" 2>/dev/null || true
[ "$(status_of "$(cat "$FX/stopped.out")")" = 1460 ] || { cat "$FX/stopped.out" "$FX/calls.log"; fail 'rename: a stopped rename is not ERROR_TIMEOUT'; }
grep -q 'netbios name = WEXBOX' "$FX/etc/samba/smb.conf" && ! grep -q NEWBOX "$FX/etc/samba/smb.conf" || { cat "$FX/etc/samba/smb.conf"; fail 'rename: smb.conf was not put back after a stop'; }
grep -q '^newrdn: CN=WEXBOX$' "$FX/ldif.log" || { cat "$FX/ldif.log"; fail 'rename: the account was not put back after a stop'; }
[ ! -e "$FX/etc/samba/smb.conf.wex-rename" ] || fail 'rename: the copy of smb.conf was left after a stop'
rm -f "$FX/kc.started" "$FX/ldif.log"
echo 'OK: wex-domain-join.sh serve rename'
echo domain_join=PASS
