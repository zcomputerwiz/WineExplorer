#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Sourced by the X11 and Wayland session launchers. Keep account/prefix setup
# independent of the display backend (the prefix-split work owns that policy).
# the shared library (scripts/lib/wex-common.sh): installed, else next to this script in the repository
for wex_lib in "${WEX_LIB_DIR:-/usr/local/lib/wineexplorer}/wex-common.sh" "$(dirname -- "$(readlink -f -- "$0")")/lib/wex-common.sh"; do [ -r "$wex_lib" ] && break; done
. "$wex_lib"
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
export XDG_CURRENT_DESKTOP=WineExplorer
export XDG_SESSION_DESKTOP=WineExplorer
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}winemenubuilder.exe=d"
export WINEDEBUG="${WINEDEBUG:--all,err+all}"
WINE="${WINE:-/usr/local/bin/wine}"

# Called after the display backend is ready. Keep prefix-split policy shared.
# A user's policy applies after the profile has loaded and the shell is up, as on Windows (not during the logon, where
# a refresh and the session's own start-up compete for session 0): this asks for a refresh through gpupdate (gpsvc
# leaves the request for root's wex-gpupdate.path) once an explorer of this user runs, five seconds later. It runs for
# every normal logon, a local user's too (the local object's user policy); with no policy at all a refresh does
# nothing. Started in the background just before the shell, so the session itself does not wait for it.
wex_refresh_policy_after_logon()
{
    [ "${WEXMODE:-normal}" = normal ] || return 0
    (
        up=0
        for _ in $(seq 1 120); do
            if pgrep -u "$(id -u)" -x explorer.exe >/dev/null 2>&1; then up=1; break; fi
            sleep 1
        done
        # no shell within two minutes (the session failed or ended): nothing to refresh, and no new wineserver is
        # started in a prefix that nobody uses any more
        [ "$up" = 1 ] || exit 0
        sleep 5
        "$WINE" gpupdate.exe /target:user /wait:0 >/dev/null 2>&1
    ) &
}

# Publish only a complete template copy. Interrupted prefixes and failed copies
# stay on disk for recovery; never merge the template into an existing prefix.
wex_init_prefix()
{
    local skel="${WEX_SKEL_PFX:-/etc/skel/.wine}" staging backup
    [ ! -d "$WINEPREFIX/drive_c" ] || return 0
    # no template (a machine without Setup's skeleton): wineboot creates the prefix, as before
    [ -d "$skel/drive_c" ] || {
        echo "Warning: no complete prefix template in $skel; wineboot creates the prefix" >&2
        return 0
    }
    mkdir -p -- "$(dirname "$WINEPREFIX")" || return 1
    if [ -e "$WINEPREFIX" ] || [ -L "$WINEPREFIX" ]; then
        backup=$(mktemp -d "${WINEPREFIX}.backup-$(date +%Y%m%d-%H%M%S).XXXXXX") || return 1
        mv -T -- "$WINEPREFIX" "$backup/prefix" || return 1
        echo "Saved interrupted prefix in $backup/prefix"
    fi
    staging=$(mktemp -d "${WINEPREFIX}.init.XXXXXX") || return 1
    echo "Initializing prefix from $skel (staging in $staging)..."
    if ! cp -a -- "$skel/." "$staging/" || [ ! -d "$staging/drive_c" ]; then
        echo "Prefix copy failed; retained staging directory $staging" >&2
        return 1
    fi
    # -T prevents nesting if a target appears while copying.
    if [ -e "$WINEPREFIX" ] || [ -L "$WINEPREFIX" ] || ! mv -T -- "$staging" "$WINEPREFIX"; then
        echo "Prefix publication failed; retained staging directory $staging" >&2
        return 1
    fi
}

# Stop this prefix's Wine and wait until its server is gone (instead of a fixed sleep). -k fails when no server
# runs, which is fine; only a server that does not go away within 30 s is a failure.
wex_stop_wine()
{
    local server="$(dirname "$WINE")/wineserver"
    timeout -k 2s 10s "$server" -k 2>/dev/null || true
    if ! timeout -k 2s 30s "$server" -w; then
        echo "Warning: Wine did not stop within 30 s for $WINEPREFIX" >&2
        return 1
    fi
}

