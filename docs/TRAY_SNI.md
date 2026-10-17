<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Native notification icons

Part C: the final portable real-program tray runner passes in both X11 and
Wayland, including left-click menus and wheel input. The final clean-ISO
regressions also pass in both sessions. Review point 2 is ready.
Signed-in Steam close/reopen/menu is reserved for the user's manual login,
as requested on 2026-10-17. This manual acceptance is not claimed passed.

The final clean ISO built successfully on the Proton VM:
`/srv/protonexplorer/wex-wayland-l1/wineexplorer-tray-final.iso`, SHA256
`5d37cfb529ed00902a68df064975ba0986d0e0a7f25a7c7043f141484e2b5923`.
It includes Wine b1d3ab0, compositor 2bde47ee and the final owned tray fix
0c1084eb. Build log: `checks/tray-final-iso-build.log` in the same VM root.
Fresh X11/Wayland acceptance used this image, with no DLL hot-staging.

## Final ISO regression checkpoint

Fresh X11 installation and tray acceptance pass on that exact image. Logs:
`build/wayland-session/final-x11-install.log` and `final-x11-tray.log`.
The latter ends with both XEmbed and SNI PASS lines. The former includes
32/64-bit programs, Fluent listed by the actual Display Properties control,
shared-app access/denial and offline account/prefix assertions.

The same installed X11 runtime passes the existing tray-bounds and eight-dialog
probes on isolated 800x600 Xvfb, then the existing composition probe at 1024x768
with Composition=0 and =1. Each mode passes five blends with restored styles
and attributes and all three capture paths. Evidence is under
`build/wayland-session/final-x11-regressions/` (archive alongside it).
With composition enabled, background and color-key hole both read (37,111,149),
the blue 50% window reads (18,95,202), and the intermediate fade (109,158,184).
With composition disabled, the blue window is opaque (0,80,255).
Fresh Wayland installation, the existing real foot/Notepad action runner,
32/64-bit applications, Fluent listing, shared-app permissions and offline
account/prefix checks also pass: `final-wayland-install.log`. Both private
prefixes are 1 MB. Its restarted installed session passes the same XEmbed
and SNI checks: `final-wayland-tray.log`, screenshots `final-wayland-tray/`.
The final install screenshots are copied into `final-x11-install-steps/`
and `final-wayland-install-steps/`. See WAYLAND_SESSION.md for review point 2.

The Unix half of wexhost owns the session-bus StatusNotifierWatcher when no
watcher exists, or registers with the existing watcher. It tracks unique bus
owners, reads item properties on registration and signals, decodes ARGB
pixmaps, and resolves named PNG/SVG/XPM icons through GdkPixbuf without GTK.
The PE event thread posts copied records to the shell UI thread. Icons enter
the existing notification-area add/modify/delete path, including Customize
Notifications and Hide inactive icons. Preferences use the item's stable Id.
Menus use Win32 popup menus and com.canonical.dbusmenu calls.

The XEmbed fallback owns the X11 tray selection without stealing it. It
captures redirected client icons on Damage events and delivers the same tray
records and clicks. Volumeicon now verifies this path under Wayland, including
mute/image updates, tooltip, its popup menu and working Preferences command.
The suggested snixembed program converts SNI **to** XEmbed, the reverse
direction: https://packages.debian.org/sid/snixembed . No third-party source
has been copied into these implementations.

## Historical development verification (2026-10-16/17)

Combined i386/x86_64 Wine build passes at the production pin b1d3ab0 with
the new owned modules. Build log on the Proton VM:
`/srv/protonexplorer/wex-wayland-l1/checks/tray-fixes-build.log`.

On the installed `wayland-session-iso` QEMU VM, the development DLLs were
staged into `/usr/local/lib/wine` and LightDM restarted. This VM is therefore
no longer an unchanged copy of the part B ISO. Its original installation
evidence remains in WAYLAND_SESSION.md. Neither the ISO nor its checksum was
changed. Test-only packages include network-manager-gnome, volumeicon-alsa
and xpad. No credentials or account login were submitted.

- The watcher reports `IsStatusNotifierHostRegistered = true`.
- Real `nm-applet --indicator` registers through libayatana; its icon draws.
- Its right-click menu uses XP/Win32 rendering, with disabled entries,
  checkmark, separator and VPN submenu.
- Selecting Connection Information opens its actual native GTK dialog.
- Xpad also registers through libayatana; it is not an XEmbed test.
- Volumeicon's XEmbed icon uses its ARGB visual; its common Win32 tooltip
  displays `volumeicon`. Left click mutes it and Damage updates the image.
  Right click opens the native popup; Preferences opens its actual dialog.
- NetworkManager's normal `Network` tooltip displays. Its Win32 menu's VPN
  submenu is fetched on opening, including AboutToShow. Its Connection
  Information command works; disabling/enabling networking through the actual
  NetworkManager service updates the tray icon through signals. Networking
  was restored. Exiting nm-applet removes its watcher registration and icon.

