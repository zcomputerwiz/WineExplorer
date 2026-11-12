<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Safe Mode and the Advanced Options Menu

Date: 2026-11-08  
Status: phase 1 (boot modes, menu, logon, session) implemented and tested on SeaBIOS and OVMF (section 6), also as
installed from the ISO and as an upgrade; phase 2 (Windows programs see Safe Mode) built on a Wine branch and
tested (sections 5 and 6).  
Related: `docs/VINTNER_BOOT.md` section 4.3 (boot loader side).

## 1. What Windows XP does

- F8 at start shows the Advanced Options menu. A choice there returns to the operating system list with the
  mode shown at the bottom; ENTER starts it.
- The loader passes `/SAFEBOOT:MINIMAL`, `/SAFEBOOT:NETWORK` or `/SAFEBOOT:MINIMAL(ALTERNATESHELL)` to the kernel.
  The kernel records the mode in a volatile registry key (`HKLM\SYSTEM\CurrentControlSet\Control\SafeBoot\Option`).
  Nothing on disk changes, so the next start is normal again with nothing to undo.
- Only the drivers and services listed under `SafeBoot\Minimal` (or `SafeBoot\Network`) start. Task Scheduler,
  the Prefetcher and Automatic Updates are not in the lists.
- There is no automatic logon. With Command Prompt, Winlogon starts `cmd.exe` (the `AlternateShell` value)
  instead of Explorer.
- Explorer shows "Safe Mode" in the four corners of a black desktop at a basic resolution, and a message box
  after logon.
- The `Run` keys and the Startup folder are skipped; `RunOnce` values run only if their name starts with `*`.
  `GetSystemMetrics(SM_CLEANBOOT)` returns 1 (minimal) or 2 (with networking).

## 2. The implementation it replaces

`wex-safemode-setup.sh` (commit `ccbf98ba`) ran at every boot and, in Safe Mode with Command Prompt:

- moved the autologin files out of `/etc/lightdm/lightdm.conf.d`, wrote `99-safemode.conf` there and inserted a
  line into `/etc/pam.d/lightdm`;
- rewrote `/var/lib/AccountsService/users/<name>` for every account with a UID of 1000 or more, hiding
  non-administrators with `SystemAccount=true` and switching administrators to the `wex-safe-terminal` session;
- kept its backups in `/run/wex-safemode`, which is lost when the machine crashes or loses power in Safe Mode.

On every normal boot it then "cleaned up" by forcing `Session=wine-explorer` and `SystemAccount=false` on every
account, which also overwrote settings the owner had chosen (another session, an account hidden on purpose).
Safe Mode itself started every service a normal boot starts.

## 3. Design

The mode lives only in `/run`, as XP keeps it in a volatile key. Every component reads it and changes nothing on
disk.

**Where the mode comes from.** `wex-bootmode` prints `normal`, `menu`, `safe`, `safenet` or `safecmd`:

