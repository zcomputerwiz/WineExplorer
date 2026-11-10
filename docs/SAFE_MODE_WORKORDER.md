<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Safe Mode: Remaining Work

Date: 2026-11-08  
Status: in progress; the owner's decisions are in section 4 (2026-11-08). Phase 1 (boot modes, Advanced Options
menu, logon, session) is done and tested (`docs/SAFE_MODE.md`, WineExplorer commit `14286423`). Done since: A1,
A2, B0-B4, B6, B7, C1, C2, D1 and parts of C4 and C7 (2026-11-09), A3, C6 (but the recovery console's shape) and
Safe Mode through Vintner Boot's variant (2026-11-10). Open: A4, B5, B8's merge, C3, C5, the rest of C4, C6 and
C7, D2-D5.  
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
the compilers. The round 3 image has no prefetch service, so A2 was the first run that checked the Prefetcher's
mask (it held).
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

The ISO is built in wexbuild now (`docs/TOOLING.md`). On 2026-11-09 an ISO built with `--vintner` installed and
passed on both firmwares (`docs/VINTNER_BOOT.md` section 4.2), but a Vintner install has no F8 yet. A2 itself needs
the default (GRUB) ISO.

**Done 2026-11-09** (`docs/SAFE_MODE.md` section 6): the GRUB ISO installed with no input and all nine phases
passed on SeaBIOS and OVMF, with the Prefetcher masked in every Safe Mode. The upgrade test passed on both
firmwares (`WEX_SAFEMODE_FILES=upgrade`). Found and fixed on the way:
- Under the system role the logon prefix has no `system.reg` of its own, so the greeter ran `wineboot -u` at every
  start of the logon screen; it now looks for `user.reg`.
- The old Safe Mode hid standard users that had no AccountsService file, and the postinst did not show them again;
  now it does.
- Upgrade test: install from an ISO that has the old Safe Mode, boot it into the old Safe Mode with Command Prompt
  once, install the new package, and check that the postinst removed the leftovers. The leftovers are
  `99-safemode.conf`, the old PAM line, `Session=wex-safe-terminal` and the enabled `wex-safemode.service`.

**A3. Wayland session in Safe Mode** (`scripts/wine-explorer-wayland-session.sh`). Today it ignores the mode.
Proposed:

- Export `WEXMODE` there too.
- In Safe Mode with Command Prompt, start the recovery console inside the compositor (it needs an X11 or
  Wayland terminal under `wexcomp`).
- If that cannot work, show a message and end the session, so that the logon screen offers the X11 session.

**Done 2026-11-10** (`docs/SAFE_MODE.md` sections 3 and 6). `wexcomp` has XWayland and a session command option
(`-S`), so the recovery console's xterm runs inside it and the fallback was not needed. If `wexcomp` itself fails
to start, the session ends and the Welcome screen comes back, as before.

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

**Done 2026-11-09** (`wex-bootmode --record`, `installer/systemd/wex-bootmode-record.service`). A security fix came
with it: `/run/wineexplorer` belonged to the prefetch service's `wexcache` account (its tmpfiles line and
`RuntimeDirectory=`). `wex-bootmode` ignores a file that root does not own, but Wine cannot check the owner, so
that account could have changed the mode Windows programs see, in either direction. The directory now belongs to root,
and the daemon only receives its socket there from systemd.

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
- List E1's elevation broker `wexelevate` in both `Minimal` and `Network` (the E1 session, 2026-11-08), or
  administrators cannot elevate in Safe Mode:
  - It is "Application Information", a demand-start service (`StartType=3`) with no `DependOnService`, installed
    by `wine.inf`. consent.exe is started by the broker and is not a service.
  - Since B3 filters demand starts as well, listing `wexelevate` is enough for administrators to elevate.
  - Outside Windows services it needs the system role (the setgid `wex-sysrun` launcher and
    `/etc/wineexplorer/sysrun.conf`), the machine root and D-Bus. Safe Mode keeps D-Bus (`basic.target`) and must
    go on doing so.
  - The later standard-user chain adds native pieces that are not Windows services: a root verifier D-Bus service
    and the PAM worker.

