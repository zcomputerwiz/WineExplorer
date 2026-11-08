#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Sourced by the X11 and Wayland session launchers. Keep account/prefix setup
# independent of the display backend (the prefix-split work owns that policy).
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
export XDG_CURRENT_DESKTOP=WineExplorer
export XDG_SESSION_DESKTOP=WineExplorer
export WINEDLLOVERRIDES="${WINEDLLOVERRIDES:+$WINEDLLOVERRIDES;}winemenubuilder.exe=d"
export WINEDEBUG="${WINEDEBUG:--all,err+all}"
WINE="${WINE:-/usr/local/bin/wine}"

# Called after the display backend is ready. Keep prefix-split policy shared.
wex_prepare_prefix()
{
    # Restore the shared view before wineboot updates. Changes take effect only
    # at login, never by replacing files underneath a running desktop.
    if [ -x /usr/local/bin/wex-graphics ] && [ -d "$WINEPREFIX/.wex-graphics" ]; then
        /usr/local/bin/wex-graphics vulkan restore || return 1
    fi
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
        /usr/local/bin/wex-personalize-prefix "$WINEPREFIX" "$USER" "$TEMPLATE_USER"
        # Per-user setup only (shell32 DllInstall "U", as Windows' Active Setup); the machine part was done by wineboot.
        "$WINE" regsvr32.exe /s /n /i:U shell32.dll || echo "warning: regsvr32 /n /i:U shell32.dll failed (exit $?): the profile folders may be missing" >&2
        "$(dirname "$WINE")/wineserver" -k
        sleep 1
    fi

    # --- Prefix update after a Wine (re)install or machine root update.
    # In the split prefix layout, machine updates advance /var/lib/wineexplorer/machine/.update-timestamp.
    NTDLL=/usr/local/lib/wine/x86_64-unix/ntdll.so
    MACHINE_ROOT="${WEX_MACHINE_ROOT:-/var/lib/wineexplorer/machine}"
    if { [ -f "$NTDLL" ] && { [ ! -e "$WINEPREFIX/.update-timestamp" ] || [ "$NTDLL" -nt "$WINEPREFIX/.update-timestamp" ]; }; } || \
       { [ -e "$MACHINE_ROOT/.update-timestamp" ] && [ "$MACHINE_ROOT/.update-timestamp" -nt "$WINEPREFIX/.update-timestamp" ]; }; then
        echo "Updating the Wine prefix..."
        "$WINE" wineboot.exe -u
        "$(dirname "$WINE")/wineserver" -k
        sleep 2
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
        mkdir -p "$THEME_PREFIX_DIR" 2>/dev/null || true
        for theme_file in "$THEME_DATA_DIR/"*.theme; do
            [ -f "$theme_file" ] || continue
            theme_target="$THEME_PREFIX_DIR/$(basename "$theme_file")"
            [ -e "$theme_target" ] || install -m644 "$theme_file" "$theme_target" 2>/dev/null || true
        done
        for theme_dir in "$THEME_DATA_DIR/"*/; do
            [ -d "$theme_dir" ] || continue
            tname="$(basename "$theme_dir")"
            [ -e "$THEME_PREFIX_DIR/$tname" ] || cp -rf "$theme_dir" "$THEME_PREFIX_DIR/$tname" 2>/dev/null || true
        done
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
}
