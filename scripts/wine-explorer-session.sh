#!/bin/bash
# WineExplorer session: plays the role of Winlogon.
#
# Windows starts whatever HKLM\Software\Microsoft\Windows NT\CurrentVersion\Winlogon\Shell names (default
# "explorer.exe") on the interactive desktop.  We do the same: read that value from the Wine prefix and
#   - Shell is explorer.exe: start the Explorer desktop manager with its shell (taskbar, Start menu, desktop)
#     enabled for the "Default" desktop;
#   - anything else: start a plain desktop and run that program in it (like setting a custom Shell on Windows).
# Plain "wine explorer /desktop=Name,WxH" keeps stock Wine behavior; only this session turns the shell on.
LOG="${XDG_RUNTIME_DIR:-$HOME}/wine-session.log"
exec > "$LOG" 2>&1
echo "=== WineExplorer session started: $(date) ==="
echo "USER=$USER DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY"

export WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
export XDG_CURRENT_DESKTOP="WineExplorer"
export XDG_SESSION_DESKTOP="WineExplorer"
export WINEDLLOVERRIDES="mscoree,mshtml="
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

# --- Winlogon: which shell?
SHELL_CMD=$("$WINE" reg query 'HKLM\Software\Microsoft\Windows NT\CurrentVersion\Winlogon' /v Shell 2>/dev/null |
            tr -d '\r' | sed -nE 's/^[[:space:]]*Shell[[:space:]]+REG_[A-Z_]+[[:space:]]+(.*)$/\1/p' | head -1)
SHELL_CMD="${SHELL_CMD:-explorer.exe}"
SHELL_BASE=$(basename "${SHELL_CMD//\\//}" | tr 'A-Z' 'a-z')
echo "Winlogon Shell=$SHELL_CMD"

if [ "$SHELL_BASE" = "explorer.exe" ]; then
    "$WINE" reg add "HKCU\\Software\\Wine\\Explorer\\Desktops\\$DESKTOP_NAME" /v EnableShell /t REG_DWORD /d 1 /f
    SHELL_ARGS=()
else
    "$WINE" reg add "HKCU\\Software\\Wine\\Explorer\\Desktops\\$DESKTOP_NAME" /v EnableShell /t REG_DWORD /d 0 /f
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
