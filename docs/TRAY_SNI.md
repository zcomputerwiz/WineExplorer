<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Native notification icons

Part C is in progress; part D and review point 2 are not ready.

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
records and clicks. This implementation still needs real XEmbed acceptance.
The suggested snixembed program converts SNI **to** XEmbed, the reverse
direction: https://packages.debian.org/sid/snixembed . No third-party source
has been copied into these implementations.

## Verified so far (2026-10-16)

Combined i386/x86_64 Wine build passes at the production pin 0f25ee1 with
the new owned modules. Build log on the Proton VM:
`/srv/protonexplorer/wex-wayland-l1/checks/tray-build.log`.

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

Evidence under gitignored `build/wayland-session/`:
[indicator/menu](../build/wayland-session/tray-nm-menu-initial.png),
[working menu command](../build/wayland-session/tray-nm-information.png),
[Xpad indicator](../build/wayland-session/tray-xpad-initial.png).

Still required: XEmbed real app, Steam close/reopen/menu, tooltip and
signal-driven status/removal, existing-watcher behavior, menu refresh,
both session acceptance, then the work order's final ISO regressions.

## Failed commands and fixes

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
