#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Builds wineexplorer and wineexplorer-session deb packages.
# Run on the Debian VM (or via scripts/vm.sh ssh).
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/wow64}"
OUT_DIR="${OUT_DIR:-$SCRIPT_DIR/live-build/config/packages.chroot}"

mkdir -p "$OUT_DIR"

if [ ! -f "$BUILD_DIR/Makefile" ]; then
    echo "BUILD_DIR must point to a configured Wine build: $BUILD_DIR"
    echo "Refusing to package the build machine's installed runtime."
    exit 1
fi
if [ "$(realpath "$BUILD_DIR")" = /home/wineuser/inst/build ]; then
    echo "The legacy inst/build directory must not run make install."
    exit 1
fi
build_source=$(sed -n 's/^srcdir = //p' "$BUILD_DIR/Makefile" | head -1)
case "$build_source" in /*) ;; *) build_source="$BUILD_DIR/$build_source" ;; esac
if [ "$(realpath "$build_source")" != "$(realpath "$ROOT_DIR/wine")" ]; then
    echo "BUILD_DIR belongs to another Wine source checkout: $build_source"
    exit 1
fi

stage_license_documents()
{
    local doc_dir="$1/usr/share/doc/$2"
    mkdir -p "$doc_dir"
    install -m644 "$ROOT_DIR/LICENSE.md" "$doc_dir/copyright"
    install -m644 "$ROOT_DIR/COPYING" "$doc_dir/COPYING"
    install -m644 "$ROOT_DIR/wine/LICENSE" "$doc_dir/WINE-LICENSE"
    install -m644 "$ROOT_DIR/wine/COPYING.LIB" "$doc_dir/COPYING.LIB"
}

PACKAGE_STAGE=$(mktemp -d "${TMPDIR:-/tmp}/wexpkg.XXXXXXXX")
trap 'rm -rf -- "$PACKAGE_STAGE"' EXIT
STAGE_WINE="$PACKAGE_STAGE/wine"
STAGE_SESSION="$PACKAGE_STAGE/session"

echo "=== 1. Packaging wineexplorer ==="
mkdir -p "$STAGE_WINE/DEBIAN"
mkdir -p "$STAGE_WINE/usr/local"

echo "Installing from $BUILD_DIR into staging..."
make -C "$BUILD_DIR" install prefix=/usr/local DESTDIR="$STAGE_WINE" >/dev/null

if [ ! -d "$STAGE_WINE/usr/local/lib/wine/x86_64-windows" ] || \
   [ ! -d "$STAGE_WINE/usr/local/lib/wine/i386-windows" ] || \
   [ ! -d "$STAGE_WINE/usr/local/lib/wine/x86_64-unix" ]; then
    echo "ERROR: Staged Wine installation is not a WoW64 build."
    echo "Expected x86_64-windows, i386-windows, and x86_64-unix under usr/local/lib/wine/."
    echo "Ensure Wine was configured with --enable-archs=i386,x86_64."
    exit 1
fi
mkdir -p "$STAGE_WINE/usr/local/share/wineexplorer/themes"
install -m644 "$ROOT_DIR/src/resources/themes/"*.theme "$STAGE_WINE/usr/local/share/wineexplorer/themes/"

# Build open Luna visual style
echo "Building open Luna theme package..."
mkdir -p "$STAGE_WINE/usr/local/share/wineexplorer/themes/Luna"
python3 "$ROOT_DIR/scripts/build_msstyles.py" \
    "$ROOT_DIR/src/resources/themes/luna-open" \
    "$STAGE_WINE/usr/local/share/wineexplorer/themes/Luna/Luna.msstyles"

# Build open icon pack (Fluent) if tools are available
echo "Building Fluent theme package..."
(
    set -e
    SRC_CACHE="${WEX_ICONSRC_CACHE:-/var/cache/wineexplorer/iconsrc}"
    export GIT_TERMINAL_PROMPT=0 GCM_INTERACTIVE=never
    mkdir -p "$SRC_CACHE"
    if [ ! -d "$SRC_CACHE/fluentui-system-icons/.git" ] && [ ! -d "$SRC_CACHE/fluentui-system-icons/assets" ]; then
        echo "Fetching fluentui-system-icons sparse checkout..."
        mkdir -p "$SRC_CACHE/fluentui-system-icons"
        (
            cd "$SRC_CACHE/fluentui-system-icons"
            git init
            git remote add origin https://github.com/microsoft/fluentui-system-icons.git
            git config core.sparseCheckout true
            echo "assets/*" > .git/info/sparse-checkout
            echo "LICENSE" >> .git/info/sparse-checkout
            timeout 600 git fetch --depth 1 origin a347af95478f989038cccbf085978865264c048a
            git checkout FETCH_HEAD
        ) || { rm -rf "$SRC_CACHE/fluentui-system-icons"; exit 1; }   # no half-made checkout left behind
    fi
    if [ ! -d "$SRC_CACHE/tela/.git" ] && [ ! -d "$SRC_CACHE/tela/src" ]; then
        echo "Fetching tela icon theme sparse checkout..."
        mkdir -p "$SRC_CACHE/tela"
        (
            cd "$SRC_CACHE/tela"
            git init
            git remote add origin https://github.com/UbuntuBudgie/Tela-icon-theme.git
            git config core.sparseCheckout true
            echo "src/scalable/places/user-trash*.svg" > .git/info/sparse-checkout
            echo "COPYING" >> .git/info/sparse-checkout
            timeout 600 git fetch --depth 1 origin 50f51f45d95c629cbb21530bf71c82cdb96269da
            git checkout FETCH_HEAD
        ) || { rm -rf "$SRC_CACHE/tela"; exit 1; }   # no half-made checkout left behind
    fi
    if [ ! -d "$SRC_CACHE/reactos/.git" ] && [ ! -d "$SRC_CACHE/reactos/dll" ]; then
        echo "Fetching reactos sparse checkout..."
        mkdir -p "$SRC_CACHE/reactos"
        (
            cd "$SRC_CACHE/reactos"
            git init
            git remote add origin https://github.com/reactos/reactos.git
            git config core.sparseCheckout true
            echo "base/applications/*" > .git/info/sparse-checkout
            echo "base/shell/*" >> .git/info/sparse-checkout
            echo "dll/cpl/*" >> .git/info/sparse-checkout
            echo "dll/shellext/netshell/res/*" >> .git/info/sparse-checkout
            echo "dll/win32/shell32/res/icons/*" >> .git/info/sparse-checkout
            echo "win32ss/user/user32/resources/*" >> .git/info/sparse-checkout
            echo "COPYING" >> .git/info/sparse-checkout
            timeout 600 git fetch --filter=blob:none --depth 1 origin 974a4464497c68c1ebcf41ab372bec2c010e6f8b
            git checkout FETCH_HEAD
        ) || { rm -rf "$SRC_CACHE/reactos"; exit 1; }
    fi
    python3 "$ROOT_DIR/scripts/build_iconpack.py" "$ROOT_DIR/assets/iconpacks/fluent.json" \
        "$STAGE_WINE/usr/local/share/wineexplorer/themes" \
        --source fluent="$SRC_CACHE/fluentui-system-icons" \
        --source tela="$SRC_CACHE/tela"
    python3 "$ROOT_DIR/scripts/build_iconpack.py" "$ROOT_DIR/assets/iconpacks/xp.json" \
        "$STAGE_WINE/usr/local/share/wineexplorer/themes" \
        --source reactos="$SRC_CACHE/reactos" \
        --source fluent="$SRC_CACHE/fluentui-system-icons"
    python3 "$ROOT_DIR/scripts/build_cursors.py" "$ROOT_DIR/assets/cursors/fluent.json" \
        "$STAGE_WINE/usr/local/share/wineexplorer/themes" \
        --source fluent="$SRC_CACHE/fluentui-system-icons"
    python3 "$ROOT_DIR/scripts/build_cursors.py" "$ROOT_DIR/assets/cursors/xp.json" \
        "$STAGE_WINE/usr/local/share/wineexplorer/themes" \
        --source reactos="$SRC_CACHE/reactos"
    echo "Fluent and XP theme packages installed to staged share/wineexplorer/themes"
) || {
    echo "WARNING: Failed to build Fluent icon pack; skipping it for this build."
    rm -rf "$STAGE_WINE/usr/local/share/wineexplorer/themes/Fluent"
}

mkdir -p "$STAGE_WINE/usr/local/share/wineexplorer/startskins"
if [ -d "$ROOT_DIR/src/resources/startskins" ]; then
    install -m644 "$ROOT_DIR/src/resources/startskins/"*.skin* "$STAGE_WINE/usr/local/share/wineexplorer/startskins/" 2>/dev/null || true
    if [ -f "$ROOT_DIR/src/resources/startskins/LICENSE" ]; then
        install -m644 "$ROOT_DIR/src/resources/startskins/LICENSE" "$STAGE_WINE/usr/local/share/wineexplorer/startskins/" 2>/dev/null || true
    fi
fi

# Offline payloads and their full upstream notices. Build dependencies are
# explicit: python3, tar, zstd, unzip and msitools; never apt-install mid-build.
python3 "$SCRIPT_DIR/stage-runtime-assets.py" --stage "$STAGE_WINE" \
    --cache "${WEX_DOWNLOAD_CACHE:-/var/cache/wineexplorer/downloads}"

# Remove development include headers to save ISO squashfs space
rm -rf "$STAGE_WINE/usr/local/include"

# Remove session script from wineexplorer package (owned by wineexplorer-session)
rm -f "$STAGE_WINE/usr/local/bin/wine-explorer-session.sh"
rm -f "$STAGE_WINE/usr/local/bin/wex-"*.sh

WINE_DEPS="libc6, libasound2t64, libavcodec61, libavformat61, libcapi20-3t64, libcups2t64, libdbus-1-3, libfontconfig1, libfreetype6, libgdk-pixbuf-2.0-0, libgettextpo0, libgl1, libglu1-mesa, libgnutls30t64, libgphoto2-6t64, libgstreamer-plugins-base1.0-0, libkrb5-3, libldap2, libncursesw6, libopenal1, libosmesa6, libpcap0.8t64, libpcsclite1, libpulse0, librsvg2-common, libsdl2-2.0-0, libssl3t64, libudev1, libunwind8, libusb-1.0-0, libv4l-0t64, libvulkan1, libwayland-client0, libx11-6, libxcomposite1, libxcursor1, libxdamage1, libxext6, libxfixes3, libxi6, libxinerama1, libxkbfile1, libxkbregistry0, libxml2, libxmu6, libxrandr2, libxrender1, libxslt1.1, libxt6t64, libxtst6, libxxf86dga1, libxxf86vm1, zlib1g"

WINE_DEPS="$WINE_DEPS, fontconfig, fonts-dejavu-core, fonts-liberation, fonts-liberation-sans-narrow, fonts-crosextra-carlito, fonts-crosextra-caladea, fonts-noto-core, fonts-noto-cjk, fonts-ipafont-gothic, fonts-ipafont-mincho, fonts-unfonts-core"

cat > "$STAGE_WINE/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if [ "$1" = "configure" ] && command -v fc-cache >/dev/null 2>&1; then
    fc-cache -f /usr/local/share/fonts
fi
if [ "$1" = "configure" ] && [ -d /var/lib/wineexplorer/machine/drive_c/windows/system32 ] && [ -x /usr/local/bin/wex-setup-machine.sh ]; then
    /usr/local/bin/wex-setup-machine.sh --update
fi
exit 0
EOF
chmod 755 "$STAGE_WINE/DEBIAN/postinst"

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
mkdir -p "$STAGE_SESSION/usr/local/sbin"
mkdir -p "$STAGE_SESSION/usr/share/xsessions"
mkdir -p "$STAGE_SESSION/usr/share/wayland-sessions"
mkdir -p "$STAGE_SESSION/etc/xdg/wineexplorer/wayland"
mkdir -p "$STAGE_SESSION/usr/share/xgreeters"
mkdir -p "$STAGE_SESSION/etc/lightdm/lightdm.conf.d"
mkdir -p "$STAGE_SESSION/etc/X11/xorg.conf.d"
mkdir -p "$STAGE_SESSION/etc/grub.d"
mkdir -p "$STAGE_SESSION/lib/systemd/system"
mkdir -p "$STAGE_SESSION/etc/polkit-1/rules.d"

# Session launcher and setup runners
cp "$ROOT_DIR/scripts/wine-explorer-session.sh" "$STAGE_SESSION/usr/local/bin/wine-explorer-session.sh"
cp "$ROOT_DIR/scripts/wine-explorer-session-common.sh" "$STAGE_SESSION/usr/local/bin/"
cp "$ROOT_DIR/scripts/wine-explorer-wayland-session.sh" "$STAGE_SESSION/usr/local/bin/"
cp "$ROOT_DIR/scripts/wex-safe-terminal.sh" "$STAGE_SESSION/usr/local/bin/wex-safe-terminal.sh"
cp "$ROOT_DIR/scripts/wex-safemode-setup.sh" "$STAGE_SESSION/usr/local/sbin/wex-safemode-setup.sh"
cp "$ROOT_DIR/scripts/wex-safemode-pam-check.sh" "$STAGE_SESSION/usr/local/sbin/wex-safemode-pam-check.sh"
cp "$SCRIPT_DIR/scripts/wex-gui-setup.sh" "$STAGE_SESSION/usr/local/bin/wex-gui-setup.sh"
cp "$SCRIPT_DIR/scripts/wex-oobe.sh" "$STAGE_SESSION/usr/local/bin/wex-oobe.sh"
cp "$ROOT_DIR/scripts/wex-greeter.sh" "$STAGE_SESSION/usr/local/bin/wex-greeter.sh"
cp "$ROOT_DIR/scripts/setup-machine-root.sh" "$STAGE_SESSION/usr/local/bin/wex-setup-machine.sh"
cp "$ROOT_DIR/scripts/migrate-prefix-split.sh" "$STAGE_SESSION/usr/local/bin/wex-migrate-prefix.sh"
install -m755 "$ROOT_DIR/scripts/wex-graphics.py" "$STAGE_SESSION/usr/local/bin/wex-graphics"
install -m755 "$ROOT_DIR/scripts/wex-components.py" "$STAGE_SESSION/usr/local/bin/wex-components"
install -m755 "$ROOT_DIR/scripts/check-font-targets.py" "$STAGE_SESSION/usr/local/bin/wex-check-font-targets"
install -Dm644 "$SCRIPT_DIR/wex-components.desktop" "$STAGE_SESSION/usr/share/applications/wex-components.desktop"
mkdir -p "$STAGE_SESSION/etc/wineexplorer/component-licenses"
chmod 755 "$STAGE_SESSION/usr/local/bin/"*
chmod 755 "$STAGE_SESSION/usr/local/sbin/"*

# Desktop session definition
cp "$ROOT_DIR/scripts/wine-explorer.desktop" "$STAGE_SESSION/usr/share/xsessions/wine-explorer.desktop"
cp "$ROOT_DIR/scripts/wex-safe-terminal.desktop" "$STAGE_SESSION/usr/share/xsessions/wex-safe-terminal.desktop"
chmod 644 "$STAGE_SESSION/usr/share/xsessions/"*.desktop
install -m644 "$ROOT_DIR/scripts/wine-explorer-wayland.desktop" "$STAGE_SESSION/usr/share/wayland-sessions/"
install -m644 "$ROOT_DIR/scripts/wayland/"*.xml "$STAGE_SESSION/etc/xdg/wineexplorer/wayland/"
install -m644 "$ROOT_DIR/scripts/wineexplorer-greeter.desktop" "$STAGE_SESSION/usr/share/xgreeters/"
install -m644 "$SCRIPT_DIR/live-build/config/includes.chroot/etc/lightdm/lightdm.conf.d/50-wineexplorer.conf" "$STAGE_SESSION/etc/lightdm/lightdm.conf.d/"

# Xorg modesetting configuration
cp "$ROOT_DIR/scripts/20-wineexplorer-modesetting.conf" "$STAGE_SESSION/etc/X11/xorg.conf.d/20-wineexplorer-modesetting.conf"
chmod 644 "$STAGE_SESSION/etc/X11/xorg.conf.d/20-wineexplorer-modesetting.conf"

# GRUB Safe Mode boot entry
cp "$SCRIPT_DIR/live-build/config/includes.chroot/etc/grub.d/11_wineexplorer_safemode" "$STAGE_SESSION/etc/grub.d/"
chmod 755 "$STAGE_SESSION/etc/grub.d/"*

# Setup systemd units and polkit rule
cp "$SCRIPT_DIR/systemd/wex-textsetup.service" "$STAGE_SESSION/lib/systemd/system/"
cp "$SCRIPT_DIR/systemd/wex-setup.service" "$STAGE_SESSION/lib/systemd/system/"
cp "$SCRIPT_DIR/systemd/wex-oobe.service" "$STAGE_SESSION/lib/systemd/system/"
cp "$SCRIPT_DIR/systemd/wex-safemode.service" "$STAGE_SESSION/lib/systemd/system/"
chmod 644 "$STAGE_SESSION/lib/systemd/system/"*.service
cp "$SCRIPT_DIR/systemd/49-wexsetup.rules" "$STAGE_SESSION/etc/polkit-1/rules.d/"
chmod 644 "$STAGE_SESSION/etc/polkit-1/rules.d/49-wexsetup.rules"

# Postinst to enable services and update existing machine root if present
cat > "$STAGE_SESSION/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
systemctl daemon-reload || true
systemctl enable wex-textsetup.service wex-setup.service wex-oobe.service wex-safemode.service || true
if [ "$1" = "configure" ] && [ -d /var/lib/wineexplorer/machine/drive_c/windows/system32 ] && [ -x /usr/local/bin/wex-setup-machine.sh ]; then
    /usr/local/bin/wex-setup-machine.sh --update
fi
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
Depends: wineexplorer, wexcomp, acl, lightdm, lightdm-gtk-greeter, accountsservice, xterm, xorg, xserver-xorg-video-all, xserver-xorg-input-all, x11-xserver-utils, x11-utils, xdotool, libfdisk1, libblkid1, ncurses-bin, desktop-base, python3, vulkan-tools, winetricks, cabextract, zenity, pkexec, xvfb, xauth
Replaces: wineexplorer (<< 11.19-wineexplorer2)
Description: WineExplorer session scripts and desktop configuration
 Session scripts, Xorg configurations, LightDM greeter and systemd setup units
 for the WineExplorer Windows XP environment.
EOF

stage_license_documents "$STAGE_SESSION" wineexplorer-session
dpkg-deb --build --root-owner-group "$STAGE_SESSION" "$OUT_DIR/wineexplorer-session_1.0-1_all.deb"
echo "Built $OUT_DIR/wineexplorer-session_1.0-1_all.deb"

# Clean up staging
rm -rf "$STAGE_WINE" "$STAGE_SESSION"
echo "Done packaging WineExplorer."
