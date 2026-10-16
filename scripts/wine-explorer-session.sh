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

# winemenubuilder (started by wineboot) creates a window before our explorer exists, which starts Wine's default
# desktop first; ours then fails to create "Default" and the session ends.  It only exports Linux menu entries.
. "$(dirname "$0")/wine-explorer-session-common.sh"
DESKTOP_NAME="Default"

# Disable screen blanking & DPMS
xset s off -dpms 2>/dev/null || true

# Display size: explicit WIDTH/HEIGHT, else xrandr, else 1920x1080
# Restore the mode accepted in Display Properties. XRandR validates that the
# current output still supports it; a replaced/disconnected monitor keeps its mode.
DISPLAY_PROFILE="${XDG_CONFIG_HOME:-$HOME/.config}/wineexplorer/display"
if [ -z "$WIDTH" ] && [ -r "$DISPLAY_PROFILE" ] && command -v xrandr >/dev/null 2>&1; then
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

# Ensure the prefix exists from /etc/skel/.wine template if missing
if [ ! -d "$WINEPREFIX" ] || [ ! -d "$WINEPREFIX/drive_c" ]; then
    if [ -d "/etc/skel/.wine" ]; then
        echo "Initializing prefix from /etc/skel/.wine..."
        cp -a /etc/skel/.wine "$WINEPREFIX"
    fi
fi

# --- First logon of an account made by Setup: its prefix is a copy of /etc/skel/.wine, built by the setup account,
# so the registry names that account's profile.  Point it at this user, then let shell32 create this user's profile
# folders (Desktop, My Documents...) and shell folder entries; seconds instead of building a whole prefix.
TEMPLATE_USER=wexsetup
if [ "$USER" != "$TEMPLATE_USER" ] && grep -qs "users\\\\\\\\$TEMPLATE_USER\\\\\\\\" "$WINEPREFIX/user.reg"; then
    echo "Making the template prefix this user's..."
    sed -i -e 's/\\\\users\\\\'"$TEMPLATE_USER"'\\\\/\\\\users\\\\'"$USER"'\\\\/g' \
           -e 's/\\\\users\\\\'"$TEMPLATE_USER"'"/\\\\users\\\\'"$USER"'"/g' \
        "$WINEPREFIX/user.reg"
    if [ -f "$WINEPREFIX/userdef.reg" ] && [ ! -L "$WINEPREFIX/userdef.reg" ]; then
        sed -i -e 's/\\\\users\\\\'"$TEMPLATE_USER"'\\\\/\\\\users\\\\'"$USER"'\\\\/g' \
               -e 's/\\\\users\\\\'"$TEMPLATE_USER"'"/\\\\users\\\\'"$USER"'"/g' \
            "$WINEPREFIX/userdef.reg"
    fi
    if [ -f "$WINEPREFIX/system.reg" ] && [ ! -L "$WINEPREFIX/system.reg" ]; then
        sed -i -e 's/\\\\users\\\\'"$TEMPLATE_USER"'\\\\/\\\\users\\\\'"$USER"'\\\\/g' \
               -e 's/\\\\users\\\\'"$TEMPLATE_USER"'"/\\\\users\\\\'"$USER"'"/g' \
            "$WINEPREFIX/system.reg"
    fi
    "$WINE" regsvr32.exe /s shell32.dll
    "$(dirname "$WINE")/wineserver" -k
    sleep 1
fi

# --- Prefix update after a Wine (re)install or machine root update.
# In the split prefix layout, machine updates advance /var/lib/wineexplorer/machine/.update-timestamp.
NTDLL=$(ls /usr/local/lib/wine/x86_64-unix/ntdll.so 2>/dev/null)
MACHINE_ROOT="${WEX_MACHINE_ROOT:-/var/lib/wineexplorer/machine}"
if { [ -n "$NTDLL" ] && { [ ! -e "$WINEPREFIX/.update-timestamp" ] || [ "$NTDLL" -nt "$WINEPREFIX/.update-timestamp" ]; }; } || \
   { [ -e "$MACHINE_ROOT/.update-timestamp" ] && [ "$MACHINE_ROOT/.update-timestamp" -nt "$WINEPREFIX/.update-timestamp" ]; }; then
    echo "Updating the Wine prefix..."
    "$WINE" wineboot.exe -u
    "$(dirname "$WINE")/wineserver" -k
    sleep 2
fi

# Install project-authored theme definitions without replacing user themes.
# On split prefixes, themes are already installed in the shared machine root;
# only copy for standalone/unlinked prefixes when windows directory is real and writable.
THEME_DATA_DIR="${WEX_THEME_DATA_DIR:-/usr/local/share/wineexplorer/themes}"
if [ -d "$THEME_DATA_DIR" ] && [ ! -L "$WINEPREFIX/drive_c/windows" ] && [ -w "$WINEPREFIX/drive_c/windows" ]; then
    THEME_PREFIX_DIR="$WINEPREFIX/drive_c/windows/Resources/Themes"
    mkdir -p "$THEME_PREFIX_DIR" 2>/dev/null || true
    for theme_file in "$THEME_DATA_DIR/"*.theme; do
        [ -f "$theme_file" ] || continue
        theme_target="$THEME_PREFIX_DIR/$(basename "$theme_file")"
        [ -e "$theme_target" ] || install -m644 "$theme_file" "$theme_target" 2>/dev/null || true
    done
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

# Explorer owns the session's X11 root and manages other runtimes' windows.
# Gamescope owns a different root, so nesting the entire shell under it would
# isolate native Linux and Proton windows. Use Gamescope per application instead.
echo "Starting X11 desktop (${WIDTH}x${HEIGHT})"
exec "${LAUNCH[@]}"
