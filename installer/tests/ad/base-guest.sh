#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# Runs INSIDE an installed session 0 machine as root, once per installed disk (machine.sh with WEX_AD_BASELINE=make;
# docs/GPO_LAB.md, "The lab's base disk"). It installs what the lab's guest scripts would otherwise each install on
# every run, and stops: the disk it leaves is the read-only base every guest's overlay is forked from.
#   - the packages of machine-guest.sh, join-guest.sh and join-ps-guest.sh (their installs then find them present);
#   - the PowerShell 7 release archives of join-ps-guest.sh, checked against the digests in that script
#     (/root/ad-base-ps.sh, a copy of it), kept as /root/pwsh.zip and /root/pwsh32.zip, where it looks first.
# Nothing domain-specific happens here: no lab network, no smb.conf, no join. One line per step, "ok NAME" or
# "not ok NAME: detail", then AD-BASE-DONE.
set -uo pipefail
export DEBIAN_FRONTEND=noninteractive

ok() { echo "ok $1"; }
notok() { echo "not ok $1: $(printf '%s' "$2" | tr '\n' ' ' | cut -c1-240)"; }
finish() { rm -f /root/ad-base-ps.sh; echo "AD-BASE-DONE"; exit 0; }

# the three guests' packages together (keep in step with their install lines)
PKGS=(samba winbind libnss-winbind libpam-winbind krb5-user dnsutils ldap-utils libsasl2-modules-gssapi-mit
      xdotool unzip curl)
t=$SECONDS
if apt-get update > /root/ad-apt.log 2>&1 && apt-get install -y "${PKGS[@]}" >> /root/ad-apt.log 2>&1; then
    ok "packages_installed ($((SECONDS - t)) s)"
else
    notok packages_installed "$(tail -3 /root/ad-apt.log)"; finish
fi

# the release archives, with the URL and digest join-ps-guest.sh names (one place for them)
eval "$(grep -E '^PS(32)?_(URL|SHA)=' /root/ad-base-ps.sh)"
fetch() {   # fetch URL SHA FILE
    curl -fsSL --retry 3 -o "$3" "$1" 2> "$3.err" || return 1
    [ "$(sha256sum "$3" | cut -d' ' -f1)" = "$2" ] || { echo "digest differs" > "$3.err"; return 1; }
    rm -f "$3.err"
}
if [ -n "${PS_URL:-}" ] && [ -n "${PS32_URL:-}" ]; then
    t=$SECONDS
    if fetch "$PS_URL" "$PS_SHA" /root/pwsh.zip && fetch "$PS32_URL" "$PS32_SHA" /root/pwsh32.zip; then
        ok "pwsh_archives ($((SECONDS - t)) s)"
    else
        rm -f /root/pwsh.zip /root/pwsh32.zip
        notok pwsh_archives "$(cat /root/pwsh*.zip.err 2>/dev/null)"
    fi
else
    notok pwsh_archives "no PS_URL/PS32_URL in join-ps-guest.sh"
fi
apt-get clean
finish
