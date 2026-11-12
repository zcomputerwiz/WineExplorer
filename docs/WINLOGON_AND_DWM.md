# Logon and window composition: plan

Status (2026-10-12): W1-W3 and W7 done (LightDM/logonui.exe on the dev VM and verified installed images); W4 next. D1 compositor core and D2 effects (shadows, blend fades, capture) merged, off by default. This plan covers two projects:

- **Winlogon**: XP's logon experience (Welcome screen, classic Log On to Windows dialog, Lock Computer, Switch
  User / fast user switching, Windows Security) on top of a real Linux display manager.
- **Desktop Window Manager**: an optional compositing manager in Explorer, so that XP features which need alpha
  composition (layered windows, fades, drop shadows) work, followed by live thumbnails and tear-free output.

The two projects do not depend on each other and can proceed in parallel. Each phase ends in a working, committed
state.

## 1. Where Windows and Linux put things

Windows XP spreads logon across a hierarchy of privileged services. The same roles already exist on Debian, so we keep
the Linux parts for everything that needs privilege and write only the user interface.

| Windows XP | Role | Here |
|---|---|---|
| Service Control Manager (`services.exe`), LocalSystem | starts and supervises system services | systemd, root |
| Winlogon (one per session, runs as SYSTEM) | starts sessions, Secure Attention Sequence, locks, user switching | LightDM daemon (root) |
| LSA (`lsass.exe`), MSV1_0 | checks passwords, builds the logon token | PAM (`pam_unix`), called by LightDM |
| Terminal Services sessions | one session per logged-on user (fast user switching) | systemd-logind sessions; LightDM runs one X server per session on its own VT |
| LogonUI.exe / MSGINA.dll (runs as SYSTEM in XP) | Welcome screen, Log On to Windows dialog, Unlock dialog | **our greeter**: `logonui.exe` in Wine, run by LightDM as the unprivileged `lightdm` service account |
| user profile (`ntuser.dat`, Default User template) | per-user settings | the user's Wine prefix (`~/.wine`); a template for new users is a later phase |
| `userinit.exe`, `Shell` value | starts the shell | `wine-explorer-session.sh` (already reads `Winlogon\Shell`) |

**Do we need Windows services or service accounts inside Wine first?** No. Wine's `services.exe` keeps running per
user prefix as it does now, and no Wine code runs as root. Two Linux-side changes are needed:

1. **LightDM replaces GDM** (phase W1). GDM's greeter is always GNOME Shell and cannot be replaced; LightDM is built
   for third-party greeters, does fast user switching (`Seat.SwitchToGreeter`, `SwitchToUser`, `Lock` on D-Bus), and
   is much lighter, which suits the minimal install.
2. **The `lightdm` service account gets a Wine prefix** (phase W2), created by root at install time in its home
   (`/var/lib/lightdm`), so the greeter starts quickly and never runs `wineboot` in front of the user.

**Licensing.** The LightDM daemon (GPL-3) is a separate program, which our GPL-2.0-or-later code may talk to. The client
library `liblightdm-gobject` is LGPL-3+ (Debian's copyright file), which we do not link or `dlopen`: wexhost speaks the
greeter protocol (a small binary message format over two pipes the daemon passes in `LIGHTDM_TO_SERVER_FD` and
`LIGHTDM_FROM_SERVER_FD`) with its own code. The user list comes from AccountsService and logind over D-Bus, which
wexhost already uses. ReactOS `base/system/logonui` (the NT5 Welcome frame) and `dll/win32/msgina` (dialogs, which to
show when) are GPL and may be ported.

**Rejected:** our own display manager (it would re-implement PAM, session setup, X authorization and VT switching,
where mistakes are security holes); greetd (no user switching); running the greeter inside the user's session (the
session does not exist yet, and a crash would expose a locked desktop).

## 2. Winlogon phases

### W1: LightDM in place of GDM (infrastructure)
- Dev VM: install `lightdm` with `lightdm-gtk-greeter` as the interim greeter, autologin for `wineuser` into
  `wine-explorer`, disable GDM. A text login on tty3 stays available as the way back in.
- wexhost: Switch User asks LightDM (`org.freedesktop.DisplayManager.Seat.SwitchToGreeter` on `XDG_SEAT_PATH`), GDM
  stays the fallback. New action **Lock** (`Seat.Lock`), with **Windows+L** in Explorer. XP shows the Welcome screen
  when locking with fast user switching on, which is what LightDM's lock does.
