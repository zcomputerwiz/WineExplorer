#!/bin/bash
set -e

# Configure GDM3 for autologin and Xorg
cat > /etc/gdm3/daemon.conf << 'EOF'
# GDM configuration storage
[daemon]
WaylandEnable=false
AutomaticLoginEnable=true
AutomaticLogin=wineuser
DefaultSession=wine-explorer.desktop

[security]

[xdmcp]

[chooser]

[debug]
EOF

# Configure AccountsService default session for wineuser
mkdir -p /var/lib/AccountsService/users
cat > /var/lib/AccountsService/users/wineuser << 'EOF'
[User]
Session=wine-explorer
XSession=wine-explorer
SystemAccount=false
EOF

# Set update-alternatives for x-session-manager if present
if command -v update-alternatives >/dev/null 2>&1; then
    update-alternatives --install /usr/bin/x-session-manager x-session-manager /usr/local/bin/wine-explorer-session.sh 100 || true
    update-alternatives --set x-session-manager /usr/local/bin/wine-explorer-session.sh || true
fi

echo "WineExplorer configured as default system session successfully."
