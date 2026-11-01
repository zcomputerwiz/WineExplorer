#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Setup handler for WineExplorer Safe Mode with Command Prompt.
# Invoked early in boot before display-manager.service.
set -e

STATE_DIR="/run/wex-safemode"
OVERRIDE_CONF="/etc/lightdm/lightdm.conf.d/99-safemode.conf"
PAM_LIGHTDM="/etc/pam.d/lightdm"
ACC_DIR="/var/lib/AccountsService/users"

is_safemode_cmdline() {
    [ -n "$WEXMODE_FORCE_SAFECMD" ] || { [ -r /proc/cmdline ] && grep -qw "wexmode=safecmd" /proc/cmdline; }
}

clean_safemode() {
    # Remove LightDM override
    rm -f "$OVERRIDE_CONF"

    # Restore any backed up autologin configs
    if [ -d "$STATE_DIR" ]; then
        for f in "$STATE_DIR"/*.conf.bak; do
            if [ -f "$f" ]; then
                orig_name=$(basename "$f" .bak)
                mv -f "$f" "/etc/lightdm/lightdm.conf.d/$orig_name" 2>/dev/null || true
            fi
        done
    fi

    # Clean PAM lightdm rule
    if [ -f "$PAM_LIGHTDM" ]; then
        sed -i '/wex-safemode-pam-check\.sh/d' "$PAM_LIGHTDM" 2>/dev/null || true
    fi

    # Restore AccountsService state for regular users
    if [ -d "$ACC_DIR" ]; then
        # First restore backed up files if any
        if [ -d "$STATE_DIR" ]; then
            for orig in "$STATE_DIR"/orig-*; do
                if [ -f "$orig" ]; then
                    u=$(basename "$orig" | sed 's/^orig-//')
                    cp -f "$orig" "$ACC_DIR/$u" 2>/dev/null || true
                fi
            done
        fi

        # Ensure all regular users have SystemAccount=false and wine-explorer session
        while IFS=: read -r u _ uid _ _ _ _; do
            [ "$uid" -ge 1000 ] 2>/dev/null || continue
            [ "$u" = "nobody" ] && continue
            if [ -f "$ACC_DIR/$u" ]; then
                sed -i 's/^SystemAccount=true/SystemAccount=false/' "$ACC_DIR/$u" 2>/dev/null || true
                sed -i 's/^Session=wex-safe-terminal/Session=wine-explorer/' "$ACC_DIR/$u" 2>/dev/null || true
                sed -i 's/^XSession=wex-safe-terminal/XSession=wine-explorer/' "$ACC_DIR/$u" 2>/dev/null || true
            fi
        done < /etc/passwd

        if systemctl is-active --quiet accounts-daemon.service 2>/dev/null; then
            systemctl restart accounts-daemon.service 2>/dev/null || true
        fi
    fi

    rm -rf "$STATE_DIR"
}

start_safemode() {
    if ! is_safemode_cmdline; then
        # Normal boot: ensure no stale Safe Mode configuration remains
        clean_safemode
        exit 0
    fi

    echo "wex-safemode: Booting in Safe Mode with Command Prompt (wexmode=safecmd)"
    mkdir -p "$STATE_DIR"
    touch "$STATE_DIR/active"

    # 1. Back up and disable existing autologin confs if present
    for conf in /etc/lightdm/lightdm.conf.d/*autologin*.conf; do
        if [ -f "$conf" ]; then
            mv -f "$conf" "$STATE_DIR/$(basename "$conf").bak"
        fi
    done

    # 2. Write LightDM Safe Mode override (native GTK greeter, no Wine, no autologin)
    mkdir -p /etc/lightdm/lightdm.conf.d
    cat << 'EOF' > "$OVERRIDE_CONF"
# WineExplorer Safe Mode with Command Prompt override
[Seat:*]
greeter-session=lightdm-gtk-greeter
user-session=wex-safe-terminal
autologin-user=
autologin-session=wex-safe-terminal
allow-guest=false
EOF

    # 3. Restrict AccountsService user list to sudoers only
    mkdir -p "$ACC_DIR"
    while IFS=: read -r u _ uid _ _ _ _; do
        [ "$uid" -ge 1000 ] 2>/dev/null || continue
        [ "$u" = "nobody" ] && continue

        # Back up existing AccountsService entry if present
        if [ -f "$ACC_DIR/$u" ] && [ ! -f "$STATE_DIR/orig-$u" ]; then
            cp -f "$ACC_DIR/$u" "$STATE_DIR/orig-$u"
        fi

        # Check sudo membership
        if id -nG "$u" 2>/dev/null | grep -qw "sudo"; then
            # Sudoer: visible in greeter, launches Safe Mode terminal session
            cat << EOF > "$ACC_DIR/$u"
[User]
Session=wex-safe-terminal
XSession=wex-safe-terminal
SystemAccount=false
EOF
        else
            # Non-sudoer: mark as SystemAccount so LightDM greeter hides the user
            cat << EOF > "$ACC_DIR/$u"
[User]
Session=wine-explorer
XSession=wine-explorer
SystemAccount=true
EOF
        fi
    done < /etc/passwd

    # Refresh accounts-daemon if running so LightDM picks up the changes immediately
    if systemctl is-active --quiet accounts-daemon.service 2>/dev/null; then
        systemctl restart accounts-daemon.service 2>/dev/null || true
    fi

    # 4. Configure PAM authentication boundary for LightDM
    if [ -f "$PAM_LIGHTDM" ] && ! grep -q "wex-safemode-pam-check.sh" "$PAM_LIGHTDM"; then
        sed -i '1s|^|auth requisite pam_exec.so quiet /usr/local/sbin/wex-safemode-pam-check.sh\n|' "$PAM_LIGHTDM"
    fi
}

case "${1:-start}" in
    start)
        start_safemode
        ;;
    stop|clean)
        clean_safemode
        ;;
    *)
        echo "Usage: $0 {start|stop|clean}"
        exit 1
        ;;
esac

exit 0
