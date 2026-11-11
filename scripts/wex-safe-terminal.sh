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
echo "    wex-setup-machine.sh --update      Update the WineExplorer machine root"
echo "    sudo apt-get --fix-broken install  Repair interrupted package installs"
echo "    sudo dpkg --configure -a           Configure unconfigured packages"
echo "    sudo journalctl -xb                Read this start's system log"
echo "    lsblk -f                           List drives and file systems"
echo "    sudo reboot                        Restart the computer"
echo "    exit                               Close this console"
echo ""
echo "================================================================================"
echo ""
EOF

# The console's shape, as XP shows cmd.exe in Safe Mode with Command Prompt (docs/SAFE_MODE_WORKORDER.md C6): at
# the top left, 80 columns wide and as tall as the screen, light gray on black, the scroll bar on the right, titled
# with the program's name only. No window manager runs here, so the size comes from the screen's and a bitmap font's
# (xfonts-base, cells 13 pixels high): 8 pixels wide when 80 columns and the scroll bar fit, else 7 or 6. Safe Mode
# runs at 640x480 (C3), where an 8-pixel font does not fit. Until 2026-11-11 it was 120x34 cells of an 11-point font
# at +40+40, larger than 640x480: the prompt went off the screen once the output scrolled. In the Wayland session
# wexcomp would add a title bar; a window rule for the name wex-recovery-console leaves it out
# (scripts/wayland/rc.xml), so the rows fit there too.
CELL_H=13
CHROME=19   # two internal borders of 2 pixels, the scroll bar (14) and its border
screen_w=0 screen_h=0
read -r screen_w screen_h <<< "$(xdpyinfo 2>/dev/null | sed -n 's/^ *dimensions: *\([0-9]*\)x\([0-9]*\) pixels.*/\1 \2/p' | head -n 1)"
case "$screen_w$screen_h" in ''|*[!0-9]*) screen_w=0 screen_h=0 ;; esac
CELL_W=8
for w in 8 7 6; do
    CELL_W=$w
    [ "$screen_w" -eq 0 ] || [ $((80 * w + CHROME)) -le "$screen_w" ] && break
done
case "$CELL_W" in
    6) FONT=-misc-fixed-medium-r-semicondensed--13-120-75-75-c-60-iso10646-1 ;;
    *) FONT=-misc-fixed-medium-r-normal--13-120-75-75-c-${CELL_W}0-iso10646-1 ;;
esac
ROWS=25
if [ "$screen_h" -gt 0 ]; then ROWS=$(( (screen_h - 4) / CELL_H )); fi

# Run xterm in an interactive loop so an accidental exit does not abruptly end the session
while true; do
    # allowTitleOps: Debian's bashrc would retitle it "user@host: dir" at every prompt
    xterm \
        -name wex-recovery-console \
        -title bash \
        -n bash \
        -xrm 'XTerm*allowTitleOps: false' \
        -bg black \
        -fg '#c0c0c0' \
        -cr '#c0c0c0' \
        -uc \
        -bw 0 \
        -fn "$FONT" \
        -geometry "80x${ROWS}+0+0" \
        -sb -rightbar -sl 5000 \
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
