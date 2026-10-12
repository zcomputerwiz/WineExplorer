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
chmod -R a+rwX /etc/wineexplorer
touch /var/log/wineexplorer-oobe.log
chmod 666 /var/log/wineexplorer-oobe.log

# Ensure dedicated wexsetup user exists
if ! id "wexsetup" >/dev/null 2>&1; then
    useradd -r -m -d /var/lib/wexsetup -s /usr/sbin/nologin -G audio,video,render,users wexsetup
fi
chown -R wexsetup:wexsetup /var/lib/wexsetup

# Grant wexsetup sudo rights during OOBE
echo "wexsetup ALL=(ALL) NOPASSWD: ALL" > /etc/sudoers.d/wexsetup
chmod 440 /etc/sudoers.d/wexsetup

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

# Run oobe.exe inside dedicated wine prefix as wexsetup
su -s /bin/bash wexsetup -c '
    export DISPLAY=:0
    export WINEPREFIX=/var/lib/wexsetup/.wine
    export WINEDLLOVERRIDES="mscoree,mshtml="
    export WINEDEBUG=-all
    /usr/local/bin/wine explorer /desktop=OOBE,1024x768 /usr/local/bin/oobe.exe
'

if [ -n "$X_PID" ]; then
    kill "$X_PID" 2>/dev/null || true
fi

# Clean up setup flags and temporary setup user
rm -f /etc/wineexplorer/setup/stage /etc/wineexplorer/setup/stage-oobe
userdel -r wexsetup 2>/dev/null || true
rm -f /etc/polkit-1/rules.d/49-wexsetup.rules
rm -f /etc/sudoers.d/wexsetup

# Ensure wineuser exists with default password and groups
if ! id "wineuser" >/dev/null 2>&1; then
    useradd -m -s /bin/bash -G sudo,audio,video,render,users wineuser 2>/dev/null || true
    echo "wineuser:wineuser" | chpasswd 2>/dev/null || true
fi

# Ensure wineuser home and initial prefix exist
mkhomedir_helper wineuser 2>/dev/null || true
if [ ! -d "/home/wineuser/.wine" ] && [ -d "/etc/skel/.wine" ]; then
    cp -a /etc/skel/.wine /home/wineuser/.wine 2>/dev/null || true
fi
chown -R wineuser:wineuser /home/wineuser 2>/dev/null || true

# Configure GDM autologin for wineuser
mkdir -p /etc/gdm3
cat << 'EOF' > /etc/gdm3/daemon.conf
# GDM configuration for WineExplorer
[daemon]
WaylandEnable=false
AutomaticLoginEnable=true
AutomaticLogin=wineuser
DefaultSession=wine-explorer

[security]

[xdmcp]

[chooser]

[debug]
EOF

# Configure AccountsService
mkdir -p /var/lib/AccountsService/users
cat << 'EOF' > /var/lib/AccountsService/users/wineuser
[User]
Language=
XSession=wine-explorer
Session=wine-explorer
SystemAccount=false
EOF

# Start default display manager
systemctl restart display-manager.service || systemctl restart gdm3.service || true
echo "=== OOBE completed successfully ==="
