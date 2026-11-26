#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# WineExplorer: the PAM side of blank passwords at the logon screen only (docs/BLANK_PASSWORDS.md). Runs as root.
#
#   wex-blank-pam.sh apply    drop nullok from the stacks, make sure the greeter lets the group nopasswdlogin in
#   wex-blank-pam.sh undo     put Debian's profile back (the diversion is removed)
#   wex-blank-pam.sh status   print what is in place; exit 0 if nothing in /etc/pam.d has nullok
#
# Debian 13 generates /etc/pam.d/common-auth with pam-auth-update from /usr/share/pam-configs/unix, whose two auth lines
# say "nullok": an account with an empty password field would then be taken by su, sudo, pkexec, login and every other
# stack that includes common-auth. A hand edit of common-auth is undone by the next pam-auth-update, so the profile is
# diverted (dpkg-divert) and ours, the same profile without nullok, takes its place. A new libpam-runtime then writes
# Debian's profile to /usr/share/wineexplorer/pam-config-unix.distrib and leaves ours: it fails safe (never nullok), at the price of not following later
# changes of Debian's profile (check it when libpam-runtime changes a lot).
#
# The greeter must not trust the group alone: Debian's lightdm stack has
#   auth sufficient pam_succeed_if.so user ingroup nopasswdlogin
# before common-auth, which lets a member in without a password, also one who has a password (the group is kept by the
# reconcile, which can be late or fail). In the services named in WEX_BLANK_PAM_SERVICES (default: lightdm) that exist that
# line is replaced, before the first @include common-auth, by
#   auth [success=ignore default=1] pam_succeed_if.so quiet user ingroup nopasswdlogin
#   auth [success=done default=ignore] pam_unix.so nullok
# A member whose field is empty gets in without a prompt (pam_unix accepts an empty field with nullok), a member who has a
# password must type it, and everyone else falls through to common-auth. Only these services keep nullok. The screen locker,
# when there is one, must call such a service. The greeter is assumed to be lightdm.
# WEX_BLANK_ROOT redirects all paths under a test directory (and skips pam-auth-update and dpkg-divert).
PATH=/usr/sbin:/usr/bin:/sbin:/bin
export PATH

ROOT=${WEX_BLANK_ROOT:-}
PROFILE=$ROOT/usr/share/pam-configs/unix
# where Debian's profile is kept: NOT in /usr/share/pam-configs, which pam-auth-update reads whole (a copy there with
# nullok would be a second profile and put the nullok line back)
DISTRIB=$ROOT/usr/share/wineexplorer/pam-config-unix.distrib
PAMD=$ROOT/etc/pam.d
SERVICES=${WEX_BLANK_PAM_SERVICES:-lightdm}
LINE1='auth [success=ignore default=1] pam_succeed_if.so quiet user ingroup nopasswdlogin'
LINE2='auth [success=done default=ignore] pam_unix.so nullok'

die() { echo "wex-blank-pam: $*" >&2; exit 1; }

diverted() { [ -z "$ROOT" ] && dpkg-divert --list "$PROFILE" 2> /dev/null | grep -q .; }

drop_nullok() {
    # the two auth lines of the profile, with nullok (and the older nullok_secure) removed; nothing else changes
    sed -e 's/ nullok_secure//g' -e 's/ nullok//g' "$1"
}

add_group_line() {
    f=$PAMD/$1
    [ -f "$f" ] || return 0
    if grep -qxF -- "$LINE1" "$f" && grep -qxF -- "$LINE2" "$f" && ! grep -v -xF -e "$LINE1" "$f" | grep -q 'ingroup nopasswdlogin'; then
        return 0
    fi
    # any other rule that lets the group in, and an earlier copy of ours, are taken out; ours go before common-auth
    awk -v l1="$LINE1" -v l2="$LINE2" '
        $0 == l1 || $0 == l2 { next }
        /pam_succeed_if\.so.*ingroup nopasswdlogin/ { next }
        !done && $0 ~ /^@include[ \t]+common-auth/ { print l1; print l2; done = 1 }
        { print }
        END { if (!done) exit 3 }
    ' "$f" > "$f.new" || { rm -f "$f.new"; echo "wex-blank-pam: $1 has no @include common-auth: not changed" >&2; return 1; }
    cat "$f.new" > "$f" && rm -f "$f.new"
    echo "wex-blank-pam: $1 now lets an empty password in only for members of nopasswdlogin"
}

# every file of /etc/pam.d with nullok except the greeter services (and the backups pam-auth-update leaves)
remaining_nullok() {
    set --
    for s in $SERVICES; do set -- "$@" "--exclude=$s"; done
    grep -rn --exclude='*.pam-old' --exclude='*.dpkg-*' --exclude='*~' "$@" nullok "$PAMD" 2> /dev/null
}

case ${1:-} in
apply)
    [ -f "$PROFILE" ] || [ -f "$DISTRIB" ] || die "no $PROFILE: not a Debian PAM layout"
    install -d -m 0755 "$(dirname "$DISTRIB")"
    if [ -z "$ROOT" ] && ! diverted; then
        dpkg-divert --quiet --add --rename --divert "$DISTRIB" "$PROFILE" || die "dpkg-divert failed"
    fi
    if [ -n "$ROOT" ]; then
        [ -f "$DISTRIB" ] || cp -- "$PROFILE" "$DISTRIB"
    fi
    drop_nullok "$DISTRIB" > "$PROFILE.new" || die "cannot write the profile"
    if grep -q nullok "$PROFILE.new"; then rm -f "$PROFILE.new"; die "nullok is still in the profile"; fi
    chmod 0644 "$PROFILE.new"
    mv -- "$PROFILE.new" "$PROFILE"
    if [ -z "$ROOT" ]; then
        # without --force: a common-* file that was edited by hand is not overwritten; then this stops and says so
        out=$(DEBIAN_FRONTEND=noninteractive pam-auth-update --package 2>&1) ||
            die "pam-auth-update refused ($out): resolve the local changes of /etc/pam.d/common-* and run this again"
    fi
    for s in $SERVICES; do add_group_line "$s" || exit 1; done
    if remaining_nullok; then die "nullok is still in $PAMD"; fi
    echo "wex-blank-pam: no stack accepts an empty password except through the greeter's group"
    ;;
undo)
    if [ -n "$ROOT" ]; then
        [ -f "$DISTRIB" ] && mv -f -- "$DISTRIB" "$PROFILE"
    elif diverted; then
        rm -f -- "$PROFILE"
        dpkg-divert --quiet --remove --rename "$PROFILE" || die "dpkg-divert --remove failed"
        out=$(DEBIAN_FRONTEND=noninteractive pam-auth-update --package 2>&1) || die "pam-auth-update refused ($out)"
    fi
    ;;
status)
    diverted && echo "profile: diverted (ours, without nullok)" || echo "profile: Debian's"
    if remaining_nullok; then exit 1; fi
    echo "no nullok in $PAMD"
    ;;
*)
    echo "usage: wex-blank-pam.sh apply|undo|status" >&2
    exit 2
    ;;
esac
