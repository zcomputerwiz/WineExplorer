<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Safe Mode: Remaining Work

Date: 2026-11-08  
Status: plan; the owner's decisions are in section 4 (2026-11-08). Phase 1 (boot modes, Advanced Options menu,
logon, session) is done and tested (`docs/SAFE_MODE.md`, WineExplorer commit `14286423`).  
Branches:

- WineExplorer: `claude/vintner-boot-integration`.
- Wine: `claude/vintner-boot-integration`, created at `cf1c29be` ("E1-S14"), the Wine commit WineExplorer pins.
  It is local and not pushed. Work on it in a separate `git worktree`. The submodule checkout (detached at
  `38820218`) stays as it is, and the token branches (`gemini/token-security*`) and the E1 code are not touched.

## 1. What phase 1 left open

Phase 1 makes the system start in Safe Mode, but Windows programs cannot tell. Wine starts every automatic
service, `GetSystemMetrics(SM_CLEANBOOT)` returns 0, and the desktop looks normal. Some XP pieces are missing or
grayed. The VM test lives only in a scratchpad.

| Area | Missing | Phase |
|---|---|---|
| Tests | Permanent VM test; full ISO install through the package; upgrade from the old Safe Mode | A |
| Sessions | Wayland session in Safe Mode | A |
| Logon | XP classic logon dialog; for now the GTK greeter shows Debian's background | A, C |
| Wine | `SafeBoot\Option` key, `SM_CLEANBOOT`, SafeBoot service lists, `RunOnce` `*` values, Safe Mode desktop paint | B |
| Shell | Black desktop with "Safe Mode" in the corners, the message box after logon, the basic resolution | C |
| Start-up | "WineExplorer did not start successfully" screen, Enable VGA Mode, Last Known Good, Return to OS Choices | D |
| Image | `en_US.UTF-8` is set but never generated (separate task, proposed as its own session) | n/a |

## 2. Constraints

- **The mode cannot travel in the environment.** The machine root's `wineboot --init` and its `services.exe` are
  started by `wex-sysrun` (`dlls/ntdll/unix/env.c:1596`). That launcher passes only `HOME`, `USER`, `LOGNAME`,
  `PATH`, `WINEPREFIX`, `WINESERVERSOCKET`, `LANG`/`LC_*` and the display variables (`tools/wex-sysrun/wex-sysrun.c:293-331`).
  Adding a variable there would change E1 code, so Wine reads the mode from the root-owned file
  `/run/wineexplorer/bootmode` instead (item B0).
- **A Safe Mode boot changes no boot state** (owner decision, 2026-11-07), and neither does any other boot through
  the menu (decision 3). Item D1 must keep that.
- **Changes to stock Wine stay small** and each one is listed in `docs/STOCK_CHANGES.md` (`CLAUDE.md`).
- **Every Wine build is WoW64**, and every step is checked in a VM on BIOS and UEFI with a 32-bit program.
- **XP references.** The F8 menu, the Safe Mode desktop, the message box, the classic logon dialog and
  `ntbtlog.txt` were measured on the XP reference VM in VirtualBox on 2026-11-08 (decision 6). The captures are in
  the local-only repository (`docs/ASSETS.md`), in `../WineExplorer-local/notes/xp/safe-mode/`. Microsoft text and
  art never enter this repository, so the plan describes XP's behaviour and does not quote it.
- **ReactOS code may be ported.** `IntPaintDesktop` in ReactOS
  (`win32ss/user/ntuser/desktop.c:2130-2355` at `e0e659386641`, GPL-2.0-or-later) draws the Safe Mode desktop.

## 3. Work items

### Phase A: make phase 1 complete and repeatable (WineExplorer only)

**A1. Permanent VM test** `installer/tests/safe-mode.sh`, from the scratchpad harness used for phase 1.

Done 2026-11-08: every phase passed on SeaBIOS and OVMF in one run (`docs/SAFE_MODE.md` section 6). The 32-bit
check uses Wine's own 32-bit and 64-bit Notepad, started by `installer/tests/session-run.py`, which now also runs
as the logged-on user. `check-wow64.sh` with its MinGW-built test programs still needs a root serial login and
the compilers. The round 3 image has no prefetch service, so A2 is the first run that checks the Prefetcher's mask.
What was planned:

