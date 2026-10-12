<!--
Copyright 2026 WineExplorer Desktop Environment Project

SPDX-License-Identifier: GPL-2.0-or-later

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
-->

# D2 implementation and coordinator validation

2026-10-12. Work stayed in `D:/DeepThought/Projects/WineExplorer-d1`, initially clean on `d2-effects` at `7c5f462`.
No SSH, VM, remote build or push was used. No C compiler or Wine runtime is available locally. Source/API review,
LF checks, Unix dispatch-table consistency and `git diff --check` are local checks, not build/visual validation.

## Commit delivery

The first ordinary add/commit failed with this exact error (once for each command):

```text
fatal: Unable to create 'D:/DeepThought/Projects/WineExplorer/.git/worktrees/WineExplorer-d1/index.lock': Permission denied
```

The shared worktree's Git administration directory is outside the writable workspace. Its branch/index were not
modified. Four sequential commits instead live on `d2-effects` in `build/d2-git`, using the original repository's
objects as read-only alternates. `build/d2-effects.bundle` delivers all new objects with `7c5f462` as prerequisite.
The current working files contain the completed implementation, but the shared worktree HEAD remains `7c5f462`.

The coordinator can attach these commits from an environment permitted to write the shared Git directory:

```powershell
git fetch ./build/d2-effects.bundle d2-effects
git diff FETCH_HEAD --
# The diff above must be empty: the current files should equal the delivered tree.
git reset --mixed FETCH_HEAD
git status --short --branch
git log -4 --oneline
```

The reset updates branch/index while preserving working files. If there are additional coordinator edits, import
the bundle into another clean worktree instead and merge/cherry-pick the four commits there.

## Build on the coordinator's existing Linux setup

Sync the changed tracked files through the coordinator's usual mechanism, including the new header/export/Unix
dispatch entries. Rebuild and install Wine with the existing configured build (the example paths are the D1 lab's):

```sh
cd ~/open/build
make -j8
make install
cd ~/open
~/open/inst/bin/winegcc -m64 -Wall -I ~/open/wine/include \
    -o compose-probe.exe compose-probe.c -luser32 -lgdi32
```

Copy `scripts/compose-probe.c` separately to `~/open/compose-probe.c` before the last command. Build the whole tree:
the affected components are wexhost, user32, winex11.drv and Explorer. Also check wexhost builds without optional
X11/Composite libraries if the coordinator has such a configuration (the new APIs have disabled-feature stubs).

## Real shell tests

Use the already-running real session or the coordinator's existing isolated X display. For each comparison set
the Composition DWORD, then restart Explorer using the established session procedure. ManageLinuxWindows must
remain enabled for the shell compositor. In the D1 lab, prefix/display examples are:

```sh
DISPLAY=:7 WINEPREFIX=~/open/prefix ~/open/inst/bin/wine reg add \
    'HKCU\Software\WineExplorer\Shell' /v Composition /t REG_DWORD /d 1 /f
DISPLAY=:7 WINEPREFIX=~/open/prefix ~/open/inst/bin/wine ./compose-probe.exe >d2-on.log 2>&1
```

Repeat with `/d 0` after a shell restart and save `d2-off.log`. Substitute the real session's DISPLAY/prefix when
testing Xorg. On the probe controls window:

1. Press M to pause motion. If shadows are initially disabled, press D; its log must report `drop_shadow=1`.
   With composition on, the yellow class-shadow popup should have a short, soft right/bottom shadow. With it off,
   the compositor adds none. Press M again: neither moving popup nor shadow should leave trails. S hide/show must
   remove/recreate the shadow. Press D to turn it off, then on: the remapped popup follows the setting. Resize/remap
   other windows with R and ensure overlapping shadows remain below their own window and above lower windows.
   Click exposed shadow pixels: the underlying desktop/window should receive input. The probe restores the initial
   drop-shadow SPI when closed normally with Escape.
2. Press B twice: about 900 ms fade in, then fade out with composition on. Each log must say `result=1`,
   `style_restored=1`, `attributes_restored=1`, and visibility must match the requested direction. With composition
   off the intermediate alpha is not visible, but visibility/style still match. Press U twice to fade the existing
   blue 50% alpha window: its original alpha must return. Press 0 twice: immediate show/hide without division by
   zero or a leftover layered style. Recheck B afterwards. The green key window/ramp must retain D1 behavior.
3. Press P and select/dismiss a TrackPopupMenu. With the drop-shadow SPI on and composition on it should have a
   shadow; off it should have none from this compositor. The log reports both menu SPIs. Automatic menu fades are
   deliberately deferred even when both settings are on. Also check a real application's tooltip/popup from a
   different process; the private driver property supplies its shadow without Explorer reading its XID.
4. Pause motion with M, close menus, press K and compare `Z:\tmp\capture.bmp` (Unix `/tmp/capture.bmp`) to a real-root
   screenshot taken before the capture preview appears. Both must contain the same wallpaper, layered windows,
   shadow and ordinary application windows. Save a copy of the BMP, then press J and compare again. Each log must
   report `result=1`; K covers compatible-bitmap copies and J covers DIB readback. Press T: the half-size screenshot
   must contain the same composed content, testing the XRender scaling fallback. Test captures with composition
   both on and off, including an ordinary application window and a Linux ARGB popup. The preview is hidden before
   each new capture and the probe writes the same BMP path each time.
5. On an already-managed isolated display, exercise compositor API toggles in the owning process and repeat K/J/T
   without restarting the probe: its original screen DC must follow current selection ownership. D1's existing
   `--own-compositor` mode is available only on an unmanaged isolated display/prefix; pass the desktop XID explicitly
   when its Win32 property reads zero. D1 already observed that mode's WM can misclassify its probe controls as a
   foreign client, so do not use it as the sole D2 validation. Close the preview/popups and press F for opaque
   fullscreen, capture during bypass, then F again and recheck shadows/capture after composition resumes.
6. Exercise repeated enable/disable, shell shutdown and popup destruction; check `+wexhost,+x11drv` logs for X errors,
   selection conflicts or stale shadow resources. Existing D1 input/color-key/ramp checks should still pass.

## Stock changes and remaining uncertainty

`docs/STOCK_CHANGES.md` lists each change and its gate. The touched stock files are `user32/win.c` and
`winex11.drv/{window.c,x11drv.h,bitblt.c,xrender.c}`. Explorer/wexhost changes are project shell/compositor code.
`AnimateWindow(AW_BLEND)` is unconditional as explicitly allowed by the spec; intermediate opacity needs a
compositor. Shadow hints and virtual-desktop capture require selection ownership. No automatic menu fade is added:
sharing user32's helper with win32u and adding an explicit compositor gate would require broader callback/driver
plumbing. Existing per-pixel layered windows preserve their bitmap by using immediate visibility changes.

Visual shadow softness, compositor throughput during fades, real-root overlay readback on Xorg, desktop root
offsets/multiple monitors, fullscreen capture/bypass, cross-process show/remap races and Linux ARGB clients remain
unverified. A foreign popup already visible when composition is enabled must be remapped to publish its shadow hint;
SPI changes apply on show. Rectangular cached shadows approximate a blur; shaped windows do not get shape-specific
blurred silhouettes. Root capture is gated on any compositing manager, as requested, so also test a non-Wex manager
if that setup is available. Fades are synchronous and leave non-blend animation behavior unchanged.