Real Wayland output: `build/wayland-session/tray-wayland-real.log` records
the XEmbed passes, then an overly exact OCR match failed on the gray menu
heading. `tray-wayland-sni.log` records the subsequent successful SNI checks.
This distinction is deliberate: the first whole runner did not pass. The
corrected whole runner is being used for X11 and the final ISO.

Additional evidence: `tray-wayland-volume-tooltip.png`,
`tray-wayland-volume-before.png`, `tray-wayland-volume-after.png`,
`tray-wayland-volume-preferences.png`, `tray-wayland-nm-submenu.png`,
`tray-wayland-nm-information.png`, `tray-wayland-nm-disabled.png`, and
`tray-wayland-nm-removed.png` in the same ignored staging directory.

Evidence under gitignored `build/wayland-session/`:
[indicator/menu](../build/wayland-session/tray-nm-menu-initial.png),
[working menu command](../build/wayland-session/tray-nm-information.png),
[Xpad indicator](../build/wayland-session/tray-xpad-initial.png).

At this development checkpoint, final ISO regressions were still required;
they are now completed above.
Signed-in Steam acceptance is a manual check; no credentials
have been requested or submitted. Existing-watcher fallback is implemented;
no synthetic watcher client or protocol corner-case suite was added.

The reusable checker is `installer/tests/check-native-tray.py VM PASSWORD_FILE
STEPS_DIR`. Run in Bob's default 1280x800/100% installed session. It installs
only real test applications, checks mute and wheel changes against the actual
dummy ALSA mixer, takes screenshots, exercises both left/right SNI menus and
lazy submenus, then verifies signal updates and process/registration removal.
It uses `session-run.py --native`, which preserves the active X11 DISPLAY
(LightDM can choose :1) and strips shell Wine variables. For the controlled
Wayland VM, its public endpoint defaults can be overridden with
`--native-wayland` / `--native-display` on that helper.

The final runs are `build/wayland-session/tray-x11-final.log` and
`tray-wayland-final.log`; both end with the XEmbed and SNI PASS lines.
Screenshots are under `tray-x11-final/` and `tray-wayland-final/`.
Steam's signed-in check remains manual by the
user's explicit instruction. No test credentials have been requested.

## Failed commands and fixes

- A one-pixel ImageMagick `-format` argument passed through PowerShell/WSL
  lost protective quoting around `{400,350}` / `{200,400}`. Bash expanded
  the braces, producing `Expected operand at 'p400'` / `'p200'` and a
  decode-delegate error. Read each pixel with `-crop 1x1+X+Y txt:-` instead;
  the earlier three-pixel format with spaces had worked.

- Final fresh X11 installation reached the added Fluent check after passing
  login, both architectures and shared-app permissions, then stopped at
  `split: cannot open '/root/WineExplorer/scripts/cpl_driver.exe'`.
  That upstream test addition assumed a prebuilt helper and the removed
  desktop_disp/desktop_xauth files. Build the existing cpl_driver.c with the
  same MinGW toolchain as the other fixtures, and use session-run.py for
  both Control Panel and the driver. Scope cleanup to the desktop user.
  The first interrupted log is preserved as `final-x11-install-first.log`;
  rerun the full fresh installation with the corrected checker, using the
  same ISO (test-only change, no runtime hot-staging or ISO rebuild needed).

- Part D preflight on the hot-staged installed VM passes the existing tray
  bounds and eight-dialog checks in an isolated 800x600 Xvfb/prefix, followed
  by the existing D1/D2 composition probe at 1024x768. Five blend operations
  restore styles/attributes, and compatible, DIB and scaled captures succeed.
  Evidence: `build/wayland-session/preflight-regressions/`. This is preparation,
  not the required final untouched-ISO result. The isolated helper initially
  returned 1 after passing all dialog cases: its EXIT trap called
  `wineserver -k` again after the server had already stopped. Make that
  cleanup idempotent. Temporary diagnostic output confirmed each existing
  dialog assertion returned zero. Also set Bob's HOME/XDG_RUNTIME_DIR instead
  of preserving root's values, avoiding shader-cache and runtime-dir warnings.
- PowerShell `rg docs/COMPOSITION*` rejected the wildcard as an invalid path;
  pass the directory and use `-g` filters instead. A search also named the
  nonexistent `src/dlls/wexshell/linuxwm.c`; search the actual directory.
- An attempted WAYLAND_SESSION.md patch used a bare `#` context line and
  failed verification without changing the file. Reapplied against its
  actual title and review-report paragraph.
  Two later combined final-doc patches also failed atomically because their
  last TRAY_SNI.md context used `Final install screenshots` / `Final Wayland
  installation` instead of the actual text. Removed the empty trailing hunk
  and reapplied the handoff/stock updates; no partial file edits occurred.
- Two progress-only `ls` checks guessed `binary/live/filesystem.squashfs`
  and `filesystem.squashfs` locations under the work directory; neither
  existed at the check time. The build continued successfully; use its log
  and final output image rather than guessed intermediate paths.

- The expanded real checker caught libayatana's omission of both ItemIsMenu
  and Activate on nm-applet. Introspection confirmed this, so missing
  ItemIsMenu now defaults to true, following the existing Waybar convention.
  Explicit false still selects normal Activate for compliant SNI clients.
  This makes nm-applet's left-click menu work without an application-name
  exception. Reference: Waybar include/modules/sni/item.hpp.
