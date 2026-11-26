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

# the shared library (scripts/lib/wex-common.sh): installed, else next to this script in the repository
for wex_lib in "${WEX_LIB_DIR:-/usr/local/lib/wineexplorer}/wex-common.sh" "$(dirname -- "$(readlink -f -- "$0")")/lib/wex-common.sh"; do [ -r "$wex_lib" ] && break; done
. "$wex_lib"

# Safe Mode (docs/SAFE_MODE.md). The session keeps its name in every mode, so LightDM records nothing new for the
# account; the mode only decides what starts. With Command Prompt the recovery console replaces the shell, as
# Windows starts its AlternateShell; it needs no Wine.
WEXMODE=$("${WEX_BOOTMODE_HELPER:-/usr/local/bin/wex-bootmode}" 2>/dev/null) || WEXMODE=normal
# A Safe Mode logon leaves the account's session choice (X11 or Wayland) as it was: LightDM has just saved this
# session as the choice, and the snapshot taken before LightDM started goes back (wex-session-choice)
case "$WEXMODE" in
    safe|safenet|safecmd) "${WEX_SESSION_CHOICE:-/usr/local/bin/wex-session-choice}" --restore || true ;;
esac
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
WEXOPTION=$("${WEX_BOOTMODE_HELPER:-/usr/local/bin/wex-bootmode}" --option 2>/dev/null) || WEXOPTION=
[ "$WEXOPTION" != vga ] || echo "Enable VGA Mode: 640x480, and the saved display mode is skipped"

# winemenubuilder (started by wineboot) creates a window before our explorer exists, which starts Wine's default
# desktop first; ours then fails to create "Default" and the session ends.  It only exports Linux menu entries.
. "$(dirname "$0")/wine-explorer-session-common.sh" || exit 1
DESKTOP_NAME="Default"

# Disable screen blanking & DPMS
xset s off -dpms 2>/dev/null || true

# Display size: explicit WIDTH/HEIGHT, else xrandr, else 1920x1080
# Restore the mode accepted in Display Properties. XRandR validates that the
# current output still supports it; a replaced/disconnected monitor keeps its mode.
# Safe Mode runs at 640x480 instead, as XP's basic display driver does (docs/SAFE_MODE_WORKORDER.md C3), and never
# reads or writes the profile; so does Enable VGA Mode (D2), which only skips reading it. wex-greeter.sh has set the
# mode already when the session keeps the greeter's X server.
if { [ "$WEXMODE" != normal ] || [ "$WEXOPTION" = vga ]; } && [ -z "$WIDTH" ] && command -v xrandr >/dev/null 2>&1; then
    wex_low_resolution
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

wex_prepare_prefix || { echo 'Session setup failed.' >&2; exit 1; }

# --- Winlogon: which shell?  (no prefix yet = first boot = the default)
SHELL_CMD=""
if [ -r "$WINEPREFIX/system.reg" ]; then
    SHELL_CMD=$(wex_reg_section "$WINEPREFIX/system.reg" 'Software\Microsoft\Windows NT\CurrentVersion\Winlogon' 2>/dev/null | awk '
        /^"Shell"=/ { sub(/^"Shell"="/, ""); sub(/"[ \t\r]*$/, ""); gsub(/\\\\/, "\\"); print; exit }
    ')
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
wex_refresh_policy_after_logon

# Safe Mode, an administrator, Explorer as the shell: the desktop runs elevated, as on Windows, where User Account
# Control does not run in Safe Mode (docs/SAFE_MODE_ELEVATED_DESKTOP.md). The launcher checks Safe Mode and the
# account again, and the server once more; when it refuses (77), the desktop starts as usual.
SYSRUN="${WEX_SYSRUN:-/usr/local/libexec/wineexplorer/wex-sysrun}"
IS_ADMIN="${WEX_IS_ADMIN_HELPER:-/usr/local/libexec/wineexplorer/wex-is-admin}"
if [ "$WEXMODE" != normal ] && [ "$SHELL_BASE" = explorer.exe ] && [ -x "$SYSRUN" ] && [ -x "$IS_ADMIN" ] &&
        "$IS_ADMIN" "$USER" >/dev/null 2>&1; then
    echo "Safe Mode: the administrator's desktop runs elevated"
    started=$SECONDS
    "$SYSRUN" safe-shell "$WIDTH" "$HEIGHT"
    rc=$?
    # a refusal (77), or a desktop that ended at once (the server ended it, it failed to start): the usual desktop,
    # so that Safe Mode always gives one; a desktop that ran is the end of the session
    if [ "$rc" != 77 ] && [ $((SECONDS - started)) -ge 15 ]; then exit "$rc"; fi
    echo "Safe Mode: no elevated desktop (status $rc); starting it as usual"
fi
# Winlogon starts the shell again when it ends alone (AutoRestartShell); the session ends with the shell otherwise
wex_run_shell "${LAUNCH[@]}"
exit $?
