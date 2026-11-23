#!/bin/bash
# Runner for Stage 2 GUI-mode Setup
set -e

source "$(dirname -- "${BASH_SOURCE[0]}")/wex-setup-common.sh"

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

wex_setup_prepare_user
chown wexsetup:adm /var/log/wineexplorer-setup.log
chmod 660 /var/log/wineexplorer-setup.log

wex_setup_start_xorg

# Query active X resolution dynamically
RES=$(wex_setup_resolution)
echo "Detected display resolution: $RES"

# Run setup.exe inside dedicated wine prefix as wexsetup
rm -f /etc/wineexplorer/setup/owner /etc/wineexplorer/setup/workgroup
wex_setup_run "GUI-mode Setup" Setup /etc/wineexplorer/setup/stage-gui-done "$RES" setup.exe -newsetup

# setup.exe stages these settings; publish them with root ownership for their consumers.
wex_setup_publish owner
wex_setup_publish workgroup

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
    # the package's markers (present by default since 2026-11-18; package-wine.sh) turn the system tier's session 0 on
    # (docs/SYSTEM_TIER_WORK_ORDERS.md) and enforce elevation ("Enforced elevation on an installed machine")
    WEX_SETUP_ENV=()
    # setup turns session 0 on by itself (decision 2, 2026-11-23); only a test image packaged without the marker
    # (WEX_SESSION0=0, for comparison) asks for a machine without it
    if [ -f /usr/local/share/wineexplorer/session0.enable ]; then WEX_SETUP_ENV+=(WEX_SESSION0=1); else WEX_SETUP_ENV+=(WEX_SESSION0=0); fi
    if [ -f /usr/local/share/wineexplorer/elevation.enable ]; then WEX_SETUP_ENV+=(WEX_ELEVATION=1); else WEX_SETUP_ENV+=(WEX_ELEVATION=0); fi
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