- LightDM allocated DISPLAY=:1 for the final X11 run. The old ignored helper
  hardcoded :0 and volumeicon could not connect. The reusable --native helper
  reads Explorer's actual session environment; no product launcher fix needed.
- After a previous mute, Dummy Master retained zero volume. Muting an already
  zero-level icon cannot change its picture. Reset the real dummy mixer before
  each test. volumeicon also normalizes ALSA's dB range, so compare the real
  mixer direction for wheel input, not an assumed raw 80-percent result.
- One new-runner invocation omitted VMTEST_DIR and searched /tmp/vmtest.
  Set VMTEST_DIR=/var/tmp/wineexplorer-vmtest for this test workspace.
- The first tray ISO finished but predates the ItemIsMenu compatibility fix.
  It is superseded and must not be used as final acceptance. Build a new clean
  ISO including that fix before part D's install checks.

- Whole-screen OCR omitted the small tray tooltip, then read `volumeicon`
  as `voiumeicon`; crop the tooltip region and normalize the i/l glyph
  ambiguity. Screenshots verify the actual text. The gray disabled menu
  heading was read as `Ethernet Ne`, so wait for the enabled `Disconnect`
  entry instead. OCR also missed `Close` in the native dialog; close it
  with the normal Alt+F4 action. No product changes were made for these.
- The new ISO build initially failed executing package-wine.sh: Permission
  denied. A Windows tar of edited files had lost its executable bit.
  Restored that one file's mode in the Linux build source and restarted the
  dedicated clean ISO build. The original part B ISO remains outside its
  work directory and was preserved.

- Pulling the new Fluent merge initially stopped because the pre-existing
  `.gitignore` `*.tmp` edit overlapped upstream Python ignore additions.
  Stashed only that file, merged master, restored both ignore additions,
  left `*.tmp` unstaged and dropped only the temporary stash. No work lost.
- `apt-get install parcellite` failed: package unavailable in Debian 13.
  Use the work order's volumeicon instead. This VM has no sound device;
  test-only `modprobe snd-dummy` supplies its ALSA mixer without host audio.
- Starting apps immediately after clicking Bob raced session startup:
  `Cannot open display`. Wait for the desktop/watcher, then launch them.
  Running the session selector before the Welcome screen was ready also
  failed OCR and selected default X11; the resulting X11 session was used
  for its tray check. Wait for `To begin` before choosing Wayland.
- Xpad in Debian 13 uses libayatana, so it cannot stand in for an XEmbed-only
  client. Volumeicon's X11 plug and owner/child tree verify actual XEmbed.
- The initial XEmbed image lost alpha: without `_NET_SYSTEM_TRAY_VISUAL`, GTK
  chose a default-depth visual. Advertise a 32-bit visual before announcing
  the manager. XGetImage on a named pixmap also has zero RGB masks: use the
  actual client visual's masks. Read-only pixel inspection of volumeicon
  confirmed the alpha-only black speaker bitmap.
- Volumeicon selects XI2 and ignores XSendEvent core clicks. Use optional
  libXtst for clients without a selected core ButtonPress mask. Its actual
  Open Mixer/Preferences/About/Quit popup now appears under Wayland.
- Default-depth XEmbed parents could show a black square on XWayland after
  replaying a click: opacity on override-redirect windows is not portable.
  Use a transparent ARGB parent and manually redirect the embedded child,
  with an empty input region except during server-generated clicks.
- Waiting for `Volume Control` after selecting Preferences timed out; the
  screenshot did not show a preferences dialog. Do not count that command
  as passed; further XEmbed interaction checks remain.
- The KDE source URL `master/xembedsniproxy/sniproxy.cpp` returned 404.
  Consulted Plasma/5.27/xembed-sni-proxy instead (LGPL-2.1-or-later), for
  bridge behavior, including XI2 click injection. No source copied.

- Initial tray_x11.c build: `X11/extensions/Xdamage.h: No such file or directory`.
  Used the public runtime ABI declarations, following existing compose.c;
  no new stock configure dependency.
- Next build: `processthreadsapi.h:133: expected identifier ... numeric constant`.
  Undefine Xlib's ControlMask before Wine headers, as existing x11wm.c does.
- A serial inline shell loop passed through Windows/WSL lost `$arch` and
  `$module`: `install: cannot stat '/tmp/dlls//-windows/.dll'`. Moved the
  operation into ignored install-tray.sh served over the staging HTTP server.
  The corrected script installed both architectures before the test login.
- `vmtest.py click ... --button right` is invalid; button is positional.
- A serial command ending in `&` became `&; echo` in vmtest's completion
  wrapper and failed with `syntax error near unexpected token ';'`.
  Use a script or end the command with `& true`.
- Initial SNI right-button handling fell through DefWindowProc and opened
  a second taskbar menu. Returning zero after handling native input fixes it.
- freedesktop.org's SNI specification URLs returned HTTP 403. Consulted
  Waybar's upstream watcher/item source for protocol behavior; no code copied.
