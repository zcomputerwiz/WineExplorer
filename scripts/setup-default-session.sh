#!/bin/bash
# Run as root on the VM: LightDM as the display manager, with automatic logon of wineuser into the WineExplorer
# session (docs/WINLOGON_AND_DWM.md, phase W1).  Needs the lightdm and lightdm-gtk-greeter packages.
set -e
cd "$(dirname "$0")"

install -D -m 644 lightdm-wineexplorer.conf /etc/lightdm/lightdm.conf.d/50-wineexplorer.conf

# AccountsService default session for wineuser (the greeter preselects it)
mkdir -p /var/lib/AccountsService/users
cat > /var/lib/AccountsService/users/wineuser << 'EOF'
[User]
Session=wine-explorer
XSession=wine-explorer
SystemAccount=false
EOF

# make LightDM the display manager
echo /usr/sbin/lightdm > /etc/X11/default-display-manager
echo "lightdm shared/default-x-display-manager select lightdm" | debconf-set-selections
systemctl disable gdm3 2>/dev/null || true
ln -sf /usr/lib/systemd/system/lightdm.service /etc/systemd/system/display-manager.service
systemctl daemon-reload

echo "LightDM configured; restart the display manager (systemctl restart display-manager) to apply."
