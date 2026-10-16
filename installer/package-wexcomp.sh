#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Build/package the pinned compositor, without installing onto the build host.
set -euo pipefail
ROOT_DIR=$(cd "$(dirname "$0")/.." && pwd)
source_dir="$ROOT_DIR/wexcomp"
build_dir="${WEXCOMP_BUILD_DIR:-$ROOT_DIR/build/wexcomp}"
out_dir="${OUT_DIR:-$ROOT_DIR/installer/live-build/config/packages.chroot}"
test -f "$source_dir/meson.build" || { echo 'Initialize the wexcomp submodule first.' >&2; exit 1; }
git -C "$source_dir" diff --quiet HEAD -- || { echo 'Compositor source has uncommitted changes.' >&2; exit 1; }
revision=$(git -C "$source_dir" rev-parse HEAD)
if [ ! -f "$build_dir/build.ninja" ]; then
    meson setup "$build_dir" "$source_dir" --prefix=/usr/local -Dxwayland=enabled -Dman-pages=disabled -Dtest=disabled
fi
# A stale build of a different checkout must not silently supply the ISO binary.
meson introspect --buildsystem-files "$build_dir" |
    python3 -c 'import json,sys,os; expected=os.path.realpath(sys.argv[1]); assert any(os.path.realpath(p)==expected for p in json.load(sys.stdin)), "Compositor build belongs to another source tree"' "$source_dir/meson.build"
meson compile -C "$build_dir"

stage=$(mktemp -d "${TMPDIR:-/tmp}/wexcomp-package.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
package_dir="$stage/debian/wexcomp"
mkdir -p "$package_dir/DEBIAN" "$package_dir/usr/local/bin" "$package_dir/usr/share/doc/wexcomp" "$out_dir"
install -m755 "$build_dir/labwc" "$package_dir/usr/local/bin/wexcomp"
install -m644 "$source_dir/LICENSE" "$package_dir/usr/share/doc/wexcomp/copyright"
if [ -f "$source_dir/subprojects/libsfdo/LICENSE" ]; then
    install -m644 "$source_dir/subprojects/libsfdo/LICENSE" "$package_dir/usr/share/doc/wexcomp/libsfdo-LICENSE"
fi
printf '%s\n' "$revision" > "$package_dir/usr/share/doc/wexcomp/source-commit"
cat > "$stage/debian/control" <<'EOF'
Source: wexcomp
Section: x11
Priority: optional
Maintainer: WineExplorer contributors <wineexplorer@localhost>

Package: wexcomp
Architecture: any
Description: WineExplorer Wayland compositor
EOF
depends=$(cd "$stage" && dpkg-shlibdeps -O -e "$package_dir/usr/local/bin/wexcomp")
depends=${depends#shlibs:Depends=}
cat > "$package_dir/DEBIAN/control" <<EOF
Package: wexcomp
Version: 0.8.3+git${revision:0:12}-1
Architecture: $(dpkg --print-architecture)
Section: x11
Priority: optional
Maintainer: WineExplorer contributors <wineexplorer@localhost>
Depends: $depends, xwayland, libseat1, dbus-user-session
Description: WineExplorer Wayland compositor
 The project's pinned labwc fork, including its private shell socket and
 native window grouping. X11 remains the default WineExplorer session.
EOF
dpkg-deb --build --root-owner-group "$package_dir" "$out_dir/wexcomp_0.8.3+git${revision:0:12}-1_amd64.deb"
