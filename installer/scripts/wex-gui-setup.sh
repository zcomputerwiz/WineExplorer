#!/bin/bash
# Runner for Stage 2 GUI-mode Setup
set -e

STAGE_FILE="/etc/wineexplorer/setup/stage"
[ -f "$STAGE_FILE" ] || exit 0
STAGE=$(cat "$STAGE_FILE" 2>/dev/null | tr -d ' \r\n')
[ "$STAGE" = "gui" ] || exit 0

echo "=== Starting WineExplorer GUI-mode Setup ==="

# Dismiss plymouth immediately so boot doesn't hang
plymouth --quit 2>/dev/null || true

# Ensure permissions on setup directory and logs
mkdir -p /etc/wineexplorer/setup /var/lib/wexsetup /var/log
chmod -R a+rwX /etc/wineexplorer
touch /var/log/wineexplorer-setup.log
chmod 666 /var/log/wineexplorer-setup.log

# Create dedicated wexsetup user if not already present
if ! id "wexsetup" >/dev/null 2>&1; then
    useradd -r -m -d /var/lib/wexsetup -s /usr/sbin/nologin -G audio,video,render,users wexsetup
fi
chown -R wexsetup:wexsetup /var/lib/wexsetup

# Grant wexsetup sudo rights during setup
echo "wexsetup ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/wexsetup
chmod 440 /etc/sudoers.d/wexsetup

# Setup X display on vt7
export DISPLAY=:0
export XAUTHORITY=/var/lib/wexsetup/.Xauthority

# Start X server if not running
if ! pidof Xorg >/dev/null 2>&1; then
    Xorg :0 vt7 -nolisten tcp -noreset -keeptty -verbose 3 &
    X_PID=$!
    for i in $(seq 1 30); do
        if xset q >/dev/null 2>&1; then break; fi
        sleep 0.5
    done
fi

chvt 7 2>/dev/null || true
xhost +SI:localuser:wexsetup 2>/dev/null || true

# Run setup.exe inside dedicated wine prefix as wexsetup
su -s /bin/bash wexsetup -c '
    export DISPLAY=:0
    export WINEPREFIX=/var/lib/wexsetup/.wine
    export WINEDLLOVERRIDES="mscoree,mshtml="
    export WINEDEBUG=-all
    /usr/local/bin/wine explorer /desktop=Setup,1024x768 /usr/local/bin/setup.exe -newsetup
'

if [ -n "$X_PID" ]; then
    kill "$X_PID" 2>/dev/null || true
fi

echo "=== GUI-mode Setup stage finished ==="

# Ensure stage transitioned to oobe
echo "oobe" > /etc/wineexplorer/setup/stage
touch /etc/wineexplorer/setup/stage-oobe
sync

# Reboot into Stage 3 OOBE
sleep 1
reboot
