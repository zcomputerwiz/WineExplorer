#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
#
# apt-cache.sh setup|status|purge - one Debian package cache for the test machine's builds (docs/TOOLING.md, "Package
# cache"). Run as root in the wexbuild WSL distribution.
#
#   setup   installs apt-cacher-ng, listening on 127.0.0.1:3142 only. All WSL distributions share one network namespace,
#           so wexbuild, wexgpo and Debian reach it at that address, and QEMU guests with user networking at
#           10.0.2.2:3142. Packages are fetched from Debian once and checked against the signed indexes; later builds
#           get them from /var/cache/apt-cacher-ng.
#   status  says whether it answers, and the size of the cache.
#   purge   empties the cache (the next build downloads everything again).
#
# installer/build-iso.sh uses it on its own when it answers (WEX_APT_PROXY=none turns that off): debootstrap, the
# live system's apt and the Vintner Boot package build go through it. Nothing of the proxy reaches the image:
# live-build passes it to apt in the environment only.
set -eu
PORT=3142
CONF=/etc/apt-cacher-ng/acng.conf
CACHE=/var/cache/apt-cacher-ng

answers() { curl -fs -m 3 -o /dev/null "http://127.0.0.1:$PORT/acng-report.html"; }

case ${1:-} in
    setup)
        [ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }
        if ! dpkg -s apt-cacher-ng >/dev/null 2>&1; then
            # "allow HTTP tunnels (CONNECT)": no
            echo 'apt-cacher-ng apt-cacher-ng/tunnelenable boolean false' | debconf-set-selections
            DEBIAN_FRONTEND=noninteractive apt-get install -y -q apt-cacher-ng
        fi
        # this machine only: never a proxy for the network the host is on
        if grep -qE '^[#[:space:]]*BindAddress:' "$CONF"; then
            sed -i -E 's/^[#[:space:]]*BindAddress:.*/BindAddress: 127.0.0.1/' "$CONF"
        else
            echo 'BindAddress: 127.0.0.1' >> "$CONF"
        fi
        grep -qE '^Port:' "$CONF" || echo "Port: $PORT" >> "$CONF"
        systemctl enable apt-cacher-ng >/dev/null 2>&1
        systemctl restart apt-cacher-ng
        for _ in 1 2 3 4 5 6 7 8 9 10; do answers && break; sleep 1; done
        answers || { echo "apt-cacher-ng does not answer on 127.0.0.1:$PORT"; exit 1; }
        echo "package cache: http://127.0.0.1:$PORT ($CACHE)"
        ;;
    status)
        if answers; then echo "answers on http://127.0.0.1:$PORT, cache $(du -sh "$CACHE" 2>/dev/null | cut -f1)"
        else echo "not answering on 127.0.0.1:$PORT"; exit 1; fi
        ;;
    purge)
        [ "$(id -u)" = 0 ] || { echo "run as root"; exit 1; }
        systemctl stop apt-cacher-ng
        find "$CACHE" -mindepth 1 -maxdepth 1 ! -name '_*' -exec rm -rf {} +
        systemctl start apt-cacher-ng
        echo "emptied $CACHE"
        ;;
    *)
        echo "usage: apt-cache.sh setup|status|purge"; exit 2 ;;
esac