**B1-B4 and B6 are done on the Wine branch** (2026-11-09; the branch is local, and the submodule still pins
`cf1c29be`). What was built is in `docs/SAFE_MODE.md` section 5 and the first entry of
`docs/STOCK_CHANGES.md`. Different from the plan above:
- B2 reads the key when `SM_CLEANBOOT` is first asked for, not when the metrics are set up, and keeps a value once
  found: a process that starts before wineboot has made the key asks again.
- B3 checks the lists in the automatic start and in `StartService` (`rpc.c`), not in `service_start`: that also
  starts the internal `Winedevice` host services that run the drivers, and a first version there kept every driver
  from starting in Safe Mode.
- Not covered: drivers that ntoskrnl's PnP manager loads for the devices of a listed bus (winebus, wineusb) go
  through `ZwLoadDriver` in the driver host (`dlls/ntoskrnl.exe/ntoskrnl.c`), not through services.exe, so they
  load in Safe Mode too; Windows checks those in the kernel. They are device drivers (HID, USB), so this was left
  as it is.
- B3 fails open: with no SafeBoot list (a prefix made before the lists) every service starts as before. Under the
  system role the lists are in the machine root's `HKLM`, so they reach an existing install with the Wine
  package's postinst (`wex-setup-machine.sh --update`), not with a user's prefix update.
- B4's lists, with no XP export to compare yet: `Minimal` is EventLog, MountMgr, PlugPlay, RpcSs, winebus, wineusb,
  Winmgmt and `wexelevate`; `Network` adds LanmanServer, NDIS, nsiproxy and TermService. NDIS and nsiproxy share
  MountMgr's "System Bus Extender" group, so groups are not listed, or `Minimal` would start them too.
- B6 is written from the XP measurements rather than ported from ReactOS, and shows Wine's version.

**B5. RunOnce values in wineboot** (`process_run_key`, `programs/wineboot/wineboot.c:1257-1319`). Open: it waits for
the native probe on the XP VM.

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
- **Done 2026-11-09.** Both conformance tests are on the branch. The VM test's probe is
  `installer/tests/safeboot-probe.c`, built with MinGW in wexbuild as a 32-bit and a 64-bit program and run in
  `safe`, `safenet`, `safecmd` and `normal` (`docs/SAFE_MODE.md` section 6). It also checks that starting the
  spooler fails with 1084.

**B8. Build and record.** WoW64 build (`configure --enable-archs=i386,x86_64`, `docs/BUILDING.md`); entries in
`docs/STOCK_CHANGES.md` for B1-B6; the WineExplorer submodule pointer moves only when the branch is merged, in
agreement with the owners of the E1 work. The build (WoW64, in wexbuild) and the `STOCK_CHANGES.md` entry are done;
the merge is open.

### Phase C: Safe Mode desktop and logon (WineExplorer)

The XP captures were taken on 2026-11-08 (`../WineExplorer-local/notes/xp/safe-mode/`, with the numbers in its
`measurements.md`). What they showed is summarised under each item, in our own words.

**C1. Safe Mode desktop in wexshell.** `draw_wallpaper` (`src/dlls/wexshell/desktop_view.c:418`) paints black
instead of the wallpaper, then draws "Safe Mode" in the four corners and the version line at the top centre, when
`SM_CLEANBOOT` is set (B2; until then, `WEXMODE` in the environment). The version line uses WineExplorer's own
name and version, never Microsoft's text. Port the layout from ReactOS. On XP the desktop uses the classic look,
the corner text is white and smoothed and sits flush against the screen edges, the bottom corners sit just above
the taskbar, and the version line is centred on the top row. The corner glyphs measure the same as the window
title font, but whether they follow that setting is unconfirmed. **Done 2026-11-09** (`desktop_view.c`): the
window title font, smoothed; the version line is "WineExplorer (Wine <version>)". The classic look is not forced.

