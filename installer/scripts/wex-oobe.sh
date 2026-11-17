#!/bin/bash
# Runner for Stage 3 OOBE
set -e

source "$(dirname -- "${BASH_SOURCE[0]}")/wex-setup-common.sh"

STAGE_FILE="/etc/wineexplorer/setup/stage"
[ -f "$STAGE_FILE" ] || exit 0
STAGE=$(cat "$STAGE_FILE" 2>/dev/null | tr -d ' \r\n')
[ "$STAGE" = "oobe" ] || exit 0

echo "=== Starting WineExplorer Out-of-Box Experience (OOBE) ==="

COORDINATOR_PID=""
X_PID=""

cleanup() {
    echo "Cleaning up OOBE session..."
    rm -f /run/wex-oobe/stage
    if [ -n "$COORDINATOR_PID" ] && kill -0 "$COORDINATOR_PID" 2>/dev/null; then
        kill "$COORDINATOR_PID" 2>/dev/null || true
        wait "$COORDINATOR_PID" 2>/dev/null || true
    fi
    rm -rf /run/wex-oobe
    if [ -n "$X_PID" ] && kill -0 "$X_PID" 2>/dev/null; then
        kill "$X_PID" 2>/dev/null || true
    fi
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM HUP

# Dismiss plymouth immediately so boot doesn't hang
plymouth --quit 2>/dev/null || true

# Ensure permissions on setup directory and logs
mkdir -p /etc/wineexplorer/setup /var/lib/wexsetup /var/log
touch /var/log/wineexplorer-oobe.log

wex_setup_prepare_user
chown wexsetup:adm /var/log/wineexplorer-oobe.log
chmod 660 /var/log/wineexplorer-oobe.log

wex_setup_start_xorg

# Query active X resolution dynamically
RES=$(wex_setup_resolution)
echo "Detected display resolution: $RES"

# Ensure root-owned component licenses directory
mkdir -p /etc/wineexplorer/component-licenses
chown root:root /etc/wineexplorer/component-licenses
chmod 755 /etc/wineexplorer/component-licenses

# Launch OOBE setup coordinator as root
mkdir -p /run/wex-oobe
chown root:root /run/wex-oobe
chmod 755 /run/wex-oobe
echo "oobe" > /run/wex-oobe/stage
chown root:root /run/wex-oobe/stage
chmod 644 /run/wex-oobe/stage
/usr/local/bin/wex-components --oobe-coordinator &
COORDINATOR_PID=$!

# Run oobe.exe inside dedicated wine prefix as wexsetup
wex_setup_run OOBE OOBE /etc/wineexplorer/setup/stage-oobe-done "$RES" oobe.exe

# Terminate coordinator and revoke authority
rm -f /run/wex-oobe/stage
if [ -n "$COORDINATOR_PID" ]; then
    kill "$COORDINATOR_PID" 2>/dev/null || true
    wait "$COORDINATOR_PID" 2>/dev/null || true
    COORDINATOR_PID=""
fi
rm -rf /run/wex-oobe

if [ -n "$X_PID" ]; then
    kill "$X_PID" 2>/dev/null || true
    X_PID=""
fi

# Apply automatic updates choice
UPDATES_CHOICE="on"
if [ -f /etc/wineexplorer/setup/automatic-updates ]; then
    UPDATES_CHOICE=$(tr -d ' \r\n' < /etc/wineexplorer/setup/automatic-updates | tr '[:upper:]' '[:lower:]')
fi
mkdir -p /etc/apt/apt.conf.d
if [ "$UPDATES_CHOICE" = "off" ] || [ "$UPDATES_CHOICE" = "no" ] || [ "$UPDATES_CHOICE" = "0" ]; then
    echo "Disabling automatic updates (/etc/apt/apt.conf.d/20auto-upgrades)..."
    cat <<'EOF' > /etc/apt/apt.conf.d/20auto-upgrades
APT::Periodic::Update-Package-Lists "0";
APT::Periodic::Unattended-Upgrade "0";
EOF
else
    echo "Enabling automatic updates (/etc/apt/apt.conf.d/20auto-upgrades)..."
    cat <<'EOF' > /etc/apt/apt.conf.d/20auto-upgrades
APT::Periodic::Update-Package-Lists "1";
APT::Periodic::Unattended-Upgrade "1";
EOF
fi

# Clean up temporary setup user and files
pkill -9 -u wexsetup 2>/dev/null || true
rm -rf "/var/cache/wineexplorer/prefetch/$(id -u wexsetup 2>/dev/null || echo none)"
userdel -r -f wexsetup 2>/dev/null || true
rm -rf /var/lib/wexsetup /etc/skel/.wine/drive_c/users/wexsetup
chown -R root:root /etc/skel/.wine 2>/dev/null || true
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
