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
touch /var/log/wineexplorer-setup.log

# Create dedicated wexsetup user if not already present, ensuring membership in adm
if ! id "wexsetup" >/dev/null 2>&1; then
    useradd -r -m -d /var/lib/wexsetup -s /usr/sbin/nologin -G audio,video,render,users,adm wexsetup
else
    usermod -a -G adm wexsetup 2>/dev/null || true
fi
chown -R wexsetup:wexsetup /var/lib/wexsetup /etc/wineexplorer
chmod 755 /etc/wineexplorer /etc/wineexplorer/setup
chmod 644 /etc/wineexplorer/setup/* 2>/dev/null || true
chown wexsetup:adm /var/log/wineexplorer-setup.log
chmod 660 /var/log/wineexplorer-setup.log

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

# Ensure /etc/skel/.wine is writable by wexsetup
mkdir -p /etc/skel/.wine
chown -R wexsetup:wexsetup /etc/skel/.wine

# Query active X resolution dynamically
RES=$(DISPLAY=:0 xdpyinfo 2>/dev/null | grep 'dimensions:' | awk '{print $2}' || true)
[ -z "$RES" ] && RES=$(DISPLAY=:0 xdotool getdisplaygeometry 2>/dev/null | awk '{print $1"x"$2}' || true)
[ -z "$RES" ] && RES="1024x768"
echo "Detected display resolution: $RES"

# Run setup.exe inside dedicated wine prefix as wexsetup
rm -f /etc/wineexplorer/setup/stage-gui-done
su -s /bin/bash wexsetup -c '
    RES="${1:-1024x768}"
    export DISPLAY=:0
    export WINEPREFIX=/etc/skel/.wine
    export WINEDLLOVERRIDES="winemenubuilder.exe=d"
    export WINEDEBUG=-all
    /usr/local/bin/wine explorer /desktop=Setup,$RES setup.exe -newsetup &
    WINE_PID=$!
    while [ ! -f /etc/wineexplorer/setup/stage-gui-done ]; do
        if ! kill -0 "$WINE_PID" 2>/dev/null; then
            echo "GUI-mode Setup exited without completion; preserving setup for retry." >&2
            exit 1
        fi
        sleep 1
    done
    sleep 1
    wineserver -k 2>/dev/null || true
    wait $WINE_PID 2>/dev/null || true
' bash "$RES"

# Template prefix for new users (AccountsService copies /etc/skel into each new home): their first logon then
# does not build a prefix.  The runner's own profile folder is dropped; Wine creates each user's on first use.
su -s /bin/bash wexsetup -c '
    export WINEPREFIX=/etc/skel/.wine WINELINKFAKEDLLS=1 WINEDLLOVERRIDES="winemenubuilder.exe=d" WINEDEBUG=-all
    /usr/local/bin/wine wineboot -u
    /usr/local/bin/wineserver -w
' || echo "Template prefix generation failed"
rm -rf /etc/skel/.wine/drive_c/users/wexsetup /var/lib/wexsetup/.wine
find /etc/skel/.wine -name "*.tmp" -delete 2>/dev/null || true

# Split prefix into shared machine root (/var/lib/wineexplorer/machine) and per-user symlinks
if [ -x /usr/local/bin/wex-setup-machine.sh ]; then
    # an image packaged with WEX_SESSION0=1 turns the system tier's session 0 on (docs/SYSTEM_TIER_WORK_ORDERS.md)
    # and one packaged with WEX_ELEVATION=1 enforces elevation ("Enforced elevation on an installed machine")
    WEX_SETUP_ENV=()
    [ ! -f /usr/local/share/wineexplorer/session0.enable ] || WEX_SETUP_ENV+=(WEX_SESSION0=1)
    [ ! -f /usr/local/share/wineexplorer/elevation.enable ] || WEX_SETUP_ENV+=(WEX_ELEVATION=1)
    env "${WEX_SETUP_ENV[@]}" /usr/local/bin/wex-setup-machine.sh || echo "Machine root setup failed"
fi

# The built-in Administrator account (docs/SAFE_MODE_WORKORDER.md C4; the package creates it locked) gets the
# Administrator password that setup.exe gave root, as a copy of root's hash. With no password both stay locked:
# an empty password would let every local user su to it.
if getent passwd administrator >/dev/null 2>&1; then
    root_hash=$(getent shadow root | cut -d: -f2)
    case "$root_hash" in
        ''|'*'|'!'*) echo "No Administrator password: the Administrator account stays locked" ;;
        *) printf 'administrator:%s\n' "$root_hash" | chpasswd -e && echo "Administrator account: password set" ;;
    esac
    unset root_hash
fi

chown root:adm /var/log/wineexplorer-setup.log 2>/dev/null || true
chmod 640 /var/log/wineexplorer-setup.log 2>/dev/null || true

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