**C2. Message box after logon.** XP shows a Yes/No message box on the Safe Mode desktop, with Yes as the default;
it does not appear in Safe Mode with Command Prompt. No opens System Restore while the desktop keeps loading
behind it, and cancelling System Restore leaves the user in the Safe Mode session. Show it once from
`WexShellStart` (`src/dlls/wexshell/desktop.c:1321`) after the desktop appears. WineExplorer has no System
Restore, so No logs off to the logon screen until a restore tool (Vintner rollback) can take its place
(decision 2). **Done 2026-11-09** (`desktop.c`, `safe_mode_message`): the box comes before the taskbar, and the
desktop icons wait for the answer; the text is WineExplorer's own. The VM test answers it in `safe`, `safenet` and
`accounts`.

**C3. Basic resolution.** XP starts Safe Mode at 640x480 with its basic display driver. That driver keeps its own
saved mode, separate from the normal driver's, so Safe Mode never takes the normal-mode resolution. Let
`wine-explorer-session.sh` set 640x480 with `xrandr` for a Safe Mode session, never saving it to the display
profile. Whether XP saves a resolution chosen in Safe Mode could not be confirmed; until it is, Display Properties
should not save one (check `desk.cpl`'s save path). E1's consent dialogs measure 260×178 and 260×215 dialog units,
about 390×350 pixels at 96 dpi, so they fit 640×480. The E1 session runs its headless dialog test at 640×480 as
work order WO-13 (queued 2026-11-08, after WO-11).

**C4. Logon in Safe Mode.** On the XP VM, all three Safe Modes showed the Welcome screen, with the built-in
Administrator account added to the accounts; the classic logon dialog appeared only after Ctrl+Alt+Del was pressed
twice, on the black Safe Mode desktop. So Safe Mode uses the Welcome screen (decision 1):

- `wex-greeter.sh` starts `logonui.exe` (`src/programs/logonui/logonui.c`, the Welcome screen it already uses
  for normal starts) in Safe Mode as well. The GTK greeter stays only the last resort, when Wine fails to start;
  a broken Wine is exactly what Safe Mode with Command Prompt must survive. In Safe Mode the script first checks
  that Wine starts in the logon prefix, and falls back if it does not or if the logon screen ends with an error.
- **The Administrator account and tile** (decision 7): a dedicated account, `administrator`, not root. Today
  Setup's "Computer Name and Administrator Password" page sets root's password (`WexRootPasswordSet`,
  `src/programs/setup/setup.c`); left empty, root keeps no password (`*`). Found on the round 3 image (2026-11-08):
  - The Welcome screen never lists root: AccountsService leaves it out of `ListCachedUsers`.
  - With a password, root can log on at a text console (Ctrl+Alt+F2 to F6; there is no `securetty`), and in the GTK
    fallback by typing `root`: `/etc/pam.d/lightdm` does not refuse root, only `lightdm-autologin` does.
  - There is no network logon. The installed system has no SSH, RDP or Samba server, and `/root/.ssh` is empty.
    Samba is an optional component, off by default.
  - Root would make a poor Administrator for Windows programs. In the token model, uid 0 outside `sudo` is a
    standard user, yet uid 0 passes every file permission check (`docs/ELEVATION_SYSTEM_ROLE_WORKORDER.md`), and
    every Windows program in its session would run as root.

  The work:
  - Setup creates `administrator` (display name Administrator) through AccountsService: an administrator (account
    type 1, the `sudo` group), with the `wine-explorer` session and the password from the same page. Root still
    gets that password, as it does today, so nothing about root changes (Debian's emergency shell needs it).
    The two passwords are independent from then on. The E1 accounts map picks the new account up through its
    `/etc/passwd` path unit, like any other account. It should get the well-known RID 500 and the Windows name
    "Administrator", as on XP, because some programs look for that account. Wine compares account names without
    regard to case, so the Unix name `administrator` resolves as "Administrator". Setup then provisions it with the
    reserved RID 500 instead of the next one from the counter, which starts at 1001. That needs a small
    provisioning option in the accounts map, which the E1 session adds when C4 starts (agreed 2026-11-08). A home
    costs about 2 MB (round 3 image:
    `/etc/skel` 108 KB, `wineuser`'s home 2.3 MB). Homes are 0700, so working on another user's profile goes
    through elevation, as for any administrator.
  - In a normal start the account is hidden from the Welcome screen, as on XP Professional, and Safe Mode shows its
    tile. `logonui.exe` applies XP's own rule for the built-in Administrator and the registry's hidden-account list
    (C7). The account therefore stays an ordinary AccountsService account with no `SystemAccount` flag, and an
    `Administrator` value of 1 in that list shows it in a normal start, as on XP. The classic dialog will accept it
    in any start.
  - With a blank Administrator password, the account stays locked and gets no tile. Debian's `common-auth` has
    `pam_unix.so nullok`, and `su` and `sudo` include it. An empty password would let every local user run
    `su administrator` and then `sudo` to root.
  - No network logon, with or without a password: install `/etc/ssh/sshd_config.d/wineexplorer.conf` with
    `PermitRootLogin no`, `DenyUsers administrator` and `PermitEmptyPasswords no`. It only takes effect if someone
    installs an SSH server; Debian's own default still lets root log on with a key. The Samba component never adds
    either account to Samba's accounts. **Done 2026-11-08:** `installer/ssh/sshd-wineexplorer.conf`, packaged as a
    conffile.
  - **Done 2026-11-09**, built in the new `wexbuild` distro (`docs/TOOLING.md`):
    - `users_list` (`src/dlls/wexhost/unixlib.c`) skips locked accounts; that is one condition, with no new
      `wexhost` call.
    - `logonui.exe` applies the Administrator rule and the hidden-account list (C7).
    - OOBE refuses "Administrator".
    - The package postinst's Safe Mode part creates the account, locked, in E1's administrators' group, with an
      AccountsService file (new and existing installs alike).
    - `wex-gui-setup.sh` copies root's password hash to it after Setup. So Setup's page and `wexhost` need no new
      password call.
    - The VM test's `accounts` phase checks it with this branch's Wine (`WEX_TEST_WINE`).
  - Still open:
    - E1's reserved RID 500 and `wex-is-admin`, queued for this step on the E1 side;
    - the classic dialog accepting "Administrator";
    - the XP checks listed under C7.
  - One definition of "administrator". E1 decides it from `admin_group=` in `/etc/wineexplorer/sysrun.conf` (default
    `sudo`; strict rules: root-owned, not group- or world-writable, no symlink, at most one key, and a bad file
    means nobody is an administrator), through `getgrouplist` at logon. The Administrator account goes into that
    group. The Safe Mode with Command Prompt check (`scripts/wex-safemode-pam-check.sh`, `sudo` today) uses the same
    answer. Rather than a third parser, use the helper the E1 session offered, `wex-is-admin <user>` (0 or 1). It
    is queued on the E1 side for when C4 starts, together with the RID 500 option. E1's WO-12 makes
    `server/registry.c` read `admin_group=` as strictly as `server/accounts.c`.
  - OOBE refuses Administrator, in any case, as a new user's name. Check what XP's OOBE does with that name.
  - Existing installs: the package's postinst creates the account, locked, if it is missing; its password is then
    set in User Accounts.
  - VM test: Safe Mode logs on through the Administrator tile. A normal start shows no tile. With a blank password,
    `su administrator` fails for a standard user.
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
- The same changes apply to D1's failure screen, which uses the menu's layout today. On XP its text starts at
  column 0, and the countdown sits on the row directly under the footer.

**C6. `ntbtlog.txt`** (`scripts/wex-bootlog.sh`). XP writes it as UTF-16 with CRLF line ends. Each start appends a
header line with no blank line before it, and the date follows the service pack name with no space between them.
A Safe Mode start always writes the log, whether or not Enable Boot Logging was chosen.

**Done 2026-11-10** (`scripts/wex-bootlog.sh`, `installer/systemd/wex-bootmode-generator`; `docs/SAFE_MODE.md`
section 4). The header's service pack string is the machine registry's `CSDVersion` string. On today's image it
is empty: the machine root's `HKLM\Software\Microsoft\Windows NT\CurrentVersion` still has Wine's Windows 10 values
(`CurrentBuild` 19045, `CSDVersion` a DWORD 0). XP's lines are
`Loaded driver` and `Did not load driver`; WineExplorer keeps its own `Did not start service` line for systemd
units that failed, and has no counterpart for drivers held back in Safe Mode. The recovery console's shape (below)
is still open.

The A1 test can then also check the recovery console's shape: on XP, Safe Mode with Command Prompt shows a
maximized 80-column console titled with the program name only, with no taskbar, desktop icons or Explorer.

**C7. Hidden accounts on the Welcome screen** (decision 8). XP's Welcome screen leaves out every account that has a
REG_DWORD value of 0 under `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon\SpecialAccounts\UserList`.
The value's name is the account name; a value of 1 shows the account. Hidden accounts can still log on through
the classic dialog and Run As. The built-in Administrator has its own rule: it is hidden in a normal start (XP
Professional, when another administrator account exists) and shown in Safe Mode, and an `Administrator` value of 1
shows it in a normal start too. Check on the XP VM:
- what a value of 0 does to Administrator in Safe Mode;
- whether the key also hides accounts in User Accounts (WineExplorer has no User Accounts yet; when it does, it
  follows XP);
- whether the Welcome screen reads the key every time it appears.

The work:

- `logonui.exe` (`src/programs/logonui/logonui.c`, `load_users`) reads the key with the registry API after
  `WexUsersList`. It leaves out every account whose value is 0 (account names compared without regard to case)
  and applies the Administrator rule, using `wex-bootmode`'s mode as the Safe Mode test. Nothing on the Unix side
  filters accounts. **Done 2026-11-09.**
  - The mode comes from `WEXMODE`, which `wex-greeter.sh` exports, or `SM_CLEANBOOT` once B2 exists.
  - The VM test's `accounts` phase writes the value into the logon prefix's own registry. On an image without
    the system role, that is the registry the logon screen reads.
- The logon prefix must see the machine's `HKLM`, and today it does not. On the round 3 image
  `/var/lib/lightdm/.wine-logon/system.reg` is a private 3.3 MB hive that `wineboot` made at the greeter's first
  start. User prefixes link `system.reg` and `userdef.reg` to the machine root instead (`/etc/skel/.wine`, made by
  `scripts/setup-machine-root.sh`). `setup-greeter.sh` and `wex-greeter.sh` should give the logon prefix the same
  two links, and convert an existing logon prefix. Also check that `wineboot -u` in the logon prefix still works.
- **The greeter's Wine must run under the `wex-sysrun` launcher, as desktop users' does** (the E1 session,
  2026-11-08). Then its `wineserver` has the system role and loads `HKLM` straight from the machine root.
  - That server never saves a hive it reached through a link (`save_branch` returns early, `server/registry.c`
    around line 3999), and only system-role processes write back to the machine root.
  - A stock server started without the launcher would follow the link. It cannot write the root-owned machine
    root, but it could leave a private copy behind, so the launcher is required.
  - Nothing has to call the launcher. Wine's ntdll starts the server through
    `/usr/local/libexec/wineexplorer/wex-sysrun` itself (`wine/dlls/ntdll/unix/sysrun.c`) whenever the root-owned
    `/etc/wineexplorer/sysrun.conf` names a machine root and the launcher is installed. A plain `wine` from the
    greeter is enough (the E1 session, 2026-11-08).
  - **The prefix must be `<passwd home>/.wine` of the calling account.** The launcher serves exactly that prefix,
    and ntdll checks it (`sysrun.c` around line 121). Any other `WINEPREFIX` gets a stock server and the warning
    "the prefix ... is not ..., the system role is not available for it".
    - So the logon prefix moves from `/var/lib/lightdm/.wine-logon` to `/var/lib/lightdm/.wine`. `/var/lib/lightdm`
      is the `lightdm` account's home; check that on the installed image.
    - That means changes to `setup-greeter.sh`, the `WINEPREFIX` default in `wex-greeter.sh`, and the tests, plus a
      migration of an existing `.wine-logon` (move it, or remove it and let the greeter build the new one).
    - **Done 2026-11-08.** `wex-greeter.sh` uses `$HOME/.wine` and moves `~/.wine-logon` there once.
      `setup-greeter.sh` does the same. `installer/tests/check-lightdm.py` checks the account's home and the
      prefix, and the VM test checks the move.
    - The links to the machine hives are not made yet. They come once a test image has the system role, which the
      round 3 image does not (no `sysrun.conf`), and E1's WO-11 has passed. Until then the logon prefix keeps its
      private hive.
    - The E1 session could teach the launcher one fixed extra prefix for the greeter account instead, but that is
      a bigger change than renaming the directory.
  - The E1 session is writing a variant of `scripts/test_sysrun_server.sh` 3(iii) for exactly this case, as work
    order WO-11 (in progress 2026-11-08). The account has a system uid below 1000, uses nologin, and is in neither
    `sudo` nor `wexsys`; its prefix is `~/.wine` with the machine-hive links. The test also checks that the server
    is the one the launcher started.
- Reading the key works for `lightdm`. A non-administrator session token holds BUILTIN\Users, and the default
  machine key descriptor gives BUILTIN\Users `KEY_READ` (`server/token.c`; `key_default_sd` in
  `server/registry.c`), unless that key gets a custom ACL. E1-S13's "no token/load authority" for unmapped
  accounts concerns the standard-user elevation token and profile loading, not a session's own token.
- A change shows the next time the Welcome screen starts, once the writing session's `wineserver` has saved the
  hive (within 30 seconds). Writing the key is an ordinary administrator `HKLM` write. It already works through the
  filtered-administrator consent path; nothing waits on the standard-user broker. C7 changes nothing in E1.
- The GTK fallback lists no accounts (`greeter-hide-users=true`), so it needs no change.
- Tests: the filter in `logonui.exe` with a list and a key made for the test. In the VM, an account with a value
  of 0 is missing from the Welcome screen in a normal start. The Administrator tile is shown in Safe Mode only, and
  also in a normal start once `Administrator` is 1.

### Phase D: start-up failure and recovery (GRUB now, Vintner later)

**D1. "WineExplorer did not start successfully".** XP shows this screen after a start that did not finish, with
a countdown to a normal start. On the XP VM it appeared only after a reset during the boot splash, not after one
during the firmware, the disk check or logon. It offers the three Safe Modes, Last Known Good and Start Normally
(the default) with a 30-second countdown, and any arrow key stops the countdown and removes its line.

**Done (2026-11-08)**, with GRUB; `docs/SAFE_MODE.md` sections 3 and 4 describe it as built.
- **GRUB:** `11_wineexplorer_advanced` records every start as the grubenv variable `wex_boot_pending`. Debian's
  `recordfail` is not used: when it is set, Debian's `grub.cfg` shows GRUB's menu for 30 seconds, and
  `grub-common.service` clears it as soon as `multi-user.target` starts, before the logon screen exists. So no
  Debian unit is overridden. If the variable is still 1 at the next start, GRUB boots the Advanced Options entry
  with `wexfailed=1`.
- **Menu:** `wex-advanced-options` then shows the failure screen. The explanation is in our own words, in XP's
  layout.
- **Check:** `wex-startup-check.service` clears the variable once the logon screen is up. It is the check from
  section 4.4 of `docs/VINTNER_BOOT.md` (`Before=` and `RequiredBy=boot-complete.target`), so Vintner's bless and
  the clear happen at the same point. The Vintner session has the unit name. Vintner's boot counters
  (`VINTNER_T_<g>`) stay separate until the two are unified.
- **Decision 3:** a start through F8 writes the previous value back. A Safe Mode chosen on the failure screen does
  not count either, so the screen comes back at the next start. Start Normally on it counts.
- **Tests:** the VM test's `failed` phase passes on SeaBIOS and OVMF.
- **With Vintner:** this becomes `vintner status --json` and the boot counters (`docs/VINTNER_BOOT.md` section 4.3).
- **Not measured on XP:** whether a Safe Mode chosen on the failure screen goes through the operating system list
  (here it starts at once), and whether XP shows the screen again after a Safe Mode start.

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
previous-good` and `next-boot menu` (`docs/VINTNER_BOOT.md` V6). Neither is in Vintner's current phase 3 (the
Vintner session, 2026-11-08). Until then the first stays grayed and the second shows the local list.

**D4. Debugging Mode.** Today it raises the service manager's log level only. Consider sending kernel messages to
the first serial port as well, which is closer to XP's kernel debugger on COM1. Low priority.

**D5. Startup and Recovery in sysdm.cpl** (`docs/VINTNER_BOOT.md` section 4.6). The "Time to display recovery
options when needed" setting is D1's countdown, so add it when D1 lands.

## 4. Owner decisions (2026-11-08)

1. **Logon screen in Safe Mode:** the Welcome screen (`logonui.exe`), as XP shows it in Safe Mode (C4), in all
   three modes. The GTK greeter is only the last resort, when Wine cannot start. (Decided 2026-11-08 after the XP
   captures; it replaces the earlier choice of the classic dialog.)
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
7. **The Administrator account** (C4): a dedicated administrator account, `administrator`, so root's behavior does
   not change (the owner's suggestion; it replaces using root, from earlier the same day). Setup sets its password
   on the Administrator password page. It is hidden from the Welcome screen in a normal start and gets a tile in
   Safe Mode, as on XP Professional. It is for troubleshooting, such as a broken user profile. It never gets a
   network logon, and with no password it cannot log on anywhere. It is a nice-to-have: Safe Mode works without it.
8. **Hidden accounts** (C7): the Welcome screen reads the hidden-account list (`SpecialAccounts\UserList`) from the
   machine registry, as XP does. The Administrator account is hidden by XP's own rule, not by an AccountsService
   flag, so the registry can show it as on XP. (The owner's request, 2026-11-08.)

## 5. Order

1. A1, A2 (tests first; they check every later step). Done.
2. B0-B8 on the Wine branch, then C1 and C2. These need B2, or `WEXMODE` as a stopgap. Done except B5 and B8's
   merge.
3. D1 (GRUB; done 2026-11-08), then A3 and A4.
4. C1-C6 (the XP captures exist); C4's Welcome screen first, the classic dialog (Ctrl+Alt+Del twice) later. C4's
   Administrator account (decision 7) and C7's hidden accounts (decision 8) go together, C7's logon-prefix links
   first; both are optional and can come at any point.
5. D2 stays grayed; look at it again after C3. D3 and the Vintner variant when Vintner's commands exist.

Each step: unit tests, the A1 VM test on BIOS and UEFI with a 32-bit program, a commit, and an update to
`docs/SAFE_MODE.md`.
