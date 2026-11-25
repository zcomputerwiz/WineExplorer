#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Tests of the root side of blank passwords (docs/BLANK_PASSWORDS.md): the reconcile of the group nopasswdlogin
# (installer/blank/wex-accounts-blank-reconcile) and the PAM change (installer/blank/wex-blank-pam.sh). Run as root on a
# Debian machine that can be changed (wexgpo): the second half makes real accounts and diverts the real PAM profile, then
# puts everything back. Prints "ok NAME" / "FAIL NAME" lines and "blankpw=PASS" or a failure count.
#
#   test_blank_password.sh [--real]      without --real only the directory-redirected form runs
set -u
HERE=$(cd "$(dirname "$0")" && pwd)
ROOTDIR=$(dirname "$HERE")
RECONCILE=$ROOTDIR/installer/blank/wex-accounts-blank-reconcile
PAM=$ROOTDIR/installer/blank/wex-blank-pam.sh
fail=0
ok() { echo "ok $1"; }
bad() { echo "FAIL $1"; fail=$((fail + 1)); }
check() { local name=$1; shift; if "$@" > /dev/null 2>&1; then ok "$name"; else bad "$name"; fi; }
members() { awk -F: '$1 == "nopasswdlogin" { print $4 }' "$1"; }

T=$(mktemp -d)
trap 'rm -rf "$T"' EXIT

# ---------------------------------------------------------------- the redirected form
mkdir -p "$T/etc" "$T/usr/share/pam-configs" "$T/etc/pam.d"
cat > "$T/etc/login.defs" << 'EOF'
UID_MIN 1000
UID_MAX 60000
EOF
cat > "$T/etc/passwd" << 'EOF'
root:x:0:0::/root:/bin/sh
svc:x:999:999::/:/bin/false
alice:x:1001:1001::/home/alice:/bin/sh
bob:x:1002:1002::/home/bob:/bin/sh
carol:x:1003:1003::/home/carol:/bin/sh
dave:x:1004:1004::/home/dave:/bin/sh
nobody:x:65534:65534::/:/bin/false
EOF
cat > "$T/etc/shadow" << 'EOF'
root::19000:0:99999:7:::
svc::19000:0:99999:7:::
alice::19000:0:99999:7:::
bob:$6$salt$hash:19000:0:99999:7:::
carol:!:19000:0:99999:7:::
dave:!$6$salt$hash:19000:0:99999:7:::
nobody::19000:0:99999:7:::
EOF
printf 'sudo:x:27:alice\nnopasswdlogin:x:1234:bob,carol\n' > "$T/etc/group"
export WEX_BLANK_ROOT=$T
sh "$RECONCILE" > /dev/null
[ "$(members "$T/etc/group")" = "alice" ] && ok "reconcile: only the ordinary account with an empty field is a member (root, svc and nobody are not, a hash and a lock are not)" ||
    bad "reconcile: members are [$(members "$T/etc/group")]"
grep -q '^sudo:x:27:alice$' "$T/etc/group" && ok "reconcile: other groups are untouched" || bad "reconcile: another group changed"
sed -i 's/^bob:[^:]*:/bob::/' "$T/etc/shadow"
sh "$RECONCILE" > /dev/null
[ "$(members "$T/etc/group")" = "alice,bob" ] && ok "reconcile: a new empty field is added" || bad "reconcile: members are [$(members "$T/etc/group")]"
sed -i 's/^alice::/alice:$6$s$h:/' "$T/etc/shadow"
sh "$RECONCILE" > /dev/null
[ "$(members "$T/etc/group")" = "bob" ] && ok "reconcile: a member who now has a password is removed" || bad "reconcile: members are [$(members "$T/etc/group")]"
sed -i '/^nopasswdlogin:/d' "$T/etc/group"
sh "$RECONCILE" > /dev/null
[ "$(members "$T/etc/group")" = "bob" ] && ok "reconcile: a missing group is made" || bad "reconcile: no group after: [$(cat "$T/etc/group")]"
sed -i '/^nopasswdlogin:/d' "$T/etc/group"
printf 'nopasswdlogin:x:1234:root,svc,bob,ghost\n' >> "$T/etc/group"
sh "$RECONCILE" > /dev/null
[ "$(members "$T/etc/group")" = "bob" ] && ok "reconcile: root, a system account and an unknown name are removed" || bad "reconcile: members are [$(members "$T/etc/group")]"
echo 'UID_MIN 5000' > "$T/etc/login.defs"
sh "$RECONCILE" > /dev/null
[ "$(members "$T/etc/group")" = "bob" ] || ok "reconcile: a UID_MIN of 5000 puts the ordinary range above these accounts"
# UID_MIN below 1000 is not trusted
echo 'UID_MIN 0' > "$T/etc/login.defs"
sh "$RECONCILE" > /dev/null
case ",$(members "$T/etc/group")," in *,root,*) bad "reconcile: a UID_MIN of 0 let root in" ;; *) ok "reconcile: a UID_MIN of 0 is not trusted (root stays out)" ;; esac