wex_prepare_prefix()
{
    # Restore the shared view before wineboot updates. Changes take effect only
    # at login, never by replacing files underneath a running desktop.
    if [ -x /usr/local/bin/wex-graphics ] && [ -d "$WINEPREFIX/.wex-graphics" ]; then
        /usr/local/bin/wex-graphics vulkan restore || return 1
    fi
    wex_init_prefix || return 1

    # --- First logon of an account made by Setup: its prefix is a copy of /etc/skel/.wine, built by the setup account,
    # so the registry names that account's profile.  Point it at this user, then let shell32 create this user's profile
    # folders (Desktop, My Documents...) and shell folder entries; seconds instead of building a whole prefix.
    # The profile folder is the plain name, as Wine's WINEUSERNAME: a domain account "DOMAIN\user" (Winbind) gets
    # C:\users\user in its own prefix (docs/DOMAIN_LOGON.md, E).
    TEMPLATE_USER=wexsetup
    PROFILE_USER=${USER##*\\}
    if [ "$PROFILE_USER" != "$TEMPLATE_USER" ] && grep -qs "users\\\\\\\\$TEMPLATE_USER\\\\\\\\" "$WINEPREFIX/user.reg"; then
        echo "Making the template prefix this user's..."
        /usr/local/bin/wex-personalize-prefix "$WINEPREFIX" "$PROFILE_USER" "$TEMPLATE_USER" ||
            echo "warning: wex-personalize-prefix failed (exit $?): the profile may still name $TEMPLATE_USER" >&2
        # Per-user setup only (shell32 DllInstall "U", as Windows' Active Setup); the machine part was done by wineboot.
        "$WINE" regsvr32.exe /s /n /i:U shell32.dll || echo "warning: regsvr32 /n /i:U shell32.dll failed (exit $?): the profile folders may be missing" >&2
        wex_stop_wine || true
    fi

    # --- Prefix update after a Wine (re)install or machine root update.
    # In the split prefix layout, machine updates advance /var/lib/wineexplorer/machine/.update-timestamp.
    MACHINE_ROOT="${WEX_MACHINE_ROOT:-$(wex_machine_root)}"
    if wex_prefix_needs_update "$WINEPREFIX" "$MACHINE_ROOT"; then
        echo "Updating the Wine prefix..."
        "$WINE" wineboot.exe -u || echo "warning: wineboot -u failed (exit $?)" >&2
        wex_stop_wine || true
    fi

    # Safe Mode (docs/SAFE_MODE.md): the steps above are what logging on needs; the optional ones below are
    # skipped, so the account starts with Wine's defaults and the shared graphics view, and nothing is seeded.
    case "${WEXMODE:-normal}" in
        safe|safenet) return 0 ;;
    esac

    # Seed only missing native-runtime choices before starting the desktop. This
    # helper refuses live hives and never launches Wine; user/app overrides win.
    if [ -x /usr/local/bin/wex-components ]; then
        /usr/local/bin/wex-components --apply-user-defaults || \
            echo 'Runtime defaults could not be prepared; existing user settings retained.' >&2
    fi

    # Install project-authored theme definitions without replacing user themes.
    # On split prefixes, themes are already installed in the shared machine root;
    # only copy for standalone/unlinked prefixes when windows directory is real and writable.
    THEME_DATA_DIR="${WEX_THEME_DATA_DIR:-/usr/local/share/wineexplorer/themes}"
    if [ -d "$THEME_DATA_DIR" ] && [ ! -L "$WINEPREFIX/drive_c/windows" ] && [ -w "$WINEPREFIX/drive_c/windows" ]; then
        THEME_PREFIX_DIR="$WINEPREFIX/drive_c/windows/Resources/Themes"
        mkdir -p "$THEME_PREFIX_DIR" || echo 'Warning: theme directory could not be prepared.' >&2
        for theme_file in "$THEME_DATA_DIR/"*.theme; do
            [ -f "$theme_file" ] || continue
            theme_target="$THEME_PREFIX_DIR/$(basename "$theme_file")"
            [ -e "$theme_target" ] || install -m644 "$theme_file" "$theme_target" || echo "Warning: could not install $theme_target" >&2
        done
        for theme_dir in "$THEME_DATA_DIR/"*/; do
            [ -d "$theme_dir" ] || continue
            tname="$(basename "$theme_dir")"
            [ -e "$THEME_PREFIX_DIR/$tname" ] || cp -rf "$theme_dir" "$THEME_PREFIX_DIR/$tname" || echo "Warning: could not install theme $tname" >&2
        done
    fi

    # The default theme, as XP's: Open Luna, which also gives the XP Start menu (the shell takes the Start menu's
    # style from the account's theme, and an account without one gets the Classic Start menu). Applied once, at the
    # first normal logon of an account that never chose a theme (no CurrentTheme in its registry); Display
    # Properties' choice is kept from then on. Wine's own default (its aero.msstyles) is never shown.
    if ! grep -qs '^"CurrentTheme"=' "$WINEPREFIX/user.reg" \
            && [ -f "$WINEPREFIX/drive_c/windows/Resources/Themes/Luna.theme" ] \
            && [ -f "$WINEPREFIX/drive_c/windows/Resources/Themes/Luna/Luna.msstyles" ]; then
        echo "Applying the default theme (Open Luna)..."
        "$WINE" explorer.exe '/theme=C:\windows\Resources\Themes\Luna.theme' \
            || echo "warning: the default theme was not applied (exit $?)" >&2
        wex_stop_wine || true
    fi
    if [ -x /usr/local/bin/wex-graphics ] && [ -d "$WINEPREFIX/.wex-graphics" ]; then
        local graphics_overrides
        if graphics_overrides=$(/usr/local/bin/wex-graphics vulkan prepare); then
            [ -z "$graphics_overrides" ] || export WINEDLLOVERRIDES="$graphics_overrides;$WINEDLLOVERRIDES"
        else
            echo 'Vulkan preparation failed; retaining the shared Wine graphics view.' >&2
            /usr/local/bin/wex-graphics vulkan restore || return 1
        fi
    fi
    return 0
}

