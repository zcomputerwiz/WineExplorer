#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Sourced by the X11 and Wayland session launchers. Keep account/prefix setup
# independent of the display backend (the prefix-split work owns that policy).
export WINEPREFIX="${WINEPREFIX:-$HOME/.wine}"
export XDG_CURRENT_DESKTOP=WineExplorer
export XDG_SESSION_DESKTOP=WineExplorer
export WINEDLLOVERRIDES='mscoree,mshtml=;winemenubuilder.exe=d'
export WINEDEBUG="${WINEDEBUG:--all,err+all}"
WINE="${WINE:-/usr/local/bin/wine}"

# Called after the display backend is ready. Keep prefix-split policy shared.
wex_prepare_prefix()
{
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
    NTDLL=/usr/local/lib/wine/x86_64-unix/ntdll.so
    MACHINE_ROOT="${WEX_MACHINE_ROOT:-/var/lib/wineexplorer/machine}"
    if { [ -f "$NTDLL" ] && { [ ! -e "$WINEPREFIX/.update-timestamp" ] || [ "$NTDLL" -nt "$WINEPREFIX/.update-timestamp" ]; }; } || \
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
        for theme_dir in "$THEME_DATA_DIR/"*/; do
            [ -d "$theme_dir" ] || continue
            tname="$(basename "$theme_dir")"
            [ -e "$THEME_PREFIX_DIR/$tname" ] || cp -rf "$theme_dir" "$THEME_PREFIX_DIR/$tname" 2>/dev/null || true
        done
    fi
}
