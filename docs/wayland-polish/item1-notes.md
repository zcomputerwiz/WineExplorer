<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Item 1: fixed and verified (2026-10-17)

Work order: WAYLAND_POLISH_WORKORDER.md. One review at the end; this is an
incremental record, not a request for review. Item 1's primary fix is Wine
`bcf842a3a910f704cea3ecf88f03d2069a2182fa` on the fork's
`upstream/win32u-surface-shape` and production `wineexplorer/11.19` branches.

The real volumeicon Preferences host reports frame (382,142)-(898,631),
client (385,171), 510x457. Its window region includes all three side pixels.
Repainting does not restore them. Rendering the actual WINDOW theme's left
part into a local DIB gives the correct gray/white/white pixels. A temporary
compositor trace confirms the three source pixels are already zero ARGB in
Wine's buffer; all four compositor crops have the correct dimensions/offsets.

Wine's `win32u/dce.c:set_surface_shape_rect` uses `l_mask & r_mask` for spans
contained in one byte. The right mask describes bits outside the rectangle;
it must be complemented, as in the multi-byte path. For x=0..3, the old mask
is 0x1f instead of 0xe0. The bottom/caption spans cross byte boundaries and
survive. The one-line fix restores the actual Preferences side borders. Its
client rectangle stays unchanged. No compositor change is needed for this.

The 100/150% real-program matrix has already verified foot, QTerminal and
xterm borders, unchanged move dimensions and native/Win32 client alignment
within one physical pixel. Two extra real failures prevented completion:

- At 150%, volumeicon's native popup was unreachable. systray passed physical
  screen coordinates into XEmbed/XTest on the logical XWayland screen. Convert
  only native actions, retaining physical coordinates for Win32 popup menus
  and retaining dbusmenu timestamps. This fix opens the actual popup.
- Preferences then falls back to a native frame at 150%. Its initial physical
  client height 686, with top margin 29, maps to a frame-local logical height
  458 rather than native height 457. Initial/observed shell geometry now uses
  the same frame-local endpoint rounding rule as the driver's existing resize
  conversion. The final real-program matrix verifies successful adoption,
  complete borders and correct native/Win32 client alignment at both scales.

Both architectures build with the mask and shell fixes. The final matrix
passes all eight app/scale combinations: [raw output](logs/item1-frames.log).
Screenshots: [before](screenshots/volume-before.png),
[Preferences 100%](screenshots/volumeicon-100.png),
[Preferences 150%](screenshots/volumeicon-150.png), plus foot/QTerminal/xterm
at both scales in the same directory. Source runner:
`installer/tests/check-wayland-polish.py VM PASSWORD_FILE OUTPUT_DIR`.
Final fresh-ISO regression is still pending until all polish items
are addressed. Temporary diagnostics were removed from the compositor build
source and guest; the guest again runs production compositor 2bde47ee.
Development runtime changes are staged only on the existing QEMU
`wayland-session-iso` VM, not on the preserved final acceptance installations.

## Failed commands and workarounds

- Initial `Get-Content docs/WAYLAND_POLISH_WORKORDER.md` preceded the fetch and
  found no file. Fetched/fast-forwarded master, then read the new work order.
- PowerShell literal wildcard paths `wexcomp/src/wex*`,
  `wine/dlls/winewayland.drv/wex*`, `src/dlls/wexshell/*.h` were rejected by rg.
  Search the directory and use `-g` filters. Guessed paths
  `wine/dlls/uxtheme/nonclient.c`, `wine/dlls/win32u/nonclient.c` and
  `wine/dlls/win32u/window_surface.c` do not exist: actual implementations are
  uxtheme/window.c, win32u/defwnd.c and win32u/dce.c. The ignored
  `prepare-real-apps.sh` helper also did not exist; inspected existing helpers.
- `ls /tmp/*state*exe` found no prior state executable. Staged the existing
  wayland-window-control fixture explicitly.
- Inline serial `reg query HKCU\Software\Microsoft\Windows\CurrentVersion\ThemeManager`
  lost its backslashes through shell parsing (`reg: Invalid system key`).
  Read the ThemeManager section of the test user's registry file instead.
- Cross-process GetWindowDC/GetPixel returned CLR_INVALID, so it is not used
  as pixel evidence. A local themed DIB and actual compositor buffer readback
  establish the rendering boundary instead.
- Debug compositor compilation first assumed `struct view.title`, but title
  belongs to a different struct. Removed the title filter and traced actual
  hosted buffers. wlroots headers are under wlroots-0.18, not the guessed
  wlroots-0.19 path. The diagnostic build then succeeded.
- SCP of the original compositor source restored its contents with CRLF,
  yielding an 821/821-line diff despite an empty ignore-EOL diff. Restored only
  that owned diagnostic file from its original Git HEAD and rebuilt; source
  is clean. No temporary compositor code is retained in the project.
- The first matrix matched `Polish-xterm`, but Debian's shell changes its
  title to `bobsmith@WEX-PC: ~`. Use xterm's `allowTitleOps:false` resource
  for the named test window. Earlier passing checks remain recorded in the
  failed log, not counted as an overall pass.
- The second matrix could not open the 150% volumeicon menu (native coordinate
  bug above). After that fix, the next run asserted ACTIVE while the initial
  group was NOT_READY. The runner now waits for adoption. Actual adoption
  still failed with `could not place window ... showing it without a frame`;
  that persistent failure exposed the rounding issue above.
- Root `DISPLAY=:0 xdpyinfo` failed X authorization. Native checks must use
  session-run.py with the actual user's session environment.
- Git emitted `[attr]generated ... not allowed: wine/.gitattributes:1` while
  fast-forwarding the parent with a modified submodule. The merge completed;
  no attribute/resource changes were made to address that diagnostic.
- The first evidence-copy command used two space-separated New-Item paths.
  PowerShell rejected the second positional argument; dependent copies and
  redirection then failed because the directories did not exist. Created an
  explicit `-Path` array and repeated the copies with ErrorActionPreference
  Stop. All raw evidence files are now present.

The initial matrix also placed a tall foot window partly below the screen
after dragging it to a fixed y=80. The final runner derives the target from
the actual frame size so all screenshots fit above the taskbar. This is a
fixture positioning correction, not an application placement fix.

Jules's PRs merged as 93665eec and d692e6ce while this investigation ran.
Pulled both, preserving Gemini's art changes and the user's unrelated
`.gitignore` edit. The latest combined build includes Jules's changes.