# Winlogon's AutoRestartShell (HKLM\Software\Microsoft\Windows NT\CurrentVersion\Winlogon, a DWORD, 1 when the value is
# absent): prints 1 or 0. Read from the prefix's system.reg like the Shell value, without starting Wine.
wex_auto_restart_shell()
{
    local value
    value=$(wex_reg_section "$WINEPREFIX/system.reg" 'Software\Microsoft\Windows NT\CurrentVersion\Winlogon' 2>/dev/null |
        awk '/^"AutoRestartShell"=dword:/ { sub(/^"AutoRestartShell"=dword:/, ""); sub(/[ \t\r].*$/, ""); print; exit }')
    case "$value" in
        "") echo 1 ;;
        *[!0]*) echo 1 ;;
        *) echo 0 ;;
    esac
}

# The file wineboot --end-session and --kill leave before a session ends on purpose (programs/wineboot, Log Off and
# shutdown; removed when the end is refused). The shell ending without it is a crash.
wex_session_ending_marker()
{
    # one name for the prefix (wineboot writes it unconditionally: its caller may lack the login session environment)
    echo "$WINEPREFIX/drive_c/wex-session-ending"
}

# Whether the prefix's wineserver is still running (wineserver -k0 sends no signal and says whether there is one).
wex_wineserver_alive()
{
    "${WEX_WINESERVER:-${WINESERVER:-$(dirname -- "$WINE")/wineserver}}" -k0 2>/dev/null
}

# Whether a shell of this account's desktop is running already (one the user started in the gap, or the supervisor's
# own); explorer.exe of the desktop manager has the command line "explorer.exe /desktop".
wex_shell_running()
{
    local pid env_prefix
    if [ -n "${WEX_SHELL_PGREP:-}" ]; then "$WEX_SHELL_PGREP"; return; fi
    # the X11 session's explorer has "/desktop=...", the Wayland session's "/shell"; of this prefix only (another
    # prefix's explorer of the same account must not hold the supervisor)
    for pid in $(pgrep -u "$(id -u)" -f 'C:.windows.system32.explorer.exe /(desktop|shell)' 2>/dev/null); do
        env_prefix=$(tr '\0' '\n' < "/proc/$pid/environ" 2>/dev/null | sed -n 's/^WINEPREFIX=//p' | head -1)
        [ "${env_prefix:-$HOME/.wine}" = "$WINEPREFIX" ] && return 0
    done
    return 1
}