1. `/run/wineexplorer/bootmode`, written by the Advanced Options menu (root-owned files only);
2. else `wexmode=` on the kernel command line (Vintner Boot's Advanced Options variant passes `wexmode=menu`);
3. else `normal`.

The Safe Mode values become `normal` while a setup stage is pending (`/etc/wineexplorer/setup/stage`): setup has
to finish first.

**The record** (work order B0, 2026-11-09). `wex-bootmode-record.service` runs `wex-bootmode --record` early in
every boot, before `sysinit.target`. It writes the mode to `/run/wineexplorer/bootmode` unless the file exists; a
boot through the menu records nothing, and the menu writes its own choice. Wine reads the mode from that file
(section 5), so `wexmode=` on the command line, as a Vintner variant passes it, reaches Windows programs too.
`/run/wineexplorer` belongs to root: Wine cannot check who owns the file, so the prefetch service, which used to own
the directory (`RuntimeDirectory=`), now only receives its socket there.

**Which services start.** The generator `wex-bootmode-generator` writes only to
`/run/systemd/generator.early`, which systemd clears at every boot:

| Mode | `default.target` | Masked for this boot |
|---|---|---|
| `normal` | unchanged (`graphical.target`) | nothing |
| `menu` | `wex-advanced-options.target`: `sysinit.target` and the menu only | nothing |
| `safe`, `safecmd` | `wex-safemode.target` | `timers.target` (Task Scheduler), `wexprefetchd` (Prefetcher), networking |
| `safenet` | `wex-safenet.target` | `timers.target`, `wexprefetchd` |

`wex-safemode.target` is the allow-list, the counterpart of `SafeBoot\Minimal`: `basic.target` (the equivalent of
the boot drivers), logind, user sessions, the display manager and the text consoles. Units that D-Bus activates
on demand (AccountsService, polkit) still start, as demand-start services do in Windows Safe Mode.
`wex-safenet.target` adds NetworkManager. Networking is masked in the minimal modes because NetworkManager is
D-Bus activatable and would otherwise start on first use. Services the security model needs (`wex-accounts`) are
not masked, as LSA and SAM run in Windows Safe Mode. While an offline update is pending (`/system-update`), the
generator steps aside and the update runs first.

**Logon.** Two `pam_exec` lines are added once, when the session package is installed, and both consult the mode:

- `/etc/pam.d/lightdm-autologin`: refuses in every Safe Mode. LightDM then shows the logon screen instead
  (`switch_to_greeter_from_failed_session` in LightDM's `seat.c`).
- `/etc/pam.d/lightdm`: in Safe Mode with Command Prompt only root and members of `sudo` may log on, as before.
  In the other modes everyone may, as in Windows.

`wex-greeter.sh` shows the Welcome screen (`logonui.exe`) in Safe Mode too, as XP does
(`docs/SAFE_MODE_WORKORDER.md` C4, decision 1). In Safe Mode it first checks that Wine starts in the logon prefix
(`cmd.exe /c exit 0`, at most 120 seconds, and the prefix update at most 300). If Wine is missing, does not
start, or the logon screen ends with an error, it starts the GTK greeter instead: that one needs no Wine, so Safe
Mode with Command Prompt can still be reached with a broken Wine. A normal start is unchanged. The GTK greeter is at
640x480 as well, and black with no distribution art (work order A4, 2026-11-11). For it alone, `wex-greeter.sh`
appends `/usr/share/wineexplorer/safe-greeter` to `XDG_DATA_DIRS`; that directory's
`lightdm/lightdm-gtk-greeter.conf.d/90-wineexplorer-safe-mode.conf` (`installer/lightdm/`) sets `background=#000000`
and `user-background=false`. lightdm-gtk-greeter 2.0.9 reads the data directories in order, then the configuration
directories and `/etc/lightdm`, and a later file overrides an earlier one. So the last directory's keys replace
Debian's `01_debian.conf`, and nothing under `/etc` changes.

**The session choice survives Safe Mode** (2026-11-11). LightDM saves the session of every logon through a greeter
as the account's choice: AccountsService's `XSession`, and `~/.dmrc` (LightDM 1.32, `seat.c`,
`greeter_start_session_cb`). There is no setting to stop that. A Safe Mode logon could therefore change an account's
X11 or Wayland choice for good: through the Welcome screen's session list, or through the GTK greeter, which logs on
with its default, X11. `wex-session-choice` (`scripts/wex-session-choice.sh`) undoes it:

- **Snapshot:** `wex-session-choice.service` runs before LightDM. In Safe Mode only, it records each account's
  saved session in `/run/wineexplorer/session-choice/<account>`, root-owned and readable. An account with none gets
  LightDM's `user-session`. The record also notes whether `~/.dmrc` exists.
- **Restore:** a Safe Mode session, X11 or Wayland, starts with `wex-session-choice --restore` as the account. If
  LightDM saved another session, the recorded one goes back through AccountsService (`SetXSession` over D-Bus) and
  `~/.dmrc`. A `~/.dmrc` that LightDM created at this logon is removed.

An automatic logon uses the account's own saved session. Until 2026-11-11 `50-wineexplorer.conf` had
`autologin-session=wine-explorer`, which started X11 at every automatic logon, even for an account that had chosen
Wayland.

**The Administrator account and hidden accounts** (work order C4 and C7, decisions 7 and 8; 2026-11-09):
- The package creates `administrator`, display name Administrator, locked and in the administrators' group.
  Setup gives it the Administrator password by copying root's hash. Without a password it stays locked and
  `wexhost`'s account list leaves it out, as XP leaves disabled accounts out.
- `logonui.exe` shows it in Safe Mode only, as XP Professional does, unless
  `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Winlogon\SpecialAccounts\UserList` says otherwise. Any account
  with a value of 0 there is left out of the Welcome screen.
- The logon prefix is `lightdm`'s `~/.wine`. Where the system role runs (an ISO install has
  `/etc/wineexplorer/sysrun.conf`), its `HKLM` is the machine's, so the list is the machine's and the prefix has no
  `system.reg` of its own; `wex-greeter.sh` therefore treats a prefix with `user.reg` as made. Without the system
  role the list is in the prefix's own registry.
- No SSH logon is possible for root or Administrator (`/etc/ssh/sshd_config.d/wineexplorer.conf`).

**Session.** The session keeps its name (`wine-explorer`), so LightDM records no new session for the account.
`wine-explorer-session.sh` reads the mode:

- `safecmd`: starts the recovery console (`wex-safe-terminal.sh`, an xterm without Wine), XP's `AlternateShell`.
  It has the shape of XP's `cmd.exe` there (work order C6, 2026-11-11): at the top left, 80 columns wide and as
  tall as the screen, light gray on black with an underline cursor and the scroll bar on the right, titled with the
  program's name (`bash`). With no window manager the size comes from the screen's (`xdpyinfo`) and a bitmap font's:
  misc-fixed 8x13, or 7x13 where 80 columns and the scroll bar do not fit, as at Safe Mode's 640x480 (36 rows);
  25 rows when the screen's size is unknown. Its banner fits 80 columns. In the Wayland session `wexcomp` would add
  a title bar and push the last rows off the screen, so a window rule for the console's name,
  `wex-recovery-console` (`scripts/wayland/rc.xml`), leaves the bar out and puts it at the top left there too;
- `safe`, `safenet`: starts Explorer with `WEXMODE` exported. It does what a logon needs (template
  personalisation, the prefix update after a Wine update) and skips what is optional: the saved display mode, the
  native runtime defaults, the theme copy and the Vulkan preparation.

**640x480** (work order C3). XP starts Safe Mode at 640x480 with its basic display driver, which keeps a mode of its
own. In all three Safe Modes `wex-greeter.sh` switches the connected output to 640x480 with `xrandr` before the
Welcome screen starts, and `wine-explorer-session.sh` does the same for a session that gets an X server of its own;
an output without that mode keeps its own. Only the running X server changes: the next start's server begins at
its own mode, and the session restores the saved display profile only in a normal start. Display Properties can
still change the mode in Safe Mode, for that session only: `WexDisplaySet` (wexhost) does not write the profile
while Safe Mode runs (`SM_CLEANBOOT`, or `WEXMODE`). Whether XP keeps a mode chosen in Safe Mode is not measured.
The Wayland session's compositor takes no `xrandr`. In Safe Mode `wine-explorer-wayland-session.sh` exports
`WEX_OUTPUT_MODE=640x480`, and `wexcomp` starts each output in its own mode of that size nearest 60 Hz (branch
`claude/safe-mode-output-mode` of the labwc fork, 2026-11-11). An output without such a mode, or a normal start
(no variable), gets the usual mode, and nothing is saved.

At 640x480 the Welcome screen (`logonui.exe`) takes XP's compact layout, which XP uses at that size (measured on
the XP VM, 2026-11-11). Any screen narrower than 800 pixels gets it:
- The logo sits above the list, with its right edge at column 490 and its bottom at row 166, and "To begin" is 25
  pixels left of the logo's edge and 20 below it.
- The tiles start at the left, with the first picture frame's white border at 165,244 and one tile every 80 rows.
- A fainter divider runs at column 128 from row 209 to 382. There is no shine.
- The account hint starts at column 310 and fits on the screen; in the wide layout it was cut off at 640 pixels.
- The session list, which XP does not have, moves to the empty top left of the panel.

The tiles are in alphabetical order in every layout, as on XP.

The Wayland session (`wine-explorer-wayland-session.sh`, "WineExplorer (Wayland)" in the Welcome screen's session
list) reads the mode the same way (work order A3, 2026-11-10). In `safe` and `safenet` it starts its compositor,
`wexcomp`, and Explorer with `WEXMODE` exported, skipping the same optional steps. In `safecmd` it starts no Wine:
the compositor runs the recovery console as its session command (`wexcomp -S`), an xterm on the compositor's
XWayland, and the session ends when the console does. Unlike X11, where nothing manages the console's window,
`wexcomp` gives it a window frame. Choosing a session on the Welcome screen makes LightDM save it as the account's
own; the Wayland session's own name does not change with the mode.

**Boot menu entry.** Every generation of Vintner Boot has a variant from `/etc/vintner/variants.d/advanced.conf`
with `wexmode=menu`, titled "Advanced Options" after the generation's title (`docs/VINTNER_BOOT.md` section 4.3). It
counts nothing (`docs/SAFE_MODE_WORKORDER.md`, decision 3). Vintner Boot's menu opens with F8 or any key on UEFI and
while Shift is held on BIOS; the variant is the line below the generation. Setup installed GRUB until 2026-11-10,
with an F8 hotkey entry (`11_wineexplorer_advanced`) and a grubenv record of unfinished starts; that path was removed
on 2026-11-11.

**A start that did not finish.** XP shows "Windows did not start successfully" when the last start ended before it
was complete. Vintner Boot counts every ordinary start of a generation and marks it good at `boot-complete.target`.
`vintner status --json` reports `booted.failed_attempts`: the earlier starts of this generation that did not get
there. `wex-failure-check.service` runs on every ordinary Vintner start. It is skipped on starts through the variant
and on a machine Vintner Boot did not start, with conditions on `vintner.bootstorage=` and `wexmode=`. It runs before
`basic.target` and needs no terminal, so a start with nothing to report leaves the boot splash alone. When the count
is above 0, outside Setup, it starts `wex-failure-screen.service`, which shows the failure screen on `tty1`
(section 4).

