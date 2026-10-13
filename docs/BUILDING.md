<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Building WineExplorer

Build on Debian using the project's Wine fork and owned source modules.
The deployed VM stack remains X11 with Wine as the desktop; Proton is separate.

```bash
git clone --recurse-submodules https://github.com/zcomputerwiz/WineExplorer.git
cd WineExplorer
bash scripts/assemble.sh
mkdir -p build/wine64
cd build/wine64
../../wine/configure --enable-win64 --prefix="$PWD/install"
make -j8
make install
```

Wine's build dependencies and MinGW are required. Assembly also requires
Python 3 and autoconf. The release baseline is wine-11.19; the retained
wineexplorer/11.19 branch is pinned by the main repository's gitlink.
Keep original Wine/ReactOS license notices.

Project implementations live in src/dlls and src/programs; shared headers
are in src/include and authored theme definitions in src/resources/themes.
assemble.sh links these into Wine before configure. Three declared stub
replacements preserve pristine stock directories in build/assembly-stock.
Edited, extra-file or undeclared destinations are rejected. Assembly makes
the Wine checkout appear modified through these build links; use a separate
clean fork checkout for rebasing, rather than resetting a live assembled tree.

For development VM sync, run scripts/vm.sh from Git Bash with its login
environment. VM_PORT, VM_KEY, VM_USER and VM_SOURCE select the destination.
Sync prepares the pinned fork separately from project files and preserves
an older VM source tree before switching it. Stamps are per destination
under ignored build/vm-sync. Private resource routing uses the local-only
WineExplorer-local repository; public builds never apply that overlay.

From the project root, package the configured build with:

```bash
BUILD_DIR="$PWD/build/wine64" bash installer/package-wine.sh
sudo env BUILD_DIR="$PWD/build/wine64" \
  WORK_DIR=/srv/protonexplorer/wineexplorer-iso-work \
  OUT_ISO=/srv/protonexplorer/wineexplorer.iso \
  bash installer/build-iso.sh
```

Packaging uses staged installation with the canonical /usr/local prefix;
its default build directory is build/wine64 under this checkout.
It rejects another checkout's build and /home/wineuser/inst/build.
Never run make install in the legacy inst/build directory.
An existing live-build workspace requires --clean; keep test output on the
secondary disk rather than filling the build VM's root filesystem.

P4 acceptance and remaining work are tracked in refactor/P4.md.
