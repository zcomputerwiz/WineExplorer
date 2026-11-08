#!/bin/bash
# LightDM greeter: the Windows logon screen (logonui.exe) in the lightdm account's own Wine prefix.
# LightDM runs it as the lightdm account (HOME=/var/lib/lightdm) and passes the protocol pipes in
# LIGHTDM_TO_SERVER_FD / LIGHTDM_FROM_SERVER_FD, which reach logonui.exe through wine and explorer.
# Installed as /usr/local/bin/wex-greeter.sh; prepare its isolated prefix on first use.
# See docs/WINLOGON_AND_DWM.md, phase W2.

# Safe Mode: the GTK greeter, so that logging on needs no Wine (docs/SAFE_MODE.md)
case "$(/usr/local/bin/wex-bootmode 2>/dev/null)" in
    safe|safenet|safecmd)
        echo "=== Safe Mode logon started: $(date) ==="
        exec /usr/sbin/lightdm-gtk-greeter
        ;;
esac

export WINEPREFIX="${WINEPREFIX:-/var/lib/lightdm/.wine-logon}"
export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"   # see wine-explorer-session.sh
export WINEDEBUG="${WINEDEBUG:--all,err+all}"
WINE="${WINE:-/usr/local/bin/wine}"
echo "=== WineExplorer logon started: $(date) ==="   # the output goes to /var/log/lightdm/seat0-greeter.log

# update the prefix after a Wine (re)install before showing anything (as the session script does)
NTDLL=$(ls /usr/local/lib/wine/x86_64-unix/ntdll.so 2>/dev/null)
if [ ! -e "$WINEPREFIX/system.reg" ] || { [ -n "$NTDLL" ] && { [ ! -e "$WINEPREFIX/.update-timestamp" ] || [ "$NTDLL" -nt "$WINEPREFIX/.update-timestamp" ]; }; }; then
    # built-in DLLs as links to the packaged files (docs/PREFIX_SPLIT.md), not 1.7 GB of copies
    env -u DISPLAY WINELINKFAKEDLLS=1 "$WINE" wineboot.exe -u
    "$(dirname "$WINE")/wineserver" -w
fi

RES=$(xrandr 2>/dev/null | sed -nE 's/.*current ([0-9]+) x ([0-9]+).*/\1x\2/p' | head -1)
exec "$WINE" explorer.exe "/desktop=Logon,${RES:-1024x768}" logonui.exe