This start has been counted by the loader, so the choices work as follows:

- **Start Normally (the countdown's choice):** this start goes on. Once the logon screen is up it counts, Vintner
  marks it good, and the next start shows no screen.
- **A Safe Mode:** recorded for one start in `/var/lib/wineexplorer/next-bootmode` (mode and generation, root
  only). Then `vintner next-boot --json variant advanced --generation <n>` and a restart. The variant counts nothing,
  as a Safe Mode start never does (decision 3). The menu sees the record there, removes it, and starts that mode at
  once with no menu. The start that showed the screen did not finish, so the next ordinary start shows it again.
  The generation is named because the screen is most likely on its last try (two failed starts before this one).
  There, Vintner's plain `variant <id>` takes "the generation that would boot next", which is an older one or none
  (`nothing-bootable`). A vintner without `--generation` (Vintner Boot `a24d9a4`, a usage error) gets the plain
  request. If its reply names another generation, the request is taken back (`vintner next-boot clear`). In either
  failure the journal gives vintner's reason, and this start goes on normally.
- **Last Known Good:** the previous good generation when this one never started successfully (as from the menu).
  Otherwise the registry's control set goes back and this start goes on and counts, as XP's does.

`wex-startup-check.service` (`scripts/wex-startup-check.sh`) decides when the start is complete, which here means
the logon screen is up:

- `logonui.exe` or the GTK greeter runs for the `lightdm` account. The program itself must be running: Explorer's
  `/desktop=Logon,... logonui.exe` command line names it before the Welcome screen exists, and a Wine that fails at
  that point must not count;
- or there is a graphical session after an automatic logon (a console or serial logon does not count);
- or, while Setup runs, `wex-setup.service` or `wex-oobe.service` is active.

It waits up to 15 minutes, enough for a prefix update after a Wine update, and otherwise fails. The unit is ordered
before `boot-complete.target`, where Vintner Boot marks a start good (`docs/VINTNER_BOOT.md` section 4.4), so a
failed check leaves the start uncounted. Starts through the Advanced Options variant are not counted. Safe Mode boots
do not reach `multi-user.target`, so the check does not run in them.

## 4. The Advanced Options menu

`wex-advanced-options` (Python, curses) runs on `tty1` when the mode is `menu`, before `basic.target`, so no
timer, socket or service of the normal boot has started yet. A choice writes the mode to `/run/wineexplorer`,
reloads systemd (the generator runs again and reads it) and starts `default.target`. If the menu fails, the
unit's `OnFailure=` starts `graphical.target`.

Plymouth's splash ends first. plymouthd from the initramfs keeps the screen in graphics mode and keeps reading
`tty1` (its quit units are masked), and with two readers the keys were split between them: F8 arrived as a lone ESC,
and arrow keys were lost. Worse, in about 1 menu start in 16 (2 of 36 on 2026-11-10, 5 of about 80 on 2026-11-11)
the screen stayed black until a key was pressed, however long that took. A task dump (SysRq T) during one showed why:
the menu's process did not exist yet. systemd's child for the unit, before it runs the program, was blocked reading
`tty1`: systemd's set-up of the terminal for the unit (`StandardInput=tty-force`, `TTYReset=`) reads the console's
answer to a query, and plymouthd, reading `tty1` as well, had taken it. The next key let the set-up go on; the menu
then started, ended Plymouth and threw the key away (it discards keys pressed before it draws), and the journal
showed plymouthd quitting minutes late. So `wex-plymouth-quit.service`, which has no terminal and runs before the
menu's and the failure screen's units (`Wants=`, `After=`), ends plymouthd before anything sets up `tty1`
(`wex-advanced-options --quit-plymouth`). It asks until `plymouth --ping` says plymouthd is gone (at most 10
seconds), gives each request 2 seconds, then waits for the process: after 1.5 seconds it ends it (TERM), after 1.5
more kills it, and logs either. The menu and the failure screen do the same before they draw, which then finds
nothing to do, and put the console in text mode. (Until 2026-11-10 the unit's `ExecStartPre=` ran `plymouth quit`;
it was set up on `tty1` the same way, so it stalled the same way.) The VM test logs every task (SysRq T) when a menu
has not shown after 30 seconds.
The menu also discards the keys pressed before it appears (the F8 presses that opened it). Unavailable items are
drawn in dark gray (bold black); the Linux console does not render the dim attribute.

**Layout** (work order C5, 2026-11-11), as measured on XP's 80x25 screens: light gray on black; the title and the
other headings from column 0 (rows 1 and 2), the items from column 4 (rows 4 to 17), and the footer one empty row
below the last item (row 19). The highlight is a light gray bar with black text from column 0 to the label's last
letter. The menu opens with Start WineExplorer Normally highlighted, also when F8 brings it back from the operating
system list, and Up and Down wrap round: Up on Safe Mode goes to Return to OS Choices Menu. The only difference from
XP is the grayed items, which XP does not have. On the Linux console, which is wider and taller than 80x25 on most
screens, the rows and columns are the same counted from the top left; the operating system list's last two lines
stay three and one rows above the bottom, where XP has them.

| Item | Here |
|---|---|
| Safe Mode | `safe` |
| Safe Mode with Networking | `safenet` |
| Safe Mode with Command Prompt | `safecmd` |
| Enable Boot Logging | Normal start; when it is complete, `wex-bootlog` appends the loaded drivers (kernel modules) and the services that failed to `C:\WINDOWS\ntbtlog.txt` in the machine root. Every Safe Mode start writes the log too, as on XP: the boot-mode generator adds `wex-bootlog.service` to the Safe Mode target. The file has XP's format (work order C6): UTF-16LE with a byte order mark at its start, CRLF line ends, and for each start a header line, a space, the version Wine reports in the machine registry, then the date and time, with no empty line between starts. With a service pack string (`CSDVersion`, as winecfg's XP setting writes) the header is XP's: ` Service Pack 311  8 2026 08:29:50.500`. Wine's default registry (Windows 10) has none, so the product name, version and build stand in its place, followed by a space: ` Windows 10 Pro Version 10.0 (Build 19045) 11 10 2026 09:40:51.253`. A log written before this format is converted once |
| Enable VGA Mode | Normal start at 640x480 with the display driver it has, as XP's (work order D2, 2026-11-11). The menu records the option in `/run/wineexplorer/bootoption` (`wex-bootmode --option` reads it), and the greeter and both sessions start at 640x480 as in Safe Mode (`xrandr`, or `WEX_OUTPUT_MODE` for `wexcomp`). The session skips the account's saved display mode, so a mode chosen in Display Properties then replaces it. The next normal start has its saved mode again. Not a basic driver: a broken graphics driver needs Safe Mode or the Recovery Console |
| Last Known Good Configuration | Normal start with the machine registry's control set from the last start that counted (work order D3; below). With Vintner Boot, from a generation that has not started successfully: a restart into the previous good generation as well. Grayed until one start after Setup has counted, or a previous good generation exists |
| Directory Services Restore Mode | Grayed: does not apply |
| Debugging Mode | Normal start with the service manager logging at debug level |
| Disable automatic restart on system failure | Normal start with `kernel.panic=0` |
| Start WineExplorer Normally | Normal start, straight away |
| Reboot | Restart |
| Return to OS Choices Menu | A restart into the boot loader's own menu (`vintner next-boot menu`, 30 seconds), which lists the generations, the Advanced Options variant and the Recovery Console, as XP's boot.ini list does. When Vintner cannot set it: the operating system list (one entry), as XP shows it after F8 |