# Runs the shell as Winlogon does: when it ends while the session lives, it is started again (AutoRestartShell).
#   wex_run_shell COMMAND...
# A shell that ended with the session (Log Off, shutdown) ends the session too: wineboot leaves the marker file
# (wex_session_ending_marker) before it ends one, and the launcher's exit status cannot tell the two apart (it is 0
# after a kill -9 as well). The shell ending without the marker is restarted after WEX_SHELL_RESTART_DELAY (2) seconds,
# but not while another shell of the account runs (it waits for that to end). Crash loop guard: no more than
# WEX_SHELL_RESTARTS (3) restarts in WEX_SHELL_RESTART_WINDOW (60) seconds, then the session ends. A TERM, HUP or INT
# to the session is passed to the shell and ends the session.
wex_run_shell()
{
    local child= rc stopping=0 i now marker max=${WEX_SHELL_RESTARTS:-3} window=${WEX_SHELL_RESTART_WINDOW:-60}
    local -a times=() kept=()
    marker=$(wex_session_ending_marker)
    trap 'stopping=1; [ -z "$child" ] || kill -TERM "$child" 2>/dev/null' TERM HUP INT
    rm -f "$marker"
    while :; do
        "$@" &
        child=$!
        wait "$child"; rc=$?
        while kill -0 "$child" 2>/dev/null; do wait "$child"; rc=$?; done
        child=
        [ "$stopping" = 0 ] || { trap - TERM HUP INT; return "$rc"; }
        # Log Off without logind ends the shell at once after starting wineboot (msgina: ExitWindowsEx, then ExitProcess),
        # and wineboot takes a moment to leave the marker: give it WEX_SHELL_MARKER_WAIT tenths of a second
        for i in $(seq "${WEX_SHELL_MARKER_WAIT:-40}"); do [ -e "$marker" ] && break; sleep 0.1; done
        if [ -e "$marker" ]; then
            echo "The shell ended (status $rc) with the session (the marker was there after $((i / 10)).$((i % 10)) s)."
            trap - TERM HUP INT; return "$rc"
        fi
        if [ "$(wex_auto_restart_shell)" != 1 ]; then
            echo "The shell ended (status $rc); AutoRestartShell is off."; trap - TERM HUP INT; return "$rc"
        fi
        # a shell the user started in the gap owns the desktop: not a crash of ours, and no restart over it
        while wex_shell_running && [ "$stopping" = 0 ] && [ ! -e "$marker" ]; do sleep "${WEX_SHELL_WAIT:-1}"; done
        if [ "$stopping" != 0 ] || [ -e "$marker" ]; then trap - TERM HUP INT; return "$rc"; fi
        now=$SECONDS; kept=()
        for i in "${times[@]}"; do [ $((now - i)) -lt "$window" ] && kept+=("$i"); done
        times=("${kept[@]}")
        if [ "${#times[@]}" -ge "$max" ]; then
            echo "The shell ended (status $rc) $max times in $window seconds; not starting it again."
            # the session stays until it ends on purpose (the marker) or the wineserver goes: the programs on the desktop
            # keep running (unsaved work), and a shell started by hand (Task Manager's New Task) is still possible
            while [ "$stopping" = 0 ] && [ ! -e "$marker" ] && wex_wineserver_alive; do sleep "${WEX_SHELL_WAIT:-1}"; done
            trap - TERM HUP INT; return "$rc"
        fi
        times+=("$now")
        echo "The shell ended (status $rc) while the session runs; starting it again in ${WEX_SHELL_RESTART_DELAY:-2} s (AutoRestartShell)."
        sleep "${WEX_SHELL_RESTART_DELAY:-2}"
    done
}