- Session scripts and VM helpers stop assuming GDM (`vm-clean-wine.sh` restarts `display-manager`, the X authority path
  comes from the session).
- Test: autologin to the shell; Start > Log Off > Switch User shows the greeter while the session keeps running;
  logging in again returns to it; Windows+L locks; Log Off and Turn Off still work.

**Done (2026-10-12).** `scripts/setup-default-session.sh` (run as root) installs
`scripts/lightdm-wineexplorer.conf` and makes LightDM the display manager. Verified on the dev VM: autologin into the
shell; Windows+L and Switch User show the greeter (LightDM: "Switching to existing greeter") with the session and its
windows intact; `sudo loginctl activate <session>` returns to it without a password for tests; Log Off ends the session.
Turn Off was not exercised (unchanged path). Found on the way:
- A startup race lost the session every time under LightDM: `wineboot --init` starts `winemenubuilder`, whose window
  makes Wine start its default desktop before ours, and our `explorer /desktop=Default` then fails (`aborted by
  WM_NCCREATE`). The session script now disables `winemenubuilder`. GDM's slower start hid it most of the time; it is
  likely the cause of the "often ends in the login screen" note in `scripts/ensure-shell.sh`.
- A `~/.Xauthority` left over from GDM held wildcard entries (`<host>/unix:` without a display number) that win over
  LightDM's new `unix:0` entry: "Invalid MIT-MAGIC-COOKIE-1 key". Delete the file once when switching.
- GDM is disabled but still installed on the development VM. W7 removes it from new installer images.

### W2: greeter skeleton (`logonui.exe`)
- `programs/logonui` (XP name) and a greeter link in wexhost: the LightDM greeter protocol (connect, authenticate,
  prompts, start session, cancel), the user list (AccountsService `ListCachedUsers`, `RealName`, `IconFile`), logged-on
  state (logind `ListSessions`).
- `/usr/share/xgreeters/wineexplorer.desktop` runs `wex-greeter.sh`: `WINEPREFIX=/var/lib/lightdm/.wine`, own virtual
  desktop the size of the screen, `logonui.exe`. The pipes must reach the Wine process: verify that the descriptors
  survive `wine` and the Wine process start (else the script passes them by number in the environment and wexhost
  opens them from `/proc/self/fd`).
