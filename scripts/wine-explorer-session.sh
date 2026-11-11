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

# Safe Mode (docs/SAFE_MODE.md). The session keeps its name in every mode, so LightDM records nothing new for the
# account; the mode only decides what starts. With Command Prompt the recovery console replaces the shell, as
# Windows starts its AlternateShell; it needs no Wine.
WEXMODE=$(/usr/local/bin/wex-bootmode 2>/dev/null) || WEXMODE=normal
case "$WEXMODE" in
    safecmd)
        echo "Safe Mode with Command Prompt: starting the recovery console"
        exec /usr/local/bin/wex-safe-terminal.sh
        ;;
    safe|safenet) echo "Safe Mode ($WEXMODE): 640x480, and the saved display mode and optional logon steps are skipped" ;;
    *) WEXMODE=normal ;;
esac
export WEXMODE
# Enable VGA Mode (the menu's start option, docs/SAFE_MODE_WORKORDER.md D2): a normal start at 640x480 with the display
# driver it has, as XP's. It skips the saved display mode, so a mode chosen in Display Properties then replaces it.
WEXOPTION=$(/usr/local/bin/wex-bootmode --option 2>/dev/null) || WEXOPTION=
[ "$WEXOPTION" != vga ] || echo "Enable VGA Mode: 640x480, and the saved display mode is skipped"

# winemenubuilder (started by wineboot) creates a window before our explorer exists, which starts Wine's default
# desktop first; ours then fails to create "Default" and the session ends.  It only exports Linux menu entries.
. "$(dirname "$0")/wine-explorer-session-common.sh"
DESKTOP_NAME="Default"

# Disable screen blanking & DPMS
xset s off -dpms 2>/dev/null || true

# Display size: explicit WIDTH/HEIGHT, else xrandr, else 1920x1080
# Restore the mode accepted in Display Properties. XRandR validates that the
# current output still supports it; a replaced/disconnected monitor keeps its mode.
# Safe Mode runs at 640x480 instead, as XP's basic display driver does (docs/SAFE_MODE_WORKORDER.md C3), and never
# reads or writes the profile; so does Enable VGA Mode (D2), which only skips reading it. wex-greeter.sh has set the
# mode already when the session keeps the greeter's X server.
low_resolution() {
    local output
    output=$(xrandr --current 2>/dev/null | awk '$2 == "connected" && / primary / { print $1; exit }')
    [ -n "$output" ] || output=$(xrandr --current 2>/dev/null | awk '$2 == "connected" { print $1; exit }')
    [ -n "$output" ] || return 0
    xrandr --output "$output" --mode 640x480 2>/dev/null || echo "$output offers no 640x480 mode"
}
if { [ "$WEXMODE" != normal ] || [ "$WEXOPTION" = vga ]; } && [ -z "$WIDTH" ] && command -v xrandr >/dev/null 2>&1; then
    low_resolution
fi
DISPLAY_PROFILE="${XDG_CONFIG_HOME:-$HOME/.config}/wineexplorer/display"
if [ "$WEXMODE" = normal ] && [ "$WEXOPTION" != vga ] && [ -z "$WIDTH" ] && [ -r "$DISPLAY_PROFILE" ] \
        && command -v xrandr >/dev/null 2>&1; then
    read -r SAVED_WIDTH SAVED_HEIGHT SAVED_RATE SAVED_MODE < "$DISPLAY_PROFILE"
    if [[ "$SAVED_WIDTH $SAVED_HEIGHT $SAVED_RATE" =~ ^[0-9]+\ [0-9]+\ [0-9]+$ ]]; then
        OUTPUT=$(xrandr --current | awk '/ connected primary [0-9]+x[0-9]+/ {print $1; exit}')
        [ -n "$OUTPUT" ] || OUTPUT=$(xrandr --current | awk '/ connected [0-9]+x[0-9]+/ {print $1; exit}')
        RATE=$(awk -v rate="$SAVED_RATE" 'BEGIN {printf "%.3f", rate/1000}')
        SAVED_MODE="${SAVED_MODE:-${SAVED_WIDTH}x${SAVED_HEIGHT}}"
        [ -z "$OUTPUT" ] || xrandr --output "$OUTPUT" --mode "$SAVED_MODE" --rate "$RATE" || true
    fi
fi
if [ -z "$WIDTH" ] && command -v xrandr >/dev/null 2>&1; then
    RES=$(xrandr 2>/dev/null | sed -nE 's/.*current ([0-9]+) x ([0-9]+).*/\1 \2/p' | head -1)
    WIDTH=${RES% *}; HEIGHT=${RES#* }
fi
WIDTH="${WIDTH:-1920}"
HEIGHT="${HEIGHT:-1080}"
REFRESH="${REFRESH:-60}"

wex_prepare_prefix

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

# Explorer owns the session's X11 root and manages other runtimes' windows.
# Gamescope owns a different root, so nesting the entire shell under it would
# isolate native Linux and Proton windows. Use Gamescope per application instead.
echo "Starting X11 desktop (${WIDTH}x${HEIGHT})"
exec "${LAUNCH[@]}"
