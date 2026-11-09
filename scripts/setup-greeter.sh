#!/bin/bash
# Run as root on the VM: install the Windows logon screen as LightDM's greeter (docs/WINLOGON_AND_DWM.md, phase W2).
#   setup-greeter.sh          install the files and make the lightdm account's Wine prefix
#   setup-greeter.sh use      ... and select it in LightDM (otherwise lightdm-gtk-greeter stays the greeter)
#   setup-greeter.sh gtk      select lightdm-gtk-greeter again
# The prefix belongs to the lightdm service account, so the greeter never runs as root and never sees a user's files.
# It is the account's ~/.wine, the only prefix the system role (wex-sysrun) serves (docs/SAFE_MODE_WORKORDER.md C7).
set -e
cd "$(dirname "$0")"
PREFIX=/var/lib/lightdm/.wine
CONF=/etc/lightdm/lightdm.conf.d/50-wineexplorer.conf

if [ "$1" != gtk ]; then
    install -m 755 wex-greeter.sh /usr/local/bin/wex-greeter.sh
    install -D -m 644 wineexplorer-greeter.desktop /usr/share/xgreeters/wineexplorer-greeter.desktop
    if [ ! -e "$PREFIX" ] && [ -d /var/lib/lightdm/.wine-logon ] && [ ! -L /var/lib/lightdm/.wine-logon ]; then
        mv /var/lib/lightdm/.wine-logon "$PREFIX"   # the earlier name
    fi
    if [ ! -e "$PREFIX/system.reg" ]; then
        echo "Making the logon prefix $PREFIX ..."
        if [ -d "/etc/skel/.wine" ]; then
            cp -a /etc/skel/.wine "$PREFIX"
            chown -R lightdm:lightdm "$PREFIX"
        else
            # no display: wineboot needs none, and the greeter's X server does not exist yet
            runuser -u lightdm -- env -u DISPLAY HOME=/var/lib/lightdm WINEPREFIX="$PREFIX" \
                WINELINKFAKEDLLS=1 WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d" WINEDEBUG=-all /usr/local/bin/wine wineboot.exe -i
            runuser -u lightdm -- env HOME=/var/lib/lightdm WINEPREFIX="$PREFIX" /usr/local/bin/wineserver -w
        fi
    fi
fi

case "$1" in
use) sed -i 's/^greeter-session=.*/greeter-session=wineexplorer-greeter/' "$CONF" ;;
gtk) sed -i 's/^greeter-session=.*/greeter-session=lightdm-gtk-greeter/' "$CONF" ;;
esac
grep '^greeter-session' "$CONF"