- Plain UI first: user list, password field, error text, Turn Off (login1 allows it from the greeter's session).
- Keep `lightdm-gtk-greeter` installed and switchable in `lightdm.conf` while the new greeter matures.

**Done (2026-10-12).** `wexhost/greeter.c` speaks the protocol (`WexGreeterConnect/Authenticate/Respond/Cancel/
StartSession/Read`), `WexUsersList` reads AccountsService; `programs/logonui` is the plain greeter.
`scripts/setup-greeter.sh` (root) installs `wex-greeter.sh` and `/usr/share/xgreeters/wineexplorer-greeter.desktop`
and makes the prefix `/var/lib/lightdm/.wine-logon` (since 2026-11-08 `/var/lib/lightdm/.wine`, the account's
`~/.wine`, which the system role requires; `docs/SAFE_MODE_WORKORDER.md` C7); `setup-greeter.sh use` selects it, `setup-greeter.sh gtk` goes
back. Verified on the dev VM:
- The pipes reach the Windows process through `wine`, `explorer /desktop` and its child (checked with `/proc/<pid>/fd`).
- Windows+L shows logonui (LightDM: "Greeter connected version=wineexplorer-logonui"), listing the AccountsService
  accounts.
- A wrong password: PAM's "Authentication failure" comes back and logonui shows XP's "The system could not log you
  on" message.
- A right password for a second account (`wextest`, test account on the VM; password in the gitignored
  `build/w2/test-account.txt`) starts that user's shell on its own VT while `wineuser`'s session keeps running
  (fast user switching).
- Not exercised: Turn off computer from the greeter (it would power the VM off); returning to an existing session
  through the greeter (needs `wineuser`'s password; `loginctl activate` was used instead).
Notes: `Seat.Lock` sends `lock-screen=true` without `select-user`, so nothing is preselected; that matches XP's
Welcome screen with fast user switching (the "computer is locked" text belongs to classic logon, W4). Debian sets
`greeter-hide-users=true`; the Welcome screen ignores that hint, classic logon (W4) will honour it. A fresh user's
prefix (`wextest`) showed no Recycle Bin icon on the open art set: to check.

### W3: the Welcome screen
- The XP Welcome screen: the frame and the strips (ReactOS `NT5design.c`), user tiles with pictures, the password
  bubble and arrow button, "To begin, click your user name", the password hint ("Did you forget your password?",
  AccountsService `PasswordHint`), "N programs running" for users with a session, Turn off computer, the fades.
- After a lock, the Welcome screen opens on the locked user.
- Pictures stay open (ReactOS or our own); genuine pictures only through the local repository.

**Done (2026-10-12).** `logonui.exe` paints the Welcome screen: bands, strips, shine and divider from ReactOS, a tile
per account with its AccountsService picture (GDI+; else the default picture) and "Logged on" (logind `ListUsers`,
fourth field of `WexUsersList`). Clicking a tile starts the authentication at once; when PAM asks for a password the
tile opens with the password box, the arrow and the help button (accounts without a password log on directly). A
wrong password shows XP's "Did you forget your password?" balloon and asks again. Turn off computer runs
`explorer.exe /turnoff`, XP's Turn Off Computer dialog over the grayed screen. Keyboard: arrows, Enter, Escape. The
resource numbers are XP's, so a local build can drop XP's pictures in. Verified on the dev VM: layout against the XP
VM's Welcome screen, wrong password and balloon, logon of `wextest`, Turn Off dialog and Cancel.
Found on the way: the Turn Off dialog's open background was ReactOS's dark one, on which the XP-white labels did not
show (in every open build): `open_assets.py` now draws an XP-blue one. Our `.rc` files listed no pictures for
makedep, so a changed picture never rebuilt the resource: `scripts/rc-makedep.py` added the `@makedep` lines (run it
after adding pictures; `--check` reports missing ones).
Not done: "N programs running" (needs a per-session count), the password hint text (`PasswordHint` is admin-only in
AccountsService), the fades, scrolling for more accounts than fit, the open logo (wanted art, `docs/ASSETS.md`).
Planned (2026-11-08, `docs/SAFE_MODE_WORKORDER.md`): hidden accounts read from the machine registry
(`Winlogon\SpecialAccounts\UserList`, C7), and a dedicated Administrator account that is hidden in a normal start
and shown in Safe Mode (C4). C7 first links the logon prefix's `system.reg` and `userdef.reg` to the machine root:
today the logon prefix has a private hive and sees none of the machine's `HKLM`.

### W4: classic logon (`LogonType` 0)
- "Welcome to Windows: press Ctrl+Alt+Delete" (unless `DisableCAD`), the Log On to Windows dialog with the options
  button, Computer Locked and Unlock Computer, as in ReactOS `msgina` and the XP VM.
- System settings live outside any user prefix (`/etc/wineexplorer/winlogon.conf`, readable by the greeter); the
  greeter maps them to the XP values (`LogonType`, `DisableCAD`, `DontDisplayLastUserName`, `LegalNoticeCaption/Text`).
  To decide when W4 starts (2026-11-08): once the logon prefix reads the machine's `HKLM`
  (`docs/SAFE_MODE_WORKORDER.md` C7), these values can come from `HKLM\...\Winlogon` as on XP, instead of a separate
  file. The owner asked for the hidden-account list to come from the registry, which points that way.
- Classic logon accepts hidden accounts by name, the Administrator account included (C4 and C7 of the Safe Mode
  work order).

### W5: inside the session
- Ctrl+Alt+Delete in the session: Task Manager with the Welcome screen on, the Windows Security dialog (Lock, Log Off,
  Shut Down, Change Password, Task Manager) with classic logon, as XP does.
- `LockWorkStation` (stock stub in user32) asks the shell to lock (small stock change).
- Change Password through PAM (`chauthtok`) in wexhost.

### W6: accounts and profiles
- User Accounts (`nusrmgr.cpl`) through AccountsService: create, delete, account type (administrator = `sudo` group),
  picture, password, with the polkit agent we already have. Whether it also leaves out the registry's hidden accounts
  is to be checked on the XP VM (`docs/SAFE_MODE_WORKORDER.md` C7). It sets the Administrator account's password on
  existing installs, where the package creates that account locked (C4).
- "Use the Welcome screen" and "Use Fast User Switching" options write the system settings through a polkit action.
- Automatic logon (`AutoAdminLogon`, `DefaultUserName`) maps to LightDM `autologin-user`.
- A Default User prefix template, so a new user's first logon shows XP's "Applying your personal settings" instead of
  a long `wineboot`.

### W7: installer
- Package and OOBE switch from GDM to LightDM and our greeter; OOBE creates the first user through AccountsService.
- Implemented: installer packages the custom greeter and GTK recovery greeter, selects LightDM,
  and prepares the lightdm account's isolated Wine prefix on first use. Text setup clears both live-config's main
  configuration and the live autologin snippet. Debian 13 AccountsService writes LightDM's automatic login setting
  through the existing WexUserSetSession D-Bus call; multiple users get the Welcome screen.
- The display manager is gated by the setup-stage file instead of conflicting with enabled setup units, whose
  skipped conditions could cancel the live desktop's startup job. Removed stale duplicate setup units from includes.
- Native text setup now copies directory ownership and permissions from WinntDirectories, including empty service
  homes. Without this, LightDM's home was root-owned and its X authority write failed on the installed system.
- Generate setup tables from the immutable squashfs payload; the mutable build chroot added directories not present
  on the live filesystem. Native directory regression tests and a full 5,015-directory preflight pass.
- Fresh BIOS/virtio installations passed wrong/correct password authentication, Win+L, passwordless user switching,
  unattended automatic login, normal shutdown and read-only account/ownership/cleanup checks. Live CD boot/shutdown
  also passes. See INSTALLER_TEST_REPORT.md for exact final artifacts and limits.

## 3. Desktop Window Manager

### Coordinated application graphics follow-up (2026-10-18)

`CHROMIUM_GRAPHICS_PLAN.md` extends this composition program with Wine's
application-facing DComp/DXGI implementation. D1/D2 remain the outer desktop
effects/capture layer; D3 presentation and optional D4 APIs coordinate with the
new work's surface lifetime, damage, pacing and capture contracts. Neither layer
replaces the other. No second desktop compositor is planned. Decision of 2026-11-02 (owner): the earlier "no `dwm.exe`" rule applied while the goal was XP parity. The goal now includes the Windows Vista to Windows 11 look and applications that use the Desktop Window Manager, so a thin `dwm.exe` is allowed and planned as the Windows-facing DWM state and policy service (see `DWM_PLAN.md`, section 1.0). It is not a second pixel compositor: it must never own an X Composite selection or a second Wayland scene, and applications must keep working when it is absent.

Preserve X11 wexhost composition and the Wayland ownership split in
`WAYLAND_L2_WORKORDER.md` (wexcomp composes native surfaces and owns physical
input). OpenGL acceleration on older Intel iGPUs and low-end AMD/Nvidia is a
first-class target; Vulkan is optional and capability-gated. This is an updated
plan, not evidence of completed graphics or new hardware acceptance. Historical
phase results below remain scoped to their recorded tests.

XP has no DWM, so composition is off by default. Its first purpose is XP parity: XP composes layered windows in win32k, and Wine leaves that to an X
compositing manager, which our session does not have.

### What happens today (from the code; appearance on the VM not yet checked)
- The virtual desktop window is a child of the X root; every Win32 top-level window is a child X window of the desktop
  (`winex11.drv/desktop.c`, `window.c` `create_whole_window`). Linux and Proton windows are reparented into their Win32
  host; menus, tooltips, docks and notifications from Linux stay on the root.
- `UpdateLayeredWindow` draws into a bitmap cleared to black and uploads it to an ARGB window. Nothing blends it with
  what is behind, so partly transparent pixels show their raw colors (dark fringes, black shadows). Fully transparent
  pixels and color keys become an X shape and do work.
- `SetLayeredWindowAttributes(LWA_ALPHA)` only sets `_NET_WM_WINDOW_OPACITY`, which nothing reads: uniform
  transparency is ignored.
- `CS_DROPSHADOW` is set on the menu class but nothing draws a shadow; `AnimateWindow` is a partial stub, so menu and
  tooltip fades need work beyond the compositor.
- Wine already uses the Composite extension, but only to draw OpenGL/Vulkan child surfaces off screen
  (`winex11.drv/init.c`); those windows must not be redirected a second time.

### Design
- The compositor lives in wexhost's Unix side and shares the window manager's X connection and event thread
  (`x11wm.c`, already serialized by `x_mutex`). Software rendering with XRender, Damage and XFixes, so it works on the
  VM (Xorg without acceleration); OpenGL can come later.
- Two levels of manual redirection: the children of the X root (the Wine desktop, Linux menus and tooltips) and the
  children of the Wine desktop (each Win32 top-level window and Linux host). Each frame paints the desktop's own
  pixmap (wallpaper and icons), then the desktop's children bottom to top with alpha and opacity, then the root's other
  children, into a back buffer that goes to the Composite overlay window. The overlay takes no input (empty XFixes
  input shape).
- Stacking comes from `XQueryTree`; Wine already keeps X sibling order equal to the Win32 z-order.
- The compositor owns `_NET_WM_CM_S0`, so GTK and other clients use ARGB visuals.
- A full-screen window (games) is unredirected at both levels and the overlay is hidden, so games keep their latency.
- PE side: `WexCompositionSetEnabled`, later thumbnail calls in the style of `WexWmGetIcon`; X window IDs stay on the
  Unix side. The switch is in Performance Options (System Properties > Advanced), stored per user.
- References: xcompmgr and compton (MIT/X11), picom (MIT and MPL-2.0, per file; check each file before porting).

### Risks
- The desktop DC reads the screen with `IncludeInferiors`; with redirected children, BitBlt from the desktop
  (screenshots, Print Screen, some drag images) would miss the windows and needs a compositor-backed capture.
- Linux ARGB clients inside opaque hosts may need their own composition and a cutout in the host.
- Pixmaps must be renamed after every resize and map; event masks must be merged with the window manager's.
- XRender with a back buffer is not tear-free by itself; that needs the Present extension or OpenGL.

### Phases
- **D1: core compositor**, default off: both redirection levels, alpha, opacity, shapes, damage-driven repaint, clean
  enable and disable. Proves that layered windows blend correctly. Test on Xvfb with a probe: alpha ramps, color keys,
  `LWA_ALPHA`, overlapping moving windows, input through the overlay, resizing, repeated enable/disable.

  **Implemented, awaiting build and VM validation (2026-10-12).** `wexhost/compose.c` shares the WM's Display,
  mutex and event loop, with root/desktop manual redirection, XRender back buffer and input-transparent overlay,
  Damage regions, bounding shapes, ARGB and `_NET_WM_WINDOW_OPACITY`, selection ownership and cleanup. The desktop
  XID comes directly from `WexWmStart` (`linux_windows_start` reads Wine's `__wine_x11_whole_window` desktop property);
  no window-name search is used. Set HKCU\Software\WineExplorer\Shell `Composition` (DWORD) to 1 and restart the shell;
  missing/0 leaves it off. It currently requires `ManageLinuxWindows` enabled and a successfully started WM.
  `WexCompositionSetEnabled` / `WexCompositionGetEnabled` are shell-process APIs, and `scripts/compose-probe.c`
  exercises layered windows, movement, resize/remap, clicks and five API cycles on an isolated display.
  Fullscreen bypass conservatively requires the topmost visible window to cover the entire X screen, with an
  opaque non-ARGB visual, full opacity, no border and a rectangular shape. It suspends both parent redirections
  and releases the overlay until that condition ends; no per-monitor bypass, tear-free presentation, capture,
  or composition of deeper foreign ARGB clients inside opaque hosts. These remain later phases.
  **Tested (2026-10-12, Proton lab VM, Xvfb with Composite, shell started with Composition 0 and 1).** With
  composition the per-pixel alpha ramp blends over the desktop and over other windows (without it: raw colors over
  black), `LWA_ALPHA` 50 % is translucent (without it: opaque), color-key holes stay holes and a click in the hole
  reaches the desktop, the rest of the shell looks the same. Not yet tested: switching at run time and repeated
  enable/disable (the probe's `--own-compositor` mode cannot show its own control window: its window manager takes it
  for a foreign window), full-screen bypass, Linux ARGB clients, repaint cost. On the real dev-VM session (Xorg, no acceleration) the shell starts and works with Composition=1 (checked by eye: desktop, Start menu). Also seen: from a
  process other than Explorer, `GetPropW(GetDesktopWindow(), "__wine_x11_whole_window")` reads 0; the probe takes the
  desktop's X window as an argument instead.
- **D2: XP effects and capture**: desktop capture through the compositor, menu and tooltip fades (`AnimateWindow`,
  `SPI_GETMENUANIMATION`), `CS_DROPSHADOW` shadows, translucent drag images and selection rectangles. Test on the real
  session with Linux ARGB clients.

  **Merged (2026-10-12), tested on Xvfb with the shell and `compose-probe`:** with composition on, the class-shadow
  popup has a soft right/bottom shadow; with it off, none. `AnimateWindow` blend in/out takes about 920 ms, restores
  style and attributes in both modes, and `dwTime` 0 shows/hides at once. K/J/T captures return the composed screen
  (alpha ramp, shadow) at full and half size. Not yet tested: menu shadows (P), fullscreen bypass, runtime toggling,
  Linux ARGB clients, the real Xorg session. Menu and tooltip fades are deferred (see STOCK_CHANGES). The compositor
  caches a small XRender shadow per marked window size, paints it below the window, and scales it with uniform opacity. Moves, caches a small
  XRender shadow per marked window size, paints it below the window, and scales it with uniform opacity. Moves,
  resize, unmap and property changes repaint through D1's damage/stacking machinery; no input window is added.
  `WexCompositionSetShadow(UINT64, BOOL)` marks X windows while composition is enabled. Explorer installs a
  SHOW/HIDE WinEvent hook after enabling composition. For cross-process XIDs that read zero, winex11 publishes
  `_WEX_DROPSHADOW` (class style plus `SPI_GETDROPSHADOW`) before creation/show; compose.c reads that property. Thus
  fresh/remapped popups from other processes work without PID/geometry matching. Already-visible foreign popups
  first shown without a compositor need a hide/show after enabling it; SPI changes take effect on the next show.
  `AnimateWindow(AW_BLEND)` fades through temporary layered alpha, preserving final visibility and uniform-alpha/
  color-key attributes. Existing per-pixel layered surfaces keep immediate show/hide. Automatic popup menu fading
  is deferred: `win32u/menu.c:show_popup` cannot call user32, and sharing the helper plus a compositor gate would
  require a broader stock change. Other animation flags keep their old path. No new translucent drag/selection UI
  is added in this effects/capture spec.
  Screen/desktop BitBlt and image readback use the real root/overlay at the desktop's root offset while a compositor
  owns `_NET_WM_CM_S<screen>`. XRender's scaled/converted screen reads use the same fallback; ordinary window reads
  and all DC writes are unchanged. Stock edits and their gates are listed in `STOCK_CHANGES.md`.
  `scripts/compose-probe.c`: S shadow show/hide, D shadow SPI, B 900 ms blend, U existing alpha blend, 0 zero duration,
  P TrackPopupMenu, K compatible-bitmap capture, J DIB capture, T scaled capture. Captures write `Z:\tmp\capture.bmp`
  and show a preview; the screen DC stays open across toggles. Coordinator: build Wine and the probe, run on the
  real shell with Composition 0/1, check shadow movement/unmap/input, fades/style restoration, menu shadow,
  captures against root screenshots, selection toggles and fullscreen bypass. Exact commands and expected results:
  [D2_TEST_REPORT.md](D2_TEST_REPORT.md). No build, runtime or visual checks were run here.

- **D3: thumbnails and presentation**: thumbnails for the shell (optional taskbar and Alt+Tab previews, off in XP
  mode), unredirecting full-screen windows, Present/vblank. Measure latency, CPU use and tearing on the real Xorg.
- **D4 (optional)**: `dwmapi` (`DwmIsCompositionEnabled`, thumbnails, `DwmFlush`) for Vista-era programs. It changes how
  programs draw (they try glass), so only behind its own setting.

## 4. Order of work

W1 first: it is small, needs no new program, and everything else in the Winlogon track builds on it. D1 can run in
parallel (Codex), since it touches only wexhost's X side and a probe program. Each phase is planned in more detail
in this file when it starts.