# the PAM files
cat > "$T/usr/share/pam-configs/unix" << 'EOF'
Name: Unix authentication
Default: yes
Priority: 256
Auth-Type: Primary
Auth:
	[success=end default=ignore]	pam_unix.so nullok try_first_pass
Auth-Initial:
	[success=end default=ignore]	pam_unix.so nullok
Account-Type: Primary
Account:
	[success=end new_authtok_reqd=done default=ignore]	pam_unix.so
EOF
printf 'auth\t[success=1 default=ignore]\tpam_unix.so nullok\n' > "$T/etc/pam.d/common-auth"
printf '#%%PAM-1.0\nauth requisite pam_nologin.so\n@include common-auth\naccount required pam_permit.so\n' > "$T/etc/pam.d/lightdm"
printf '#%%PAM-1.0\nauth requisite pam_nologin.so\nauth sufficient pam_succeed_if.so user ingroup nopasswdlogin\n@include common-auth\n' > "$T/etc/pam.d/gdm-password"
export WEX_BLANK_PAM_SERVICES="lightdm gdm-password"
if sh "$PAM" status > /dev/null; then bad "pam: status before apply should see nullok"; else ok "pam: status sees the nullok before apply"; fi
sh "$PAM" apply > /dev/null 2>&1; rc=$?
[ $rc != 0 ] && ok "pam: apply refuses while common-auth (here, a stand-in the profile has not regenerated) still says nullok" || bad "pam: apply passed with nullok in common-auth"
printf 'auth\t[success=1 default=ignore]\tpam_unix.so try_first_pass\n' > "$T/etc/pam.d/common-auth"
sh "$PAM" apply > /dev/null 2>&1 && ok "pam: apply (redirected)" || bad "pam: apply failed"
grep -q nullok "$T/usr/share/pam-configs/unix" && bad "pam: the profile still has nullok" || ok "pam: the profile has no nullok"
grep -q 'try_first_pass' "$T/usr/share/pam-configs/unix" && ok "pam: the rest of the profile is as it was" || bad "pam: the profile lost try_first_pass"
[ "$(grep -c nullok "$T/usr/share/wineexplorer/pam-config-unix.distrib")" = 2 ] && ok "pam: Debian's profile is kept outside pam-configs" || bad "pam: the kept profile"
[ ! -e "$T/usr/share/pam-configs/unix.distrib" ] && ok "pam: and no copy of it is in pam-configs (pam-auth-update would read it as a profile)" || bad "pam: a copy in pam-configs"
grep -q 'ingroup nopasswdlogin' "$T/etc/pam.d/lightdm" && ok "pam: a greeter stack without the rule gets it" || bad "pam: lightdm has no group line"
[ "$(grep -c nopasswdlogin "$T/etc/pam.d/lightdm")" = 1 ] && ok "pam: once" || bad "pam: more than one line"
awk '/nopasswdlogin/ { a = NR } /@include common-auth/ { b = NR } END { exit (a && b && a < b) ? 0 : 1 }' "$T/etc/pam.d/lightdm" &&
    ok "pam: the rule comes before common-auth" || bad "pam: the rule is not before common-auth"
[ "$(grep -c nopasswdlogin "$T/etc/pam.d/gdm-password")" = 1 ] && ok "pam: a stack that has the rule is not changed" || bad "pam: gdm-password changed"
sh "$PAM" apply > /dev/null 2>&1 && [ "$(grep -c nopasswdlogin "$T/etc/pam.d/lightdm")" = 1 ] && ok "pam: apply twice changes nothing more" || bad "pam: second apply"
unset WEX_BLANK_ROOT WEX_BLANK_PAM_SERVICES