Safe Mode items are grayed while setup is pending. A mode choice goes to the operating system list with the mode
at the bottom; ENTER starts it and F8 returns to the menu.

**Last Known Good Configuration** (`scripts/wex-lastgood.sh`, installed as `/usr/local/sbin/wex-lastgood`). XP keeps
a copy of `HKLM\SYSTEM\CurrentControlSet`, the settings of drivers and services, from the last start that succeeded.
Last Known Good goes back to that copy and keeps the replaced settings as the failed control set; installed
programs, user settings and files stay as they are. WineExplorer does the same with the machine registry
(`/var/lib/wineexplorer/machine/system.reg`), where Wine keeps the control set in `System\ControlSet001` and
`System\CurrentControlSet` links to it.

- **Save:** when the start-up check counts a start as successful (section 4), outside Setup, it runs
  `wex-lastgood save`, which copies the control set's keys to `/var/lib/wineexplorer/lastgood/controlset.reg`
  (root only). Starts through the menu are not counted, so they save nothing (decision 3); a start that goes on
  from the failure screen with Start Normally counts, and saves.
- **Restore:** the menu item, and the same item on the failure screen, runs `wex-lastgood restore` before
  `default.target` starts, so before any Wine program reads the registry. It writes the saved keys in place of the
  control set's keys, keeps every other key (`Software` included), and keeps the replaced keys as
  `failed-controlset.reg`. The new file is a copy of the old one (owner, mode and ACLs) renamed over it, so the
  prefixes' links to it stay valid. If the restore fails, the menu starts normally.
- **The previous good generation.** With Vintner Boot the kernel and initramfs are part of the last known good
  configuration too. Before drawing, the menu reads `vintner status --json` (Vintner Boot 4.5, its
  `docs/status.md`). This boot's generation may be newer than the newest `GOOD` one, which means it has not
  started successfully yet. Then Last Known Good is offered even without a saved control set. Choosing it runs
  `vintner next-boot previous-good`, puts the control set back if one was saved, and restarts. The previous good
  generation then starts normally, and that start counts. When this boot's generation is the newest good one, only
  the registry goes back and this boot goes on. If Vintner refuses the request, this boot continues with the
  registry alone.

**The failure screen.** `wex-failure-screen.service` (section 3) shows the "did not start successfully" screen,
laid out as XP's, with an explanation in our own words.

- The list is the three Safe Modes, Last Known Good Configuration (grayed until a start has counted) and Start
  WineExplorer Normally, which is highlighted.
- "Seconds until WineExplorer starts" counts down from 30 on the row directly under the footer and then starts
  normally.
- An arrow key stops the countdown and removes its line, as on XP. Up and Down wrap round as in the menu: Down on
  Start Normally goes to Safe Mode.
- Enter on a Safe Mode starts it without the operating system list, through a restart into the variant (section 3).
  The XP captures did not cover that case.
- Layout as XP's (C5): the explanation from row 1, column 0, broken as on 80 columns even on a wider console, then
  an empty row, the list from column 4 with the menu's bar, and the footer one empty row below the list.

## 5. Phase 2: Windows programs see Safe Mode

Built on 2026-11-09 (`docs/SAFE_MODE_WORKORDER.md` B1-B4, B6, B7, C1, C2). The Wine part is commit `46f3a86e33d` on
the local Wine branch `claude/vintner-boot-integration` (`docs/STOCK_CHANGES.md`); the `wine` submodule still pins `cf1c29be`
until that branch is merged together with the E1 work (B8). The shell part is in WineExplorer.

**What Windows programs see** (Wine):

- `wineboot --init` reads `/run/wineexplorer/bootmode` (section 3) and creates the volatile
  `HKLM\System\CurrentControlSet\Control\SafeBoot\Option` key: `OptionValue` 1 in Safe Mode and with Command
  Prompt, 2 with Networking, and `UseAlternateShell` 1 with Command Prompt. In a normal start the key does not
  exist, as on XP. Without the file, as in a stock prefix, nothing changes.
- `GetSystemMetrics(SM_CLEANBOOT)` returns `OptionValue`.
- services.exe starts only the services that `SafeBoot\Minimal` (or `SafeBoot\Network`) names, by service name or
  load order group, and refuses to start any other with error 1084 (`ERROR_NOT_SAFEBOOT_SERVICE`), as XP does.
  `wine.inf` lists Wine's own core services: EventLog, MountMgr, PlugPlay, RpcSs, winebus, wineusb, Winmgmt and
  E1's elevation broker `wexelevate`; with Networking also LanmanServer, NDIS, nsiproxy and TermService. A prefix
  made before the lists existed holds nothing back. Under the system role the lists live in the machine root's
  `HKLM`, which only the Wine package's postinst updates (`wex-setup-machine.sh --update`); a user's own prefix
  update cannot write it.
- `PaintDesktop` paints the Safe Mode desktop for desktops that WineExplorer's shell does not paint.

**The desktop** (`src/dlls/wexshell`). Explorer paints black instead of the wallpaper, with "Safe Mode" in white in
the four corners of the work area and "WineExplorer (Wine <version>)" centred on the top row, in the window title
font, smoothed. It takes Safe Mode from `SM_CLEANBOOT`, or from the session's `WEXMODE` while the Wine branch is not
merged. Before the taskbar starts it shows a Yes/No message box titled "Desktop", in WineExplorer's own words, with
Yes as the default; the desktop icons wait for the answer. Yes goes on; No logs off, until a restore tool can take
the place of XP's System Restore (owner decision 2). Safe Mode with Command Prompt starts no Explorer, so it shows
neither.

`RunOnce` (B5, 2026-11-11, Wine branch commit `d4e95e24dee`): in Safe Mode wineboot runs only the values whose
name starts with `*`, leaves the rest for the next normal start and skips the Run keys. A `!` value is deleted once
its command has ended, as on XP. Still open: the classic logon dialog after Ctrl+Alt+Del twice (C4).

## 6. Tests

`scripts/test_safemode.py` (113 tests, Linux) covers:

- the end of Plymouth: asking until it is gone, each request with a time limit, then ending a plymouthd that
  lingers or does not answer; and the unit without a terminal that does it before the menu's and the failure
  screen's units set up `tty1`;