- Start from an installed disk, as an overlay or from a fresh `install-unattended.sh` run. Put the session
  package on it and run each boot as its own phase: Safe Mode, with Networking, with Command Prompt, Enable Boot
  Logging, Start Normally from the menu, a normal start.
- Compare checksums of `/etc`, `/var/lib/AccountsService` and the accounts' `.dmrc` before and after each boot.
- Keep what phase 1 learned:
  - Send F8 repeatedly through GRUB's one-second window.
  - Wait for "Cancel" on the GTK greeter (OCR misses "Log In").
  - Click Log In at its position.
  - Repeat a key if the screen does not change.
  - Wait for "Recovery Commands" in the recovery console.
- 32-bit check: the installed test VM has no root serial login, so `installer/tests/check-wow64.sh` cannot run.
  Either give test machines a root serial login, or add a non-root mode that starts programs in the logged-on
  user's session (phase 1 used a helper that copies the Explorer process's environment). The WSL distro also lacks
  the MinGW compilers that `check-wow64.sh` needs (`gcc-mingw-w64`, which needs the owner's approval to install).
- Done when the script passes on SeaBIOS and OVMF in one run.

**A2. Full install through the package.** Phase 1 copied files onto an installed disk and ran the postinst block
by hand. Then:

- Build the ISO with the new `wineexplorer-session` package (`installer/package-wine.sh`).
- Run `install-unattended.sh` and A1 on BIOS and UEFI.
- Upgrade test: install from an ISO that has the old Safe Mode, boot it into the old Safe Mode with Command Prompt
  once, install the new package, and check that the postinst removed the leftovers. The leftovers are
  `99-safemode.conf`, the old PAM line, `Session=wex-safe-terminal` and the enabled `wex-safemode.service`.

**A3. Wayland session in Safe Mode** (`scripts/wine-explorer-wayland-session.sh`). Today it ignores the mode.
Proposed:

- Export `WEXMODE` there too.
- In Safe Mode with Command Prompt, start the recovery console inside the compositor (it needs an X11 or
  Wayland terminal under `wexcomp`).
- If that cannot work, show a message and end the session, so that the logon screen offers the X11 session.

**A4. Interim logon screen look.** The GTK greeter shows Debian's background in Safe Mode. Try putting a drop-in
first in `XDG_DATA_DIRS` before `wex-greeter.sh` starts the greeter. The drop-in is
`lightdm/lightdm-gtk-greeter.conf.d/` with a black background and no Debian art, and nothing in `/etc` changes.
First check the greeter's precedence for configuration directories. This step becomes moot when C4 lands.

### Phase B: Windows programs see Safe Mode (Wine branch)

**B0. The mode file always exists** (WineExplorer). Only the menu writes `/run/wineexplorer/bootmode` today; a
`wexmode=safe` on the command line has no file. Add `wex-bootmode-record.service`:

- `DefaultDependencies=no`, `After=systemd-tmpfiles-setup.service`, wanted by `sysinit.target`.
- It writes the output of `wex-bootmode` to the file if the file is missing.
- `wex-bootmode` keeps preferring the file, so nothing else changes.

**B1. wineboot creates `HKLM\System\CurrentControlSet\Control\SafeBoot\Option`** as a volatile key, as the Windows
kernel does:

- Values: `OptionValue` (1 minimal, 2 network) and `UseAlternateShell` (1 with Command Prompt). The XP VM
  confirms these values, and that the `Option` key does not exist in a normal start.
- Add it in `create_dynamic_registry_keys` (`programs/wineboot/wineboot.c:817`), which runs on every
  `wineboot --init`, both the machine one started by `wex-sysrun` and the user's. Both share the user's
  wineserver, and creating the volatile key is idempotent.
- Read the mode from `\??\unix\run\wineexplorer\bootmode`. That path is WineExplorer-specific, so it goes into
  `STOCK_CHANGES.md` and stays out of upstream; in a stock Wine prefix the file is absent and nothing changes.

**B2. `GetSystemMetrics(SM_CLEANBOOT)`** returns `OptionValue` (`dlls/win32u/sysparams.c:7287`, a constant 0
today). Read it once with `reg_open_key`/`query_reg_value` (`dlls/win32u/font.c:507,606`) when the system metrics
are first set up.

**B3. services.exe starts only SafeBoot services.**

- In `scmdatabase_autostart_services` (`programs/services/services.c:508-572`): when `OptionValue` is 1 or 2,
  keep a service only if its name, or its `Group`, is a subkey of `SafeBoot\Minimal` (or `SafeBoot\Network`).
  Apply the same filter to root PnP devices.
- `service_start` refuses an unlisted service with `ERROR_NOT_SAFEBOOT_SERVICE` (1084, already in
  `include/winerror.h:956`), as Windows does: on the XP VM, starting an unlisted service fails with 1084 in all
  three Safe Modes.

**B4. SafeBoot lists in `loader/wine.inf.in`.**

- Wine's boot, system and automatic services are MountMgr (0), EventLog (2), PlugPlay (2), NDIS (2) and
  NsiProxy (2). Demand services that Wine itself needs (RpcSs and others) must be listed too, or they cannot
  start in Safe Mode.
- Proposed `Minimal`: MountMgr, PlugPlay, EventLog, RpcSs and the bus/file system groups Wine uses.
  `Network` adds NDIS and NsiProxy.
- Check against an export of XP's `SafeBoot` key on the XP VM, kept in the local-only repository. Then check that
  the Safe Mode desktop, the Control Panel and Notepad work with the lists.
- WineExplorer registers no Windows services of its own today (none found in `src/` or the setup scripts).

**B5. RunOnce values in wineboot** (`process_run_key`, `programs/wineboot/wineboot.c:1257-1319`).

- In Safe Mode, run only the values whose name starts with `*`, and leave the others for the next normal start.
- In every mode, a `!` prefix should defer the deletion until the command has succeeded. Wine handles neither
  prefix today. Confirm the semantics with a native probe before changing them.
- The `Run` keys and the Startup folder do not run in WineExplorer in any mode (wineboot runs with `--init`,
  `wineboot.c:1977-1982`). That is a separate gap and is not part of Safe Mode.

**B6. `PaintDesktop` in user32** (`dlls/user32/desktop.c:120-170`). When `SM_CLEANBOOT` is set, paint black and
draw "Safe Mode" in the four corners plus the version line at the top centre, ported from ReactOS
`IntPaintDesktop`. This covers desktops that `wexshell` does not paint (plain `wine explorer /desktop`).

**B7. Tests.**

- Wine conformance tests can only run in normal mode on Windows, so they skip unless `SafeBoot\Option` exists;
  add them to `dlls/user32/tests/sysparams.c` (SM_CLEANBOOT, near line 2941) and `programs/services/tests`.
- The A1 VM test checks that EventLog and PlugPlay run in plain Safe Mode while NDIS and NsiProxy do not, that
  `SM_CLEANBOOT` reads 1, 2 and 1 in Safe Mode, with Networking and with Command Prompt, and that 32-bit and
  64-bit Notepad still start. (On the XP VM those values come from WMI's boot-state reading; no `GetSystemMetrics`
  probe could be built there without MinGW.)

**B8. Build and record.** WoW64 build (`configure --enable-archs=i386,x86_64`, `docs/BUILDING.md`); entries in
`docs/STOCK_CHANGES.md` for B1-B6; the WineExplorer submodule pointer moves only when the branch is merged, in
agreement with the owners of the E1 work.

### Phase C: Safe Mode desktop and logon (WineExplorer)

The XP captures were taken on 2026-11-08 (`../WineExplorer-local/notes/xp/safe-mode/`, with the numbers in its
`measurements.md`). What they showed is summarised under each item, in our own words.

**C1. Safe Mode desktop in wexshell.** `draw_wallpaper` (`src/dlls/wexshell/desktop_view.c:418`) paints black
instead of the wallpaper, then draws "Safe Mode" in the four corners and the version line at the top centre, when
`SM_CLEANBOOT` is set (B2; until then, `WEXMODE` in the environment). The version line uses WineExplorer's own
name and version, never Microsoft's text. Port the layout from ReactOS. On XP the desktop uses the classic look,
the corner text is white and smoothed and sits flush against the screen edges, the bottom corners sit just above
the taskbar, and the version line is centred on the top row. The corner glyphs measure the same as the window
title font, but whether they follow that setting is unconfirmed.

**C2. Message box after logon.** XP shows a Yes/No message box on the Safe Mode desktop, with Yes as the default;
it does not appear in Safe Mode with Command Prompt. No opens System Restore while the desktop keeps loading
behind it, and cancelling System Restore leaves the user in the Safe Mode session. Show it once from
`WexShellStart` (`src/dlls/wexshell/desktop.c:1321`) after the desktop appears. WineExplorer has no System
Restore, so No logs off to the logon screen until a restore tool (Vintner rollback) can take its place
(decision 2).

**C3. Basic resolution.** XP starts Safe Mode at 640x480 with its basic display driver. That driver keeps its own
saved mode, separate from the normal driver's, so Safe Mode never takes the normal-mode resolution. Let
`wine-explorer-session.sh` set 640x480 with `xrandr` for a Safe Mode session, never saving it to the display
profile. Whether XP saves a resolution chosen in Safe Mode could not be confirmed; until it is, Display Properties
should not save one (check `desk.cpl`'s save path).

**C4. Logon in Safe Mode.** On the XP VM, all three Safe Modes showed the Welcome screen, with the built-in
Administrator account added to the accounts; the classic logon dialog appeared only after Ctrl+Alt+Del was pressed
twice, on the black Safe Mode desktop. This differs from what decision 1 assumed, so decision 1 is being revisited
with the owner. The likely shape:

- `wex-greeter.sh` starts `logonui.exe` (`src/programs/logonui/logonui.c`, the Welcome screen it already uses
  for normal starts) in Safe Mode as well. The GTK greeter stays only the last resort, when Wine fails to start;
  a broken Wine is exactly what Safe Mode with Command Prompt must survive.
- An Administrator tile has no direct counterpart: which account, if any, it stands for is an owner decision.
- The classic dialog (Ctrl+Alt+Del twice) goes into `src/dlls/msgina`, which has only the Turn Off and Log Off
  dialogs today. On a workgroup machine its Options button only adds Shut Down.
- Any banner must be WineExplorer's own art (no Windows logo, `CLAUDE.md`).

**C5. Menu and OS list layout.** Differences between XP's F8 menu and `wex-advanced-options` (constants in
`Screen`, `scripts/wex-advanced-options.py`):

- XP draws headings from column 0 and items from column 4.
- Its highlight bar is light gray, and runs from column 0 to the item's last letter instead of having a fixed width.
- It opens with the Start Normally item highlighted, and Up and Down wrap around.
- The footer is one empty row below the last item, not at the bottom of the screen.
- XP grays nothing. WineExplorer keeps graying what does not apply (`CLAUDE.md`).
- In the OS list every line starts at column 0, and the chosen mode is bright blue on row 23, with no countdown.

**C6. `ntbtlog.txt`** (`scripts/wex-bootlog.sh`). XP writes it as UTF-16 with CRLF line ends. Each start appends a
header line with no blank line before it, and the date follows the service pack name with no space between them.
A Safe Mode start always writes the log, whether or not Enable Boot Logging was chosen.

The A1 test can then also check the recovery console's shape: on XP, Safe Mode with Command Prompt shows a
maximized 80-column console titled with the program name only, with no taskbar, desktop icons or Explorer.

### Phase D: start-up failure and recovery (GRUB now, Vintner later)

**D1. "WineExplorer did not start successfully".** XP shows this screen after a start that did not finish, with
a countdown to a normal start. On the XP VM it appeared only after a reset during the boot splash, not after one
during the firmware, the disk check or logon. It offers the three Safe Modes, Last Known Good and Start Normally
(the default) with a 30-second countdown, and any arrow key stops the countdown and removes its line. With GRUB it
can work now:

- Debian's `grub.cfg` already checks `recordfail` (line 60 on the installed system), but no entry sets it, and
  `grub-common.service` clears it as soon as `multi-user.target` starts. That is too early: section 4.4 of
  `docs/VINTNER_BOOT.md` counts a start as good only once the logon screen is up (decision D3).
- Add a grub.d snippet (`06_wineexplorer_recordfail`) that sets `recordfail=1` with `save_env` on every start.
  Before setting it, keep the previous value in a variable.
- When the value was already 1, boot the Advanced Options entry with `wexfailed=1` added. The menu then shows
  the failure screen: Safe Mode, with Networking, with Command Prompt, Last Known Good (grayed until Vintner),
  Start Normally. A 30-second countdown defaults to Start Normally.
- A WineExplorer health check clears `recordfail` once the logon screen (or a setup stage screen) is up, and
  replaces `grub-common.service`'s clear (an override of that unit).
- Safe Mode boots and every boot through the menu write the previous value back. A good state stays good and a
  bad one stays bad, as the owner decided (decision 3).
- `grubenv` sits on the ext4 root, which GRUB can write. With Vintner this becomes `vintner status --json` and
  the boot counters (`docs/VINTNER_BOOT.md` section 4.3).

**D2. Enable VGA Mode.** It stays grayed for now (decision 4: only if it is simple). X forces the `modesetting`
driver (`scripts/20-wineexplorer-modesetting.conf`), so with no DRM device it has no fallback. `fbdev` and `vesa`
are installed. The two ways, neither simple today:

- (a) Restart once into a GRUB entry with `nomodeset` and `set gfxpayload=keep` at a VESA mode, so that
  `simpledrm` has a framebuffer and X's `modesetting` runs on it. The entry is chosen with `grub-reboot`, which
  writes `grubenv`; on Vintner it becomes a second variant. It needs a new GRUB entry and tests on BIOS and UEFI.
- (b) No restart: X on the current DRM device at 640x480. It is not a basic driver, so it does not help when the
  graphics driver is the problem. Once C3 sets a basic resolution for Safe Mode, (b) is a few lines on top of it.
- Look at it again after C3.

**D3. Last Known Good Configuration and Return to OS Choices Menu.** These wait for Vintner's `next-boot
previous-good` and `next-boot menu` (`docs/VINTNER_BOOT.md` V6). Until then the first stays grayed and the second
shows the local list.

**D4. Debugging Mode.** Today it raises the service manager's log level only. Consider sending kernel messages to
the first serial port as well, which is closer to XP's kernel debugger on COM1. Low priority.

**D5. Startup and Recovery in sysdm.cpl** (`docs/VINTNER_BOOT.md` section 4.6). The "Time to display recovery
options when needed" setting is D1's countdown, so add it when D1 lands.

## 4. Owner decisions (2026-11-08)

1. **Logon dialog in Safe Mode:** XP's classic dialog (C4) in all three modes. The GTK greeter is only the last
   resort, when Wine cannot start. Being revisited: the XP captures show the Welcome screen in Safe Mode (C4).
2. **The message box's No button** (C2): XP's Yes/No box. No logs off to the logon screen, and is connected to a
   restore tool (Vintner rollback) when one exists.
3. **Starts through the menu do not count:** no boot through the menu changes the start-up state, whatever mode
   it ends in, good or bad. That is the same rule as Vintner's uncounted variant, so both loaders behave alike.
4. **Enable VGA Mode** (D2): stays grayed for now, unless an implementation turns out to be simple.
5. **Where Wine reads the mode** (B1): the WineExplorer file `/run/wineexplorer/bootmode`; `wex-sysrun` and the
   other E1 code stay as they are.
6. **XP captures:** taken on the XP reference VM registered in VirtualBox (the F8 menu, the OS list, the failure
   screen, the Safe Mode desktop, the message box, the classic logon dialog, `ntbtlog.txt` and the `SafeBoot`
   key), and kept in `../WineExplorer-local/notes/xp/safe-mode/`. On 2026-11-08 the VM's files were on this
   machine but the VM was not registered; the owner registers it.

## 5. Order

1. A1, A2 (tests first; they check every later step).
2. B0-B8 on the Wine branch, then C1 and C2. These need B2, or `WEXMODE` as a stopgap.
3. D1 (GRUB), then A3 and A4.
4. C1-C3, C5 and C6 (the XP captures exist); C4 once decision 1 is settled again.
5. D2 stays grayed; look at it again after C3. D3 and the Vintner variant when Vintner's commands exist.

Each step: unit tests, the A1 VM test on BIOS and UEFI with a 32-bit program, a commit, and an update to
`docs/SAFE_MODE.md`.
