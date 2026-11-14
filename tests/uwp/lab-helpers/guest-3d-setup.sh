#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright 2026 WineExplorer contributors
# Install the OpenGL, EGL and Vulkan development headers into the lab guest, then reconfigure and rebuild candidate
# u11-2 in place so that its Wine has a 3D device (wined3d on mesa's llvmpipe / lavapipe).
set -euo pipefail
root=/home/debian/uwp-u1-20261111
candidate="$root/build/candidates/u11-2"
test "$(cat "$root/owner")" = codex-uwp-u1-20261111
sudo -n apt-get install -y --no-install-recommends libgl-dev libegl-dev libgles-dev libvulkan-dev libglu1-mesa-dev libosmesa6-dev
cd "$candidate/build"
echo CONFIGURING > "$candidate/phase"
"$candidate/source/configure" --enable-archs=i386,x86_64 > "$candidate/configure.log" 2>&1
grep -E 'OpenGL|Vulkan|EGL|X11' "$candidate/configure.log" | head -10 || true
echo BUILDING > "$candidate/phase"
make -j2 > "$candidate/build.log" 2>&1 || { echo FAILED > "$candidate/phase"; exit 1; }
echo BUILT > "$candidate/phase"
