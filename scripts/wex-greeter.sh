#!/bin/bash
# LightDM greeter: the Windows logon screen (logonui.exe) in the lightdm account's own Wine prefix.
# LightDM runs it as the lightdm account (HOME=/var/lib/lightdm) and passes the protocol pipes in
# LIGHTDM_TO_SERVER_FD / LIGHTDM_FROM_SERVER_FD, which reach logonui.exe through wine and explorer.
# Installed as /usr/local/bin/wex-greeter.sh; prepare its isolated prefix on first use.
# See docs/WINLOGON_AND_DWM.md, phase W2.

# The prefix is the account's ~/.wine: the system role (wex-sysrun) serves only <passwd home>/.wine
# (docs/SAFE_MODE_WORKORDER.md C7). The prefix used to be ~/.wine-logon; move it once.
if [ -z "${WINEPREFIX:-}" ]; then
    home=${HOME:-/var/lib/lightdm}
    if [ ! -e "$home/.wine" ] && [ -d "$home/.wine-logon" ] && [ ! -L "$home/.wine-logon" ]; then
        mv "$home/.wine-logon" "$home/.wine" && echo "=== moved the logon prefix to $home/.wine ==="
    fi
    WINEPREFIX=$home/.wine
fi
export WINEPREFIX
export WINEDLLOVERRIDES="mscoree,mshtml=;winemenubuilder.exe=d"   # see wine-explorer-session.sh
export WINEDEBUG="${WINEDEBUG:--all,err+all}"
WINE="${WINE:-/usr/local/bin/wine}"
echo "=== WineExplorer logon started: $(date) ==="   # the output goes to /var/log/lightdm/seat0-greeter.log

# Safe Mode shows the Welcome screen too, as XP does. The GTK greeter, which needs no Wine, is only the last resort
# for a Wine that does not start: that is what Safe Mode, and Safe Mode with Command Prompt above all, must survive
# (docs/SAFE_MODE.md, docs/SAFE_MODE_WORKORDER.md C4).
# (The overrides serve scripts/test_safemode.py; LightDM starts the greeter with a clean environment.)
SAFE=
LIMIT=()
# WEXMODE tells logonui.exe the mode, as the session scripts tell Explorer (it shows the Administrator in Safe Mode)
WEXMODE=$("${WEX_BOOTMODE_HELPER:-/usr/local/bin/wex-bootmode}" 2>/dev/null) || WEXMODE=normal
export WEXMODE
case "$WEXMODE" in
    safe|safenet|safecmd) SAFE=1; LIMIT=(timeout 300) ;;
esac
# Enable VGA Mode (the menu's start option): a normal start at 640x480, as XP's (docs/SAFE_MODE_WORKORDER.md D2)
WEXOPTION=$("${WEX_BOOTMODE_HELPER:-/usr/local/bin/wex-bootmode}" --option 2>/dev/null) || WEXOPTION=
gtk_greeter() {
    echo "=== Safe Mode: $1; starting the GTK greeter instead: $(date) ==="
    exec "${WEX_GTK_GREETER:-/usr/sbin/lightdm-gtk-greeter}"
}
if [ -n "$SAFE" ] && [ ! -x "$WINE" ]; then gtk_greeter "no Wine at $WINE"; fi

# update the prefix after a Wine (re)install before showing anything (as the session script does). A made prefix
# always has user.reg; under the system role its HKLM comes from the machine root and it has no system.reg.
NTDLL=$(ls /usr/local/lib/wine/x86_64-unix/ntdll.so 2>/dev/null)
if [ ! -e "$WINEPREFIX/user.reg" ] || { [ -n "$NTDLL" ] && { [ ! -e "$WINEPREFIX/.update-timestamp" ] || [ "$NTDLL" -nt "$WINEPREFIX/.update-timestamp" ]; }; }; then
    # built-in DLLs as links to the packaged files (docs/PREFIX_SPLIT.md), not 1.7 GB of copies
    "${LIMIT[@]}" env -u DISPLAY WINELINKFAKEDLLS=1 "$WINE" wineboot.exe -u
    "${LIMIT[@]}" "$(dirname "$WINE")/wineserver" -w
fi

# Safe Mode runs at 640x480, as XP's basic display driver does (docs/SAFE_MODE_WORKORDER.md C3), and so does Enable
# VGA Mode (D2). Only this X server changes: the next start's begins at its own mode, and the accounts' display
# profiles stay as they are. The session script does the same (wine-explorer-session.sh), for a session that gets an
# X server of its own.
low_resolution() {
    local output
    output=$(xrandr --current 2>/dev/null | awk '$2 == "connected" && / primary / { print $1; exit }')
    [ -n "$output" ] || output=$(xrandr --current 2>/dev/null | awk '$2 == "connected" { print $1; exit }')
    [ -n "$output" ] || return 0
    xrandr --output "$output" --mode 640x480 2>/dev/null || echo "=== $output offers no 640x480 mode ==="
}
if [ -n "$SAFE" ] || [ "$WEXOPTION" = vga ]; then low_resolution; fi

RES=$(xrandr 2>/dev/null | sed -nE 's/.*current ([0-9]+) x ([0-9]+).*/\1x\2/p' | head -1)
if [ -z "$SAFE" ]; then
    exec "$WINE" explorer.exe "/desktop=Logon,${RES:-1024x768}" logonui.exe
fi

echo "=== Safe Mode logon started: $(date) ==="
if ! timeout 120 env -u DISPLAY "$WINE" cmd.exe /c exit 0 >/dev/null 2>&1; then
    gtk_greeter "Wine does not start in $WINEPREFIX"
fi
# Not exec, so that the GTK greeter can still take over if the logon screen fails. LightDM stops the greeter with
# SIGTERM: pass it on to Wine and end, as the exec'd Wine does in a normal start.
"$WINE" explorer.exe "/desktop=Logon,${RES:-1024x768}" logonui.exe &
logon=$!
trap 'kill -TERM "$logon" 2>/dev/null; wait "$logon"; exit 0' TERM INT
wait "$logon"
status=$?
trap - TERM INT
if [ "$status" -ne 0 ]; then gtk_greeter "the logon screen ended with status $status"; fi
exit 0