- the recovery console's shape on screens of several sizes, and its banner within 80 columns;
- mode parsing, generator output and PAM decisions;
- menu navigation (opening on Start Normally, wrapping round) and choices, and the logon screen choice;
- the layout of the menu, the operating system list and the failure screen, row by row against XP's, and the VM
  test's row numbers and its highlight finder;
- 640x480 in Safe Mode: the greeter switches the output in each Safe Mode and not in a normal start, and the
  session does so before it reads the desktop's size;
- the failure screen: its items, the countdown, and arrow keys stopping it;
- the start-up check: which starts count, and what counts as the logon screen;
- the failure check and screen with Vintner Boot's status report; its Safe Mode choice naming its generation, with
  a vintner that refuses and one without `--generation`; Last Known Good going back a generation; and the Advanced
  Options variant's file;
- that the GRUB path is gone (Setup, the live package list, the package and the tests);
- the postinst migration from the old implementation (run twice to show it is idempotent), including the standard
  users it hid, and the units it enables;
- the boot mode record: written once, never for the menu, its unit, and `/run/wineexplorer` belonging to root;
- Enable VGA Mode: the start option and `wex-bootmode --option` (root-owned file only), and 640x480 in the greeter
  and both sessions with the saved display mode skipped;
- that the greeter does not update a logon prefix that is already made;
- the GTK fallback greeter: 640x480, its drop-in directory last in `XDG_DATA_DIRS`, and its keys winning over
  Debian's in lightdm-gtk-greeter's reading order; and the VM test's pixel reader;
- the session choice through Safe Mode (`wex-session-choice`): the snapshot in Safe Mode only, with LightDM's
  default for an account that has none; the restore through AccountsService and `~/.dmrc`, only from root's record,
  and the removal of a `~/.dmrc` made at the logon; and both session scripts calling it first in Safe Mode only;
- a normal Wayland session restoring the display mode saved in Display Properties, and Safe Mode or Enable VGA Mode
  keeping 640x480;
- the Wayland session in each mode, with a stand-in compositor and Wine, and its 640x480 in each Safe Mode;
- the boot log: two starts in XP's format, the header's version text from XP's and from Wine's default registry
  values and with no registry, and the conversion of a log from before the format;
- Last Known Good: which starts save the control set, that only the control set is saved and put back (with
  continuation lines, and not a key that merely starts with the same name), that the rest of the registry and the
  file's mode are kept, a restore with no copy, and the item in the menu and on the failure screen;
- that the VM test installs the Safe Mode files where the package does.

`installer/tests/safe-mode.sh` is the VM test (usage in `docs/TOOLING.md`). It boots a copy-on-write overlay of an
installed test disk once per phase, opening Vintner Boot's menu (Shift held on BIOS, F8 on UEFI) and choosing the
Advanced Options variant, and checks each start on the screen and over the serial console. In the menu it presses
Down until a screenshot shows the highlight bar on the item's row (`installer/tests/highlight-row.py`), and it
checks that the menu opens on Start Normally. By default it tests what
an ISO installed; with `WEX_SAFEMODE_FILES=branch` it first copies this checkout's Safe Mode files where the session
package installs them and runs the package postinst's Safe Mode part in the guest. Before and after every boot it
records the checksums of every file and link under `/etc` and `/var/lib/AccountsService` and of the accounts'
`.dmrc`; a boot that changes one fails. (Until 2026-11-11 it also tested GRUB installs, with a `failed` phase for
GRUB's record of unfinished starts, and `WEX_SAFEMODE_FILES=upgrade`, an upgrade from the GRUB Safe Mode before
`14286423`; the runs below record those.)

With `WEX_TEST_PROBES` it also runs `installer/tests/safeboot-probe.c`, built as a 32-bit and a 64-bit program,
in the logged-on session. Each prints `SM_CLEANBOOT`, the `Option` key's values, the state of EventLog, PlugPlay,
MountMgr, NDIS and nsiproxy, and, in Safe Mode, the error from starting the spooler, which no SafeBoot list names.
`WEX_TEST_SAFE_BOX=required` fails a Safe Mode logon that shows no message box.

