#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# wexgpo-provision.sh PACKAGE_LIST - make a fresh Debian 13 WSL distribution a build and test machine like
# wexbuild (docs/GPO_LAB.md, "The build machine"). As root, once, in the new distribution; PACKAGE_LIST is
# wexbuild's "apt-mark showmanual" (the local package wineexplorer is left out). Then restart the distribution
# (wsl --terminate) so that systemd runs.
set -euo pipefail
LIST=${1:?package list}
export DEBIAN_FRONTEND=noninteractive

printf '[boot]\nsystemd=true\n\n[user]\ndefault=root\n' > /etc/wsl.conf
# the suites run the build in /root/WE-prefetch as their test users (wexbuild has /root at 755 too)
chmod 755 /root
# the broker's unit test stands in UID 1000 for the caller (wexbuild has its first user there); a distribution
# installed with --no-launch has no user at all
id -u 1000 >/dev/null 2>&1 || useradd -m -u 1000 -s /bin/bash wexdev
dpkg --add-architecture i386
apt-get update -q
mapfile -t PKGS < <(sed 's/\r$//' "$LIST" | grep -v -x -e wineexplorer -e '' )
# all at once; if that fails, one by one, and say which did not install
# libxdamage1: the X11 composer (wexhost compose.c) loads libXdamage.so.1 and is unavailable without it
if ! apt-get install -y -q "${PKGS[@]}" perl samba-common-bin libxdamage1 > /root/provision-apt.log 2>&1; then
    for p in "${PKGS[@]}" perl libxdamage1; do
        apt-get install -y -q "$p" >> /root/provision-apt.log 2>&1 || echo "not installed: $p"
    done
fi
git config --global --add safe.directory '*'
# ccache with wexbuild's settings (docs/TOOLING.md, "Builds"; scripts/lab/wex-make.sh uses it when installed): one
# cache for the machine's trees under /root, which share entries across trees (base_dir, hash_dir) and survive a
# fresh checkout's new file times (sloppiness)
apt-get install -y -q ccache >> /root/provision-apt.log 2>&1 || echo "not installed: ccache"
install -d -m 0755 /var/cache/wex-ccache
[ -s /var/cache/wex-ccache/ccache.conf ] ||
    printf 'max_size = 40G\nbase_dir = /root\nhash_dir = false\nsloppiness = include_file_mtime,include_file_ctime\n' \
        > /var/cache/wex-ccache/ccache.conf
# Wine Mono and Gecko where the /usr/local build finds them; without them a machine-root wineboot --init waits 5
# minutes on the Mono download dialog (found by the system tier session, 2026-11-15)
REPO=$(cd "$(dirname "$LIST")/../.." 2>/dev/null && pwd)
[ -f "${WEX_REPO:-$REPO}/installer/runtime-assets.json" ] &&
    bash <(sed 's/\r$//' "$(dirname "${BASH_SOURCE[0]}")/wexgpo-wine-addons.sh") "${WEX_REPO:-$REPO}" ||
    echo "run scripts/lab/wexgpo-wine-addons.sh REPO yourself (Wine Mono and Gecko)"
echo "PROVISION-DONE $(dpkg -l | grep -c '^ii') packages"
