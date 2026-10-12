#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds wineexplorer and wineexplorer-session deb packages.
# Run on the Debian VM (or via scripts/vm.sh ssh).
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$HOME/build/wine64}"
OUT_DIR="${OUT_DIR:-$SCRIPT_DIR/live-build/config/packages.chroot}"

mkdir -p "$OUT_DIR"

stage_license_documents()
{
    local doc_dir="$1/usr/share/doc/$2"
    mkdir -p "$doc_dir"
    install -m644 "$ROOT_DIR/LICENSE.md" "$doc_dir/copyright"
    install -m644 "$ROOT_DIR/COPYING" "$doc_dir/COPYING"
    install -m644 "$ROOT_DIR/wine/LICENSE" "$doc_dir/WINE-LICENSE"
    install -m644 "$ROOT_DIR/wine/COPYING.LIB" "$doc_dir/COPYING.LIB"
}

STAGE_WINE="/tmp/wexpkg-wine"
STAGE_SESSION="/tmp/wexpkg-session"
rm -rf "$STAGE_WINE" "$STAGE_SESSION"

echo "=== 1. Packaging wineexplorer ==="
mkdir -p "$STAGE_WINE/DEBIAN"
mkdir -p "$STAGE_WINE/usr/local"

if [ -f "$BUILD_DIR/Makefile" ]; then
    echo "Installing from $BUILD_DIR into staging..."
    make -C "$BUILD_DIR" install DESTDIR="$STAGE_WINE" >/dev/null
else
    echo "Copying /usr/local into staging..."
    cp -a /usr/local/* "$STAGE_WINE/usr/local/"
fi

# Remove development include headers to save ISO squashfs space
rm -rf "$STAGE_WINE/usr/local/include"

# Remove session script from wineexplorer package (owned by wineexplorer-session)
rm -f "$STAGE_WINE/usr/local/bin/wine-explorer-session.sh"
rm -f "$STAGE_WINE/usr/local/bin/wex-"*.sh

WINE_DEPS="libc6, libasound2t64, libavcodec61, libavformat61, libcapi20-3t64, libcups2t64, libdbus-1-3, libfontconfig1, libfreetype6, libgettextpo0, libgl1, libglu1-mesa, libgnutls30t64, libgphoto2-6t64, libgstreamer-plugins-base1.0-0, libkrb5-3, libldap2, libncursesw6, libopenal1, libosmesa6, libpcap0.8t64, libpcsclite1, libpulse0, libsdl2-2.0-0, libssl3t64, libudev1, libunwind8, libusb-1.0-0, libv4l-0t64, libvulkan1, libwayland-client0, libx11-6, libxcomposite1, libxcursor1, libxext6, libxfixes3, libxi6, libxinerama1, libxkbfile1, libxkbregistry0, libxml2, libxmu6, libxrandr2, libxrender1, libxslt1.1, libxt6t64, libxxf86dga1, libxxf86vm1, zlib1g"

cat > "$STAGE_WINE/DEBIAN/control" <<EOF
Package: wineexplorer
Version: 11.19-wineexplorer1
Section: otherosfs
Priority: optional
Architecture: amd64
Maintainer: WineExplorer Team <wineexplorer@local>
Depends: $WINE_DEPS
Description: Wine customized and configured with Windows XP / Server 2003 shell
 This package provides Wine customized as a full desktop environment
 matching the Windows XP and Windows Server 2003 shell experience.
EOF

stage_license_documents "$STAGE_WINE" wineexplorer
dpkg-deb --build --root-owner-group "$STAGE_WINE" "$OUT_DIR/wineexplorer_11.19-wineexplorer1_amd64.deb"
echo "Built $OUT_DIR/wineexplorer_11.19-wineexplorer1_amd64.deb"

echo "=== 2. Packaging wineexplorer-session ==="
mkdir -p "$STAGE_SESSION/DEBIAN"
mkdir -p "$STAGE_SESSION/usr/local/bin"
mkdir -p "$STAGE_SESSION/usr/share/xsessions"
mkdir -p "$STAGE_SESSION/etc/X11/xorg.conf.d"
mkdir -p "$STAGE_SESSION/lib/systemd/system"
mkdir -p "$STAGE_SESSION/etc/polkit-1/rules.d"

# Session launcher and setup runners
cp "$ROOT_DIR/scripts/wine-explorer-session.sh" "$STAGE_SESSION/usr/local/bin/wine-explorer-session.sh"
cp "$SCRIPT_DIR/scripts/wex-gui-setup.sh" "$STAGE_SESSION/usr/local/bin/wex-gui-setup.sh"
cp "$SCRIPT_DIR/scripts/wex-oobe.sh" "$STAGE_SESSION/usr/local/bin/wex-oobe.sh"
chmod 755 "$STAGE_SESSION/usr/local/bin/"*

# Desktop session definition
cp "$ROOT_DIR/scripts/wine-explorer.desktop" "$STAGE_SESSION/usr/share/xsessions/wine-explorer.desktop"
chmod 644 "$STAGE_SESSION/usr/share/xsessions/wine-explorer.desktop"

# Xorg modesetting configuration
cp "$ROOT_DIR/scripts/20-wineexplorer-modesetting.conf" "$STAGE_SESSION/etc/X11/xorg.conf.d/20-wineexplorer-modesetting.conf"
chmod 644 "$STAGE_SESSION/etc/X11/xorg.conf.d/20-wineexplorer-modesetting.conf"

# Setup systemd units and polkit rule
cp "$SCRIPT_DIR/systemd/wex-textsetup.service" "$STAGE_SESSION/lib/systemd/system/"
cp "$SCRIPT_DIR/systemd/wex-setup.service" "$STAGE_SESSION/lib/systemd/system/"
cp "$SCRIPT_DIR/systemd/wex-oobe.service" "$STAGE_SESSION/lib/systemd/system/"
chmod 644 "$STAGE_SESSION/lib/systemd/system/"*.service
cp "$SCRIPT_DIR/systemd/49-wexsetup.rules" "$STAGE_SESSION/etc/polkit-1/rules.d/"
chmod 644 "$STAGE_SESSION/etc/polkit-1/rules.d/49-wexsetup.rules"

# Postinst to enable services
cat > "$STAGE_SESSION/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
systemctl daemon-reload || true
systemctl enable wex-textsetup.service wex-setup.service wex-oobe.service || true
exit 0
EOF
chmod 755 "$STAGE_SESSION/DEBIAN/postinst"

cat > "$STAGE_SESSION/DEBIAN/control" <<EOF
Package: wineexplorer-session
Version: 1.0-1
Section: x11
Priority: optional
Architecture: all
Maintainer: WineExplorer Team <wineexplorer@local>
Depends: wineexplorer, gdm3, xorg, xserver-xorg-video-all, xserver-xorg-input-all, x11-xserver-utils, x11-utils, xdotool, libfdisk1, libblkid1, ncurses-bin, desktop-base
Replaces: wineexplorer (<< 11.19-wineexplorer2)
Description: WineExplorer session scripts and desktop configuration
 Session scripts, Xorg configurations, GDM defaults and systemd setup units
 for the WineExplorer Windows XP environment.
EOF

stage_license_documents "$STAGE_SESSION" wineexplorer-session
dpkg-deb --build --root-owner-group "$STAGE_SESSION" "$OUT_DIR/wineexplorer-session_1.0-1_all.deb"
echo "Built $OUT_DIR/wineexplorer-session_1.0-1_all.deb"

# Clean up staging
rm -rf "$STAGE_WINE" "$STAGE_SESSION"
echo "Done packaging WineExplorer."