| Phase | Checks |
|---|---|
| `safe` | OS list shows "Safe Mode"; the Welcome screen instead of automatic logon, with the start-up check's pattern matching exactly one `logonui.exe` process; `wex-safemode.target`; NetworkManager, cron, the Prefetcher and every timer inactive; the message box, answered Yes, then the desktop; Explorer session with `WEXMODE=safe`; probes: `SM_CLEANBOOT` and `OptionValue` 1, no alternate shell, EventLog, PlugPlay and MountMgr running, NDIS and nsiproxy stopped, the spooler refused with 1084; Wine's 32-bit (`syswow64`) and 64-bit (`system32`) Notepad open; the screen at 640x480 on the Welcome screen and the desktop (C3; also checked in `safenet` and `safecmd`) |
| `safenet` | The same with `wex-safenet.target`, NetworkManager active and `WEXMODE=safenet`; probes: 2, and NDIS and nsiproxy running |
| `safecmd` | The Welcome screen; the recovery console after logging on, 80 columns by 30 rows or more (its terminal's size, read over the serial console; images whose console predates C6's shape are reported), and no wineserver; then the probes, started with `wine` from the serial console as from the recovery console (output to a file): 1, `UseAlternateShell` 1, the services as in `safe` |
| `nowine` | Safe Mode with Command Prompt with `/usr/local/bin/wine` hidden for the boot: the GTK greeter takes over, black (its bottom left pixel) and at 640x480 on images that have its drop-in (A4), and logging on still starts the recovery console |
| `bootlog` | Normal start with automatic logon; `ntbtlog.txt` gains driver lines |
| `vga` | Only when named. With a 1024x768 display profile saved for the test account: Enable VGA Mode is a normal start with automatic logon at 640x480, with the start option `vga` recorded and the profile skipped. The next normal start is at 1024x768 |
| `startnormal` | Start WineExplorer Normally: automatic logon; timers, cron and NetworkManager active; the start-up check reports "not counted" |
| `oschoices` | Return to OS Choices Menu: a restart into the loader's menu with no key held, recognized by its Recovery Console entries, then Enter on the generation: a normal start |
| `normal` | No key: the same, the boot-mode generator wrote nothing, and the start-up check counted the start; the record says `normal` in a root-owned `/run/wineexplorer`; probes: 0, no `Option` key, all five services running; the screen not at 640x480 |
| `lastgood` | Last Known Good (work order D3) from a generation that started successfully. With a control set saved by a start that counted (a normal start first if there is none), the test adds a key under the machine registry's `System\ControlSet001\Control` and a `Software` key on the stopped disk (Wine writes no block for a key without values, such as `Control` itself, so the test adds a key rather than a value). Last Known Good from the menu is then a normal start with automatic logon, with no restart, not counted, and Wine's 32-bit and 64-bit Notepad open. On the disk afterwards the control set key is gone, the `Software` key is still there, and the control set key is in `failed-controlset.reg` but not in the saved copy. Once Last Known Good is offered, Up from Start Normally on the failure screen reaches it before Safe Mode with Command Prompt, which `vfailed` follows |
| `vfailed` | With LightDM held back 90 seconds for this phase (a drop-in under `/usr/lib`, removed afterwards): the Reset button (QMP `system_reset`) once the serial console answers, before `boot-complete.target`. An automatic logon follows Linux within seconds, too soon to reset reliably without the hold. The next ordinary start shows the failure screen with its countdown. Up stops it and removes its line, and Safe Mode with Command Prompt restarts into the variant, which shows no menu and comes up in that mode. The next ordinary start shows the screen again; left alone, it starts normally and counts. The start after that shows no screen |
| `waylandauto` | Only when named. With Wayland saved as the test account's session (on the stopped disk, put back afterwards), a normal start's automatic logon runs the Wayland session (`wexcomp`) |
| `waylanddisplay` | Only when named; needs `WEX_TEST_WINE` and the display probes (`installer/tests/display-probe.c`) in `WEX_TEST_PROBES`. In that Wayland session, the 64-bit probe lists the compositor's real output and modes through wexhost (`WexDisplayGet`, as Display Properties) and sets 1024x768 with the profile saved (`WexDisplaySet`). The screen changes, the 32-bit probe sees it, and the next Wayland session starts at 1024x768 |
| `vlasttry` | Only when named, and last: it leaves the generation with no tries. With LightDM held back as for `vfailed`, it resets once before the logon screen and once on the next start's failure screen. The third start is the generation's last try. Its failure screen's Safe Mode with Command Prompt must still restart into the variant in that mode (`vintner next-boot variant --generation`). Read from the stopped disk's journal, that screen's start logged 2 earlier failures and no refusal. An image whose Vintner Boot lacks the flag is reported, not failed |
| `lastgoodgen` | Last in the default list. The test appends a token to the Advanced Options variant on the stopped disk and adds a unit that runs `vintner publish` at the next start and powers off, so generation 2 is published. The variant is then put back. Generation 2 is started only through its variant, which counts nothing, so it never starts successfully. The registry is changed as in `lastgood`. Last Known Good from generation 2's menu then restarts with no menu into generation 1: a normal start whose recorded `vintner status --json` says `booted` generation 1, generation 2 not `GOOD`. The registry ends as in `lastgood` |
| `waylandsafe` | Only when named. Safe Mode with "WineExplorer (Wayland)" chosen in the Welcome screen's session list: the message box, the desktop, `WEXMODE=safe`, and a `wexcomp` process for the account. The test account's AccountsService file and `.dmrc` are left out of the record, because LightDM saves the chosen session there |
| `waylandcmd` | Only when named. Safe Mode with Command Prompt in the Wayland session: the recovery console, no wineserver, and `wexcomp` running |
| `accounts` | Needs `WEX_TEST_WINE`, a tarball of this branch's Wine. Provisioning gives Administrator a random password and adds `wexhidden` ("Hidden Test") with a value of 0 in the logon prefix's `SpecialAccounts\UserList`. The Safe Mode Welcome screen then shows Administrator and the test user but not Hidden Test, and Administrator logs on, through the message box. In a normal start, the logon screen (after switching user) shows the test user only |

After a run that showed the Welcome screen, the test checks that the greeter's prefix
is `lightdm`'s `~/.wine` and that the old `~/.wine-logon` is gone (work order C7). That passed on SeaBIOS and OVMF
on 2026-11-08, together with `safe`, `safecmd` and `nowine`.

Run 2026-11-08 on the round 3 GPT installs (`docs/VINTNER_BOOT.md` section 4.1) with this branch's files: all
seven phases passed on SeaBIOS and on OVMF in one run, about 13 minutes per firmware, with the Welcome screen in
Safe Mode and the GTK greeter taking over when Wine was hidden. No boot changed a file under `/etc`
or `/var/lib/AccountsService`, or a `.dmrc`. The round 3 image predates the prefetch service, so the Prefetcher
reads "absent" there; its mask is covered by the generator's unit test until the A2 install
(`docs/SAFE_MODE_WORKORDER.md`).

With the failure screen (D1, 2026-11-08):
- All eight phases passed on SeaBIOS.
- On OVMF the first seven passed in the same run. `failed` did not, because its first version waited for GRUB's
  "Loading Linux" text, which OCR did not find on OVMF.
- After the change to the serial-console step above, `failed` passed on both firmwares.
- The product code did not change between those runs.
- In `safe`, the start-up check's pattern matched only `logonui.exe`. Wine shows that process's command line as
  the bare `logonui.exe`, and the same pattern did not match `explorer.exe /desktop=Logon,1280x800 logonui.exe`.
- No boot changed a recorded file.

With the Administrator account and hidden accounts (C4 and C7, 2026-11-09): all nine phases passed on SeaBIOS and
on OVMF with this branch's Wine, unpacked over the round 3 image's October Wine through `WEX_TEST_WINE`. No boot
changed a recorded file.

Seen during the run and not part of this change: the installed image sets `LANG=en_US.UTF-8` without generating
that locale, so the recovery console prints `setlocale` warnings.

**Installed from the ISO** (A2, 2026-11-09). The default (GRUB) ISO, built in wexbuild with this branch, installed
with no input on SeaBIOS and on OVMF. With `WEX_SAFEMODE_FILES=installed` all nine phases passed on both. It was
the first run with the prefetch service, which was inactive in every Safe Mode and active otherwise. The check of
the logon prefix failed at first: an ISO install has the system role, so that prefix has no `system.reg`, and the
test, `check-lightdm.py` and the greeter all expected one. The greeter therefore ran `wineboot -u` at every start
of the logon screen. All three now look for `user.reg`.

**Upgrade** (A2, 2026-11-09). An ISO built from the tree before `14286423`, with today's Wine, installed on both
firmwares; `WEX_SAFEMODE_FILES=upgrade` then started its Safe Mode with Command Prompt, added a standard user and
installed this branch's packages. The old Safe Mode also wrote an AccountsService file for every account that had
none, hiding standard users, and the postinst used to restore only the files it had saved (found in the old code
while writing the test); it now shows those accounts again, and the test's standard user checks it. The run passed
on both firmwares: of the old Safe Mode's drop-in, `/run` backups, PAM line, sessions, hidden accounts, unit links
and GRUB entry nothing was left, and the automatic logon was back.

**Phase 2** (B and C, 2026-11-09). This branch's Wine, unpacked over the A2 installs through `WEX_TEST_WINE`, with
the probes and `WEX_TEST_SAFE_BOX=required`. The first runs showed the box, `SM_CLEANBOOT` and `OptionValue` right,
but every service started in Safe Mode: unpacking the tarball had not updated the machine root, so its `HKLM` had no
SafeBoot lists and services.exe held nothing back, as designed for a prefix without them. The provisioning boot now
runs `wex-setup-machine.sh --update` after the tarball, as the Wine package's postinst does. (OCR also read the box's
last line in pieces; the test now waits for its first.) The next run found that NDIS and nsiproxy stayed stopped
with Networking: services.exe starts a driver through an internal `Winedevice` host service, and the first version
checked the lists in `service_start`, which refused that host, so no driver started in any Safe Mode. The check
moved to `StartService`, and the probe now also reports MountMgr, a listed driver.

