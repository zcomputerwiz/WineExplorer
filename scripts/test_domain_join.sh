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

mkdir -p "$FX/bin" "$FX/etc/samba" "$FX/home"
cat > "$FX/bin/net" <<EOF
#!/bin/sh
echo "net \$*" >> "$FX/calls.log"
case "\$*" in
    "ads join"*) exit \$(cat "$FX/join.rc" 2>/dev/null || echo 0) ;;
    "ads testjoin") exit \$(cat "$FX/testjoin.rc" 2>/dev/null || echo 0) ;;
    "ads leave"*) exit \$(cat "$FX/leave.rc" 2>/dev/null || echo 0) ;;
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
exit \$(cat "$FX/host.rc" 2>/dev/null || echo 0)
EOF
cat > "$FX/bin/testparm" <<'EOF'
#!/bin/sh
echo ad.wexlab.example
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
          "$FX"/*.rc "$FX/kind" "$FX/calls.log" "$FX/kinit.stdin" "$FX/kinit.cc"
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
grep -q '^net ads join -k createcomputer=OU=WexMachines,DC=ad,DC=wexlab,DC=example$' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'net ads join'; }
grep -q '^host -t SRV _ldap._tcp.ad.wexlab.example' "$FX/calls.log" || fail 'no DNS check'
grep -q '^pam-auth-update --enable winbind mkhomedir' "$FX/calls.log" && grep -q '^systemctl enable --now winbind' "$FX/calls.log" && grep -q '^net ads testjoin' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'the setup after the join'; }
cc=$(sed 's/^FILE://' "$FX/kinit.cc"); [ ! -e "$cc" ] && [ ! -e "$(dirname "$cc")" ] || fail 'the ticket cache was not removed'
grep -q '^kdestroy' "$FX/calls.log" || fail 'the ticket was not destroyed'
echo 'OK: wex-domain-join.sh join (files, calls, the password only on standard input, the cache removed)'

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
out=$(leave --user Administrator --password-stdin) || { echo "$out"; cat "$FX/calls.log"; fail 'the leave failed'; }
grep -q '^net ads leave -k' "$FX/calls.log" && grep -q '^kinit Administrator@AD.WEXLAB.EXAMPLE' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'the leave did not use the administrator'; }
! grep -q '^passwd:.*winbind' "$FX/etc/nsswitch.conf" && ! grep -q '^group:.*winbind' "$FX/etc/nsswitch.conf" || { cat "$FX/etc/nsswitch.conf"; fail 'nsswitch still has winbind'; }
grep -q '^shadow:         files$' "$FX/etc/nsswitch.conf" && grep -q '^passwd:         files systemd$' "$FX/etc/nsswitch.conf" || { cat "$FX/etc/nsswitch.conf"; fail 'nsswitch is not as it was'; }
grep -q '^pam-auth-update --remove winbind' "$FX/calls.log" && grep -q '^systemctl disable --now winbind' "$FX/calls.log" || fail 'winbind was not taken out'
[ "$(cat "$FX/etc/samba/smb.conf")" = "${ORIG_SMB%$'\n'}" ] && [ ! -e "$FX/etc/samba/smb.conf.wex-pre-join" ] || fail 'smb.conf was not put back'
[ "$(cat "$FX/etc/krb5.conf")" = "${ORIG_KRB%$'\n'}" ] || fail 'krb5.conf was not put back'
# a controller that does not answer: nothing is changed without --force
reset_etc; join ad.wexlab.example --user Administrator --password-stdin >/dev/null; echo winbind > "$FX/kind"; echo 1 > "$FX/leave.rc"; : > "$FX/calls.log"
out=$(leave --user Administrator --password-stdin) && fail 'a failing net ads leave was not an error'; grep -q -- '--force' <<<"$out" || fail 'the --force hint'
grep -q '^passwd:.*winbind' "$FX/etc/nsswitch.conf" || fail 'a failed leave changed nsswitch'
: > "$FX/calls.log"
out=$(leave --force) || { echo "$out"; fail 'leave --force failed'; }
! grep -q '^kinit\|^net ads leave' "$FX/calls.log" || fail '--force asked the controller'
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
serve()   # request lines...; the empty line that ends the request is added
{
    { printf '%s\n' "$@"; echo; } | PATH="$FX/bin:$PATH" WEX_JOIN_NOROOT=1 WEX_JOIN_ETC="$FX/etc" WEX_DOMAIN_STATE="$FX/state" \
        WEX_JOIN_HOSTNAME=wexbox bash "$SCRIPT" serve 2>&1
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
grep -q '^net ads join -k createcomputer=OU=WexMachines' "$FX/calls.log" || fail 'serve: the join did not run with the OU'
out=$(serve op=join domain=ad.wexlab.example user=Administrator password=x); echo "$out" | grep -q 'message=' || fail 'serve: no message'
# a user principal name is used as it is
reset_etc; expect 0 'a join with a UPN' 'op=join' 'domain=ad.wexlab.example' 'user=admin@ad.wexlab.example' 'password=pw'
grep -q '^kinit admin@ad.wexlab.example ' "$FX/calls.log" || { cat "$FX/calls.log"; fail 'serve: the UPN was changed'; }
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
reset_etc; join ad.wexlab.example --user Administrator --password-stdin >/dev/null; echo winbind > "$FX/kind"; : > "$FX/calls.log"
expect 0 'a forced leave' 'op=leave' 'force=1'
! grep -q '^kinit\|^net ads leave' "$FX/calls.log" || fail 'serve: a forced leave asked the controller'
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
echo 'OK: wex-domain-join.sh serve (a request, the answer, the codes, a password that reaches kinit unchanged and nothing else)'
echo domain_join=PASS
