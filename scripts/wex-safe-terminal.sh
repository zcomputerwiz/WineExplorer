#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# WineExplorer Safe Mode with Command Prompt session launcher.
# Provides a minimal, robust administrative recovery environment without Wine.

# Prevent screen blanking and power management during recovery
xset s off -dpms 2>/dev/null || true

# Solid black background matching Windows Safe Mode
xsetroot -solid black 2>/dev/null || true

# Clean Wine environment variables to avoid leaking into recovery shell
unset WINEPREFIX WINEDLLOVERRIDES WINEDEBUG WINELOADER

# Set up temporary rcfile for xterm to display recovery banner and load bashrc
BANNER_RC=$(mktemp "${TMPDIR:-/tmp}/wex-safebanner.XXXXXX")
trap 'rm -f -- "$BANNER_RC"' EXIT

cat << 'EOF' > "$BANNER_RC"
# Source standard bash environment
if [ -f /etc/bash.bashrc ]; then
    . /etc/bash.bashrc
fi
if [ -f "$HOME/.bashrc" ]; then
    . "$HOME/.bashrc"
fi

# Print recovery console banner
echo ""
echo "================================================================================"
echo "  WineExplorer Recovery Console [Safe Mode with Command Prompt]"
echo "================================================================================"
echo "  Host: $(uname -n) | Kernel: $(uname -r) | User: $USER"
echo "================================================================================"
echo ""
echo "  Administrative Recovery Commands:"
echo "    wex-setup-machine.sh --update    Rebuild/update WineExplorer machine root"
echo "    sudo apt-get --fix-broken install Repair broken/interrupted package dependencies"
echo "    sudo dpkg --configure -a         Configure any unconfigured deb packages"
echo "    sudo journalctl -xb              Inspect boot logs and system errors"
echo "    lsblk -f                         Display storage drives and filesystems"
echo "    sudo reboot                      Reboot into normal mode"
echo "    exit                             Close terminal and return to login screen"
echo ""
echo "================================================================================"
echo ""
EOF

# Find available monospace font
FONT="DejaVu Sans Mono"
if ! fc-list : family | grep -qi "DejaVu Sans Mono"; then
    FONT="monospace"
fi

# Run xterm in an interactive loop so an accidental exit does not abruptly end the session
while true; do
    xterm \
        -title "Command Prompt (Safe Mode) - $USER" \
        -bg black \
        -fg white \
        -cr white \
        -bd gray30 \
        -fa "$FONT" \
        -fs 11 \
        -geometry 120x34+40+40 \
        -sb -sl 5000 \
        -e bash --rcfile "$BANNER_RC" -i

    STATUS=$?
    # Solid black root maintained
    xsetroot -solid black 2>/dev/null || true

    # Terminal exited: prompt whether to reopen, reboot, or log out
    # If xmessage or zenity is available, show simple prompt; otherwise fall back to exit
    if command -v zenity >/dev/null 2>&1; then
        ACTION=$(zenity --list --title="Safe Mode Recovery" \
            --text="The command prompt session has ended.\nSelect an action:" \
            --radiolist --column="Select" --column="Action" \
            TRUE "Open Command Prompt" \
            FALSE "Restart System (Normal Mode)" \
            FALSE "Log Out" \
            --height=240 --width=360 2>/dev/null)
        case "$ACTION" in
            "Open Command Prompt")
                continue
                ;;
            "Restart System (Normal Mode)")
                sudo reboot || reboot
                break
                ;;
            *)
                break
                ;;
        esac
    else
        break
    fi
done

exit 0
