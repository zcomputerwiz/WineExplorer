#!/bin/bash
# WineExplorer session: plays the role of Winlogon.
#
# Windows starts whatever HKLM\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\Shell names (default
# "explorer.exe") on the interactive desktop.  We do the same:
#   - Shell is explorer.exe: start the Explorer desktop manager with "/shell" (taskbar, Start menu, desktop);
#   - anything else: start a plain desktop and run that program in it (like setting a custom Shell on Windows).
# Plain "wine explorer /desktop=Name,WxH" keeps stock Wine behavior; only this session turns the shell on.
#
# The Shell value is read straight from the prefix's system.reg: running any Wine program first (e.g. "wine reg")
# would start wineserver, wineboot and a bare desktop manager before ours and make our explorer lose the race.
LOG="$HOME/.wine-session.log"   # persistent: the runtime dir disappears when the session ends
[ -f "$LOG" ] && mv -f "$LOG" "$LOG.1"
exec > "$LOG" 2>&1
echo "=== WineExplorer session started: $(date) ==="
echo "USER=$USER DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY"

export WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
export XDG_CURRENT_DESKTOP="WineExplorer"
export XDG_SESSION_DESKTOP="WineExplorer"
export WINEDLLOVERRIDES="mscoree,mshtml="
export WINEDEBUG="${WINEDEBUG:--all,err+all}"   # keep the log small; errors only
WINE="${WINE:-/usr/local/bin/wine}"
DESKTOP_NAME="Default"

# Disable screen blanking & DPMS
xset s off -dpms 2>/dev/null || true

# Display size: explicit WIDTH/HEIGHT, else xrandr, else 1920x1080
if [ -z "$WIDTH" ] && command -v xrandr >/dev/null 2>&1; then
    RES=$(xrandr 2>/dev/null | sed -nE 's/.*current ([0-9]+) x ([0-9]+).*/\1 \2/p' | head -1)
    WIDTH=${RES% *}; HEIGHT=${RES#* }
fi
WIDTH="${WIDTH:-1920}"
HEIGHT="${HEIGHT:-1080}"
REFRESH="${REFRESH:-60}"

# --- Prefix update after a Wine (re)install.  Doing it as a side effect of starting explorer kills the session
# (the update runs in the first Wine client and a failure there ends it), so do it up front and start clean.
NTDLL=$(ls /usr/local/lib/wine/x86_64-unix/ntdll.so 2>/dev/null)
if [ -n "$NTDLL" ] && { [ ! -e "$WINEPREFIX/.update-timestamp" ] || [ "$NTDLL" -nt "$WINEPREFIX/.update-timestamp" ]; }; then
    echo "Updating the Wine prefix..."
    "$WINE" wineboot.exe -u
    "$(dirname "$WINE")/wineserver" -k
    sleep 2
fi

# --- Winlogon: which shell?  (no prefix yet = first boot = the default)
SHELL_CMD=""
if [ -r "$WINEPREFIX/system.reg" ]; then
    SHELL_CMD=$(awk '
        /^\[/ { in_key = ($0 ~ /^\[Software\\\\Microsoft\\\\Windows NT\\\\CurrentVersion\\\\Winlogon\]/) }
        in_key && /^"Shell"=/ { sub(/^"Shell"="/, ""); sub(/"[ \t\r]*$/, ""); gsub(/\\\\/, "\\"); print; exit }
    ' "$WINEPREFIX/system.reg")
fi
SHELL_CMD="${SHELL_CMD:-explorer.exe}"
SHELL_BASE=$(basename "${SHELL_CMD//\\//}" | tr 'A-Z' 'a-z')
echo "Winlogon Shell=$SHELL_CMD"

if [ "$SHELL_BASE" = "explorer.exe" ]; then
    SHELL_ARGS=(/shell)
else
    SHELL_ARGS=("$SHELL_CMD")
fi

LAUNCH=("$WINE" explorer.exe "/desktop=${DESKTOP_NAME},${WIDTH}x${HEIGHT}" "${SHELL_ARGS[@]}")

if command -v gamescope >/dev/null 2>&1; then
    echo "Starting under Gamescope (${WIDTH}x${HEIGHT}@${REFRESH}Hz)"
    exec gamescope -W "$WIDTH" -H "$HEIGHT" -r "$REFRESH" -- "${LAUNCH[@]}"
else
    echo "Starting direct (${WIDTH}x${HEIGHT})"
    exec "${LAUNCH[@]}"
fi