With that, all nine phases passed on SeaBIOS and on OVMF (the first `safecmd` of that run read no probe output
over the serial console; it passed alone once the probe wrote to a file and had 180 seconds). The 32-bit and
64-bit probes agreed everywhere:

| Start | `SM_CLEANBOOT` | `UseAlternateShell` | EventLog, PlugPlay, MountMgr | NDIS, nsiproxy | Spooler |
|---|---|---|---|---|---|
| Safe Mode | 1 | none | running | stopped | refused, 1084 |
| with Networking | 2 | none | running | running | refused, 1084 |
| with Command Prompt | 1 | 1 | running | stopped | refused, 1084 |
| Normal | 0 (no `Option` key) | none | running | running | not tried |

The message box appeared and was answered in `safe`, `safenet` and `accounts`, Wine's 32-bit and 64-bit Notepad
opened in Safe Mode, and no boot changed a recorded file.

**Through Vintner Boot's variant** (V1, 2026-11-10). On disks installed from an ISO built with `--vintner`
(`docs/VINTNER_BOOT.md` section 4.3), `WEX_TEST_LOADER=vintner` with `WEX_SAFEMODE_FILES=installed` reached the
menu through the Advanced Options variant instead of F8. `safe`, `safenet`, `safecmd`, `nowine`, `bootlog`,
`startnormal`, `normal` and `accounts` passed on SeaBIOS and on OVMF, with the Wine the ISO installed and without
the probes. Every start through the variant carried `vintner.variant=advanced` and `normal` none; Wine's 32-bit and
64-bit Notepad opened in Safe Mode, and no boot changed a recorded file. Getting there:

- The first runs never saw the loader menu. Vintner's BIOS GRUB checks Shift within 2 seconds of power-on, before
  a hold sent after `vmtest.py start` arrives, and QEMU refuses input while a VM is paused ("VM not running"). The
  test now starts the VM paused, and `vmtest.py hold --cont` runs it and holds the key in the same QMP session.
- The first try at that hung: after QEMU's error, `vmtest.py` left its QMP connection open, and QEMU serves one
  client at a time, so the next call waited for good. QMP calls now always close their connection.
- On UEFI, Space held for 15 seconds once missed systemd-boot (one retry, in `accounts`); it is now held for 25.
- `check-vintner.py` looked for the variant's UKI on the ESP; Vintner keeps UKIs on boot storage.
- The provisioning boot ran `update-grub`, which grub2-common keeps on a Vintner install, and it wrote a
  `grub.cfg`. It now runs only where `/boot/grub/grub.cfg` exists.

**The Wayland session** (A3, 2026-11-10). `waylandsafe` and `waylandcmd` passed on SeaBIOS and on OVMF, on the A2
installs with this branch's files and Wine (`WEX_TEST_WINE`, `WEX_TEST_SAFE_BOX=required`). Safe Mode showed the
message box and the Safe Mode desktop under `wexcomp`; Safe Mode with Command Prompt showed the recovery console in
`wexcomp` with no wineserver. The first Wayland logon changed only the test account's AccountsService file and
`.dmrc`, which LightDM writes for a newly chosen session; the second changed nothing. (The first run could not find
the session list: OCR read its "(X11)" as "0¢11)", so the test now clicks the word "WineExplorer".)

**The boot log in XP's format** (C6, 2026-11-10). All nine phases passed on SeaBIOS and on OVMF, on the A2 installs
with this branch's files and Wine and the probes. Every Safe Mode start (`safe`, `safenet`, `safecmd`, `nowine`)
added 98 to 104 driver lines to `ntbtlog.txt`, and Enable Boot Logging still added its own. The file on the OVMF
disk afterwards was UTF-16LE with a byte order mark, CRLF line ends and seven header lines, one per logged start.

**Last Known Good** (D3, 2026-11-10). With this branch's files, Wine and probes on the A2 GRUB installs, all ten
phases (`lastgood` and `accounts` included) passed on SeaBIOS and on OVMF, except `lastgood` itself, whose first
version added a value to `System\ControlSet001\Control`: Wine writes no block for that key, so the change never
reached the file. With a key added instead, `lastgood` passed on both firmwares: after the Last Known Good start
the control set key was gone, the `Software` key was kept, and the key was in `failed-controlset.reg`. `startnormal`
and `failed` passed with Last Known Good offered (one item more above Start Normally).

**Vintner Boot's next-boot and status (D3, 2026-11-10).** An ISO built with `--vintner` and Vintner Boot's packages
from `edbb723` (`vintner next-boot`, product name and `vintner status --json`; the same tree as main at `2ece4a9`)
installed with no input on SeaBIOS and OVMF. The Safe Mode test through the variant then passed all nine phases on
both firmwares. On BIOS, eight passed before a host restart and `lastgoodgen` after it.

- `oschoices` restarted into the loader's own menu with no key held.
- In `lastgoodgen`, Last Known Good from generation 2's variant restarted into generation 1. The status report that
  start recorded says `booted` generation 1, `newest_good` 1 and generation 2 still `TRYING`. The registry ended as
  in `lastgood`.
- `lastgoodgen`'s first run failed because the test ran `vintner status` while `vintner publish` ran: status
  mounts boot storage read-only on demand, and publish then got EBUSY for its read-write mount. The test now
  publishes first, and Vintner Boot PR 86 retries such mounts for five seconds.
- Under GRUB, with this branch's menu, `safe`, `safecmd`, `startnormal`, `oschoices` and `failed` passed on both
  firmwares, and `lastgood` on SeaBIOS.
- None of the nine menu starts through the variant needed more than one Plymouth quit request. Before the retry,
  two in about 36 had needed more and shown a blank menu.

**The failure screen with Vintner Boot (D1, 2026-11-10).** An ISO built with the same Vintner Boot packages and
`wex-failure-check.service` installed with no input on SeaBIOS and OVMF. The Safe Mode test through Vintner Boot
then passed all ten phases on both firmwares, `vfailed` included:

- After a reset before the logon screen, the next ordinary start showed the screen with its countdown.
- Safe Mode with Command Prompt restarted into the variant, which showed no menu and came up in that mode.
- The start after showed the screen again; its countdown started normally, and that start counted. The start after
  that showed none.

With this branch's files on the GRUB installs, `failed`, `safe` and `normal` passed on both firmwares. The new unit
is enabled there, but its condition skips it.

**Without the GRUB path (2026-11-11).** An ISO built after the GRUB path was removed (live system without a stock
loader; Vintner Boot from the submodule at `a24d9a4`) installed with no input on SeaBIOS and OVMF. `vintner install`
had no stock loader to purge and built the BIOS core image with `grub-common`'s `grub-mkimage`. With the test's new
defaults:

