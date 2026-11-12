#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# wex-locale-gen [LOCALE]: makes sure glibc has the system locale, as root.
#
# Setup sets LANG through systemd-localed (WexLocaleSet), which on Debian writes /etc/default/locale but generates
# nothing. A locale that was never generated left every program in the C locale ("cannot change locale
# (en_US.UTF-8)"). With no argument the locale is LANG from /etc/default/locale, or else /etc/locale.conf.
#   - The package's postinst runs it for en_US.UTF-8, Setup's default, so the image is built with it.
#   - wex-locale-gen.service runs it at each start before the logon screen and OOBE, for a locale typed in Setup.
# A locale that is already there costs one `locale -a`. One that glibc does not list in SUPPORTED is reported and
# left alone. The paths can be moved for the tests (scripts/test_safemode.py).
set -u
default_file=${WEX_LOCALE_DEFAULT:-/etc/default/locale}
conf_file=${WEX_LOCALE_CONF:-/etc/locale.conf}
gen_file=${WEX_LOCALE_GEN:-/etc/locale.gen}
supported=${WEX_LOCALE_SUPPORTED:-/usr/share/i18n/SUPPORTED}

want=${1:-}
if [ -z "$want" ]; then
    for f in "$default_file" "$conf_file"; do
        [ -r "$f" ] || continue
        want=$(sed -n 's/^[[:space:]]*LANG=//p' "$f" | tail -n 1 | tr -d "\"'")
        [ -z "$want" ] || break
    done
fi
[ -n "$want" ] || exit 0
case "$want" in
    C|POSIX|C.*) exit 0 ;;   # built into glibc
esac

# The name `locale -a` prints: the codeset in lower case without punctuation (en_US.UTF-8 is en_US.utf8)
name=${want%%[.@]*}
rest=${want#"$name"}
codeset=
modifier=
case "$rest" in
    .*) codeset=${rest#.}; codeset=${codeset%%@*} ;;
esac
case "$rest" in
    *@*) modifier=@${rest#*@} ;;
esac
normal=$name
if [ -n "$codeset" ]; then
    normal=$normal.$(printf '%s' "$codeset" | tr -cd '[:alnum:]' | tr '[:upper:]' '[:lower:]')
fi
normal=$normal$modifier
if locale -a 2>/dev/null | grep -qxF -e "$want" -e "$normal"; then
    exit 0
fi

line=$(awk -v w="$want" '$1 == w { print; exit }' "$supported" 2>/dev/null)
if [ -z "$line" ]; then
    echo "wex-locale-gen: glibc cannot generate $want (not in $supported)" >&2
    exit 0
fi
# Turn it on in locale.gen: uncomment its line, or add the line from SUPPORTED
if [ -f "$gen_file" ] && awk -v w="$want" '
        { s = $0; sub(/^#[[:space:]]*/, "", s); split(s, f, /[[:space:]]+/) }
        f[1] == w { found = 1 }
        END { exit !found }' "$gen_file"; then
    awk -v w="$want" '
        { s = $0; sub(/^#[[:space:]]*/, "", s); split(s, f, /[[:space:]]+/)
          if (f[1] == w) print s; else print }' "$gen_file" > "$gen_file.wex-tmp" \
        && mv -f "$gen_file.wex-tmp" "$gen_file"
else
    printf '%s\n' "$line" >> "$gen_file"
fi
echo "wex-locale-gen: generating $want"
locale-gen
