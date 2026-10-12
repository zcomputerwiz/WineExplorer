#!/bin/bash
# Runner for Stage 3 OOBE
set -e

STAGE_FILE="/etc/wineexplorer/setup/stage"
[ -f "$STAGE_FILE" ] || exit 0
STAGE=$(cat "$STAGE_FILE" 2>/dev/null | tr -d ' \r\n')
[ "$STAGE" = "oobe" ] || exit 0

echo "=== Starting WineExplorer Out-of-Box Experience (OOBE) ==="

# Dismiss plymouth immediately so boot doesn't hang
plymouth --quit 2>/dev/null || true

# Ensure permissions on setup directory and logs
mkdir -p /etc/wineexplorer/setup /var/lib/wexsetup /var/log
touch /var/log/wineexplorer-oobe.log

# Ensure dedicated wexsetup user exists, with adm membership
if ! id "wexsetup" >/dev/null 2>&1; then
    useradd -r -m -d /var/lib/wexsetup -s /usr/sbin/nologin -G audio,video,render,users,adm wexsetup
else
    usermod -a -G adm wexsetup 2>/dev/null || true
fi
chown -R wexsetup:wexsetup /var/lib/wexsetup /etc/wineexplorer
chmod 755 /etc/wineexplorer /etc/wineexplorer/setup
chmod 644 /etc/wineexplorer/setup/* 2>/dev/null || true
chown wexsetup:adm /var/log/wineexplorer-oobe.log
chmod 660 /var/log/wineexplorer-oobe.log

export DISPLAY=:0
export XAUTHORITY=/var/lib/wexsetup/.Xauthority

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

# Query active X resolution dynamically
RES=$(DISPLAY=:0 xdpyinfo 2>/dev/null | grep 'dimensions:' | awk '{print $2}' || true)
[ -z "$RES" ] && RES=$(DISPLAY=:0 xdotool getdisplaygeometry 2>/dev/null | awk '{print $1"x"$2}' || true)
[ -z "$RES" ] && RES="1024x768"
echo "Detected display resolution: $RES"

# Run oobe.exe inside dedicated wine prefix as wexsetup
rm -f /etc/wineexplorer/setup/stage-oobe-done
su -s /bin/bash wexsetup -c '
    RES="${1:-1024x768}"
    export DISPLAY=:0
    export WINEPREFIX=/var/lib/wexsetup/.wine
    export WINEDLLOVERRIDES="mscoree,mshtml="
    export WINEDEBUG=-all
    /usr/local/bin/wine explorer /desktop=OOBE,$RES oobe.exe &
    WINE_PID=$!
    while [ ! -f /etc/wineexplorer/setup/stage-oobe-done ]; do
        sleep 1
    done
    sleep 1
    wineserver -k 2>/dev/null || true
    wait $WINE_PID 2>/dev/null || true
' bash "$RES"

if [ -n "$X_PID" ]; then
    kill "$X_PID" 2>/dev/null || true
fi

# Clean up temporary setup user and files
pkill -9 -u wexsetup 2>/dev/null || true
userdel -r -f wexsetup 2>/dev/null || true
rm -rf /var/lib/wexsetup
rm -rf /etc/wineexplorer/setup
rm -f /etc/polkit-1/rules.d/49-wexsetup.rules
rm -f /etc/sudoers.d/wexsetup
mkdir -p /etc/wineexplorer
chmod 755 /etc/wineexplorer 2>/dev/null || true
chown root:adm /var/log/wineexplorer-*.log 2>/dev/null || true
chmod 640 /var/log/wineexplorer-*.log 2>/dev/null || true
systemctl disable wex-setup.service wex-oobe.service 2>/dev/null || true

# Start default display manager
systemctl restart display-manager.service || systemctl restart lightdm.service || true
echo "=== OOBE completed successfully ==="