- `safe`, `safenet`, `safecmd`, `nowine`, `bootlog`, `startnormal`, `normal`, `lastgood` and `vfailed` passed on
  both firmwares, `oschoices` on SeaBIOS. Wine's 32-bit and 64-bit Notepad opened in Safe Mode and after Last Known
  Good, every Safe Mode start logged 104 to 106 driver lines, and no boot changed a recorded file.
- `lastgoodgen` failed on both: `lastgood`, now before it in the list, had left its Software test key (Last Known
  Good keeps it, as it should), so the count was 2. Each phase now adds a key of its own.
- `oschoices` failed once on OVMF: one Down press of nine was lost, so Enter chose Reboot, which restarted to the
  desktop. The test now presses Down past the end of the menu and checks that one more press changes nothing.
- With those two changes, `oschoices`, `lastgood` and `lastgoodgen` passed on both firmwares from new overlays.
- `WEX_SAFEMODE_FILES=branch` on SeaBIOS (`safe`, `startnormal`, `normal`): the provisioning boot ran this checkout's
  postinst Safe Mode part, whose GRUB cleanup does nothing on a Vintner install, and all three passed.

The session package's GRUB cleanup (`docs/VINTNER_BOOT.md` section 4.2) was tried on an overlay of a disk Setup
installed with GRUB on 2026-11-10: one boot ran this checkout's postinst Safe Mode part, after which `grub.cfg` had
no F8 entry and grubenv no `wex_boot_pending`, and the next two ordinary starts each reached the desktop.

**The black menu and the recovery console's shape (2026-11-11).** With `wex-plymouth-quit.service` and the console
of work order C6, from new overlays of those installs (`WEX_SAFEMODE_FILES=branch`), on both firmwares:

- `startnormal` passed 30 times out of 30 on each firmware. No menu was late, against 5 of about 80 that morning.
  In each overlay's journal, all 32 menu starts and both failure screens began after `wex-plymouth-quit.service`
  had finished.
- `safecmd` (X11, 640x480) and `waylandcmd` (1280x800) passed with a console of 36x80 and 61x80. Its title bar
  reads "bash" in both sessions.
- `vfailed` failed on both firmwares. The provisioning start, which powers off before `boot-complete.target`, had
  left a failed try. So the screen came one start early, and the test's reset made the next start generation 1's
  last try. There, `vintner next-boot variant advanced` refused (`nothing-bootable`), and the start went on
  normally. The provisioning start now runs `vintner bless`. On a last try, the failure screen names its generation
  (section 3), which needs a Vintner change (`docs/VINTNER_BOOT.md`). With both, `safecmd` then `vfailed` passed on
  both firmwares from new overlays. The provisioning log had `bless=0`, the screen came after the one reset, and
  Safe Mode with Command Prompt restarted into that mode: `a24d9a4` refused `--generation` (exit 2) and took the
  plain request.

**640x480 in the Wayland session (2026-11-11).** With this checkout's files and a `wexcomp` built from
`claude/safe-mode-output-mode` (`f2b2dcb1`, unpacked over the overlay through `WEX_TEST_WINE`), on both firmwares:
`safecmd`, `waylandsafe` and `waylandcmd` passed, each at 640x480. In the first run the Wayland console had
`wexcomp`'s title bar (about 27 pixels), so its 36 rows ran about 19 pixels past the bottom of the screen. With the
window rule it sits at the top left with no bar, like the X11 one, and the prompt stays on the screen.

**Enable VGA Mode (2026-11-11).** With this checkout's files, the `vga` phase passed on both firmwares. The OS list
showed "Enable VGA Mode" on its blue mode line. The start was a normal one (graphical target, networking, timers)
with the option `vga` recorded, at 640x480 although a 1024x768 profile was saved. The next normal start was at
1024x768.

**The fallback greeter (2026-11-11).** `nowine` with this checkout's files: the GTK greeter was black, with no
Debian art, but at 1280x800, because `wex-greeter.sh` fell back before it switched to 640x480. It now switches
first. Logging on then failed: the test's fallback click on Log In assumed 1280x800, and now it is placed from the
screen's centre. With both, `nowine` passed on both firmwares: black, 640x480, and a 36x80 console.

**Vintner Boot `f3193a55` and today's work from an ISO (2026-11-11).** The `wex-vgen` ISO has Vintner Boot
`f3193a55` (`next-boot variant --generation`), wexcomp `f2b2dcb1` (`WEX_OUTPUT_MODE`), and this checkout's Safe Mode
files and `logonui.exe`. It installed with no input on SeaBIOS and OVMF. With the installed files:

- All eleven default phases passed on SeaBIOS.
- On OVMF all passed but `vfailed`. Its first start reached the desktop with the serial getty running, but no
  serial login came within 150 seconds. Re-run on a new overlay, it passed. It looks like a one-off on the test
  harness's serial link.
- `waylandsafe`, `waylandcmd` and `vga` passed on both firmwares: 640x480 in the Wayland session from the ISO's own
  wexcomp, a 36x80 console, and Enable VGA Mode at 640x480 then 1024x768.
- `nowine` failed on both, only because it ran after the Wayland phases. The GTK greeter's X11 logon replaced the
  saved Wayland session in `.dmrc` and AccountsService, which the test compares. It now leaves those two files out
  for `nowine` as for the Wayland phases, and passed after `waylandcmd` on both firmwares.
- `vlasttry` first failed in the test itself:
  - the screendump still showed the old failure screen just after the reset, so the test now waits for it to go;
  - the serial console's account cannot read the system journal, so the check now reads it on the stopped disk.
  Safe Mode had started correctly in both runs. With both fixes, `vlasttry` passed on both firmwares (2 earlier
  failures, no refusal, then the variant in Safe Mode with Command Prompt).

**The session choice and Display Properties on Wayland (2026-11-11).** With this checkout's files on the `wex-vgen`
installs, on both firmwares:

- `waylandsafe`, `waylandcmd` and `nowine` passed. The record now compares each account's saved session (`XSession`
  and `~/.dmrc`'s `Session`), and it stayed X11 through Safe Mode logons with Wayland and with the GTK greeter. The
  first run left a `~/.dmrc` that LightDM had created; the restore now removes it.
- `waylandauto` passed: with Wayland saved, the automatic logon ran wexcomp. The first run's failure was in the test,
  which kept its backup copy in AccountsService's directory and could not put the session back.
- `waylanddisplay` passed with the Safe Mode Wine branch built with the new wexhost. The probes saw the output
  `Virtual-1` with 15 modes at 1280x800, set 1024x768 (the screen changed), the 32-bit probe saw it, and the next
  Wayland session started at 1024x768.
- Not covered: Display Properties' own wait and keep-or-revert dialogs on Wayland, and the shell resizing live.
- Both consoles printed six `setlocale` warnings above the banner. The installed system sets `LANG=en_US.UTF-8`
  (Setup, through `locale1`), but that locale was never generated (`/usr/lib/locale` has only `C.utf8`).