# ---------------------------------------------------------------- the real machine
if [ "${1:-}" = --real ]; then
    [ "$(id -u)" = 0 ] || { echo "FAIL real: needs root"; exit 1; }
    cleanup() { for u in zzblank1 zzblank2 zzblank3; do userdel -r "$u" 2> /dev/null; done; sh "$PAM" undo > /dev/null 2>&1; }
    trap 'cleanup; rm -rf "$T"' EXIT
    for u in zzblank1 zzblank2 zzblank3; do useradd -m -s /bin/bash "$u"; done
    passwd -d zzblank1 > /dev/null
    echo 'zzblank2:Secret-pass-1' | chpasswd
    sh "$RECONCILE" > /dev/null
    id -nG zzblank1 | grep -qw nopasswdlogin && ok "real: an account with an empty field is in nopasswdlogin" || bad "real: zzblank1 not in the group"
    id -nG zzblank2 | grep -qw nopasswdlogin && bad "real: an account with a password is in the group" || ok "real: an account with a password is not"
    id -nG zzblank3 | grep -qw nopasswdlogin && bad "real: a locked account is in the group" || ok "real: a locked account (field '!') is not"
    echo 'zzblank1:Secret-pass-2' | chpasswd
    sh "$RECONCILE" > /dev/null
    id -nG zzblank1 | grep -qw nopasswdlogin && bad "real: zzblank1 has a password and is still a member" || ok "real: a member who got a password is removed"
    getent group nopasswdlogin | cut -d: -f4 | tr , '\n' | grep -v '^zzblank' | grep -q . &&
        echo "note: other members of nopasswdlogin were left as the rule requires: $(getent group nopasswdlogin | cut -d: -f4)"
    # PAM: apply, no nullok anywhere, survives a reinstall of libpam-runtime
    sh "$PAM" apply > /dev/null 2>&1 && ok "real: wex-blank-pam apply" || bad "real: wex-blank-pam apply failed"
    live_nullok() { grep -rn --exclude='*.pam-old' --exclude='*.dpkg-*' nullok /etc/pam.d; }
    live_nullok > /dev/null 2>&1 && bad "real: nullok in /etc/pam.d after apply: $(live_nullok | head -3)" || ok "real: no nullok in /etc/pam.d"
    if DEBIAN_FRONTEND=noninteractive apt-get install -y --reinstall -o Dpkg::Options::=--force-confnew libpam-runtime > /dev/null 2>&1; then
        live_nullok > /dev/null 2>&1 && bad "real: nullok came back after reinstalling libpam-runtime" || ok "real: no nullok after reinstalling libpam-runtime"
        [ -f /usr/share/wineexplorer/pam-config-unix.distrib ] && grep -q nullok /usr/share/wineexplorer/pam-config-unix.distrib &&
            ok "real: Debian's new profile went to the diversion" || bad "real: no kept profile"
    else
        echo "note: could not reinstall libpam-runtime here; the reinstall check was skipped"
    fi
    sh "$PAM" undo > /dev/null 2>&1
    # the stacks themselves (scripts/pamauth.c): a service that is only common-auth, and one that is the greeter's rule
    # plus common-auth. Before the change the empty password is taken by the first; after, only by the second
    if cc -O1 -o "$T/pamauth" "$HERE/pamauth.c" -lpam 2> /dev/null; then
        printf '@include common-auth\naccount required pam_permit.so\n' > /etc/pam.d/wex-blank-test-common
        printf 'auth sufficient pam_succeed_if.so user ingroup nopasswdlogin\n@include common-auth\naccount required pam_permit.so\n' > /etc/pam.d/wex-blank-test-greeter
        trap 'cleanup; rm -f /etc/pam.d/wex-blank-test-common /etc/pam.d/wex-blank-test-greeter; rm -rf "$T"' EXIT
        passwd -d zzblank1 > /dev/null; sh "$RECONCILE" > /dev/null
        printf '\n' | "$T/pamauth" wex-blank-test-common zzblank1 && ok "baseline: Debian's stack takes an empty password (nullok): the problem the change solves" ||
            bad "baseline: the empty password was refused before the change (is nullok gone already?)"
        sh "$PAM" apply > /dev/null 2>&1 || bad "real: apply (second time)"
        printf '\n' | "$T/pamauth" wex-blank-test-common zzblank1 && bad "real: the common stack (su, sudo, login, ...) takes an empty password" ||
            ok "real: the common stack refuses an empty password"
        printf '\n' | "$T/pamauth" wex-blank-test-greeter zzblank1 && ok "real: the greeter's stack takes a blank account in nopasswdlogin" ||
            bad "real: the greeter's stack refuses a blank account"
        printf 'Secret-pass-1\n' | "$T/pamauth" wex-blank-test-greeter zzblank2 && ok "real: the greeter's stack takes the right password of an ordinary account" ||
            bad "real: the greeter's stack refused a right password"
        printf '\n' | "$T/pamauth" wex-blank-test-greeter zzblank2 && bad "real: the greeter's stack took an empty answer for an account with a password" ||
            ok "real: the greeter's stack refuses an empty answer for an account with a password"
        # the root-run reconcile follows a password set later: the account is no longer let in without one
        echo 'zzblank1:Secret-pass-3' | chpasswd; sh "$RECONCILE" > /dev/null
        printf '\n' | "$T/pamauth" wex-blank-test-greeter zzblank1 && bad "real: the greeter's stack took an empty answer after a password was set" ||
            ok "real: after a password is set the greeter needs it"
    else
        echo "note: no libpam-dev or cc: the stack checks were skipped"
    fi
    sh "$PAM" undo > /dev/null 2>&1
    live_nullok > /dev/null 2>&1 && ok "real: undo puts Debian's profile back (nullok again)" || bad "real: undo left no nullok"
fi

if [ $fail = 0 ]; then echo "blankpw=PASS"; else echo "$fail failure(s)"; exit 1; fi
