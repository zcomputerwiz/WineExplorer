<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Safe Mode and the Advanced Options Menu

Date: 2026-11-08  
Status: phase 1 (boot modes, menu, logon, session) implemented and tested on SeaBIOS and OVMF (section 6); phase 2
(Windows programs see Safe Mode) proposed.  
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
2. else `wexmode=` on the kernel command line (the GRUB entry today, a Vintner variant later);
3. else `normal`.

The Safe Mode values become `normal` while a setup stage is pending (`/etc/wineexplorer/setup/stage`): setup has
to finish first.

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
Mode with Command Prompt can still be reached with a broken Wine. A normal start is unchanged.

Not done yet: the Welcome screen shows the same accounts in Safe Mode as in a normal start. XP adds its built-in
Administrator there and hides the accounts listed in the registry. The plan is a dedicated `administrator` account
(work order C4, decision 7) and the registry's hidden-account list (C7, decision 8), which first needs the logon
prefix to read the machine's `HKLM`.

**Session.** The session keeps its name (`wine-explorer`), so LightDM records no new session for the account.
`wine-explorer-session.sh` reads the mode:

- `safecmd`: starts the recovery console (`wex-safe-terminal.sh`, an xterm without Wine), XP's `AlternateShell`;
- `safe`, `safenet`: starts Explorer with `WEXMODE` exported. It does what a logon needs (template
  personalisation, the prefix update after a Wine update) and skips what is optional: the saved display mode, the
  native runtime defaults, the theme copy and the Vulkan preparation.

The Wayland session is not handled in Safe Mode; choose the X11 session on the logon screen.

**Boot menu entry (GRUB, until Vintner).** `/etc/grub.d/11_wineexplorer_advanced` adds "Advanced Options for
WineExplorer" with `wexmode=menu` and `--hotkey=f8`. During GRUB's hidden timeout a menu hotkey boots its entry
directly (`grub-core/normal/menu.c`), so pressing F8 at start opens the menu as on XP. With Vintner this becomes
the one variant with `wexmode=menu` (`docs/VINTNER_BOOT.md` section 4.3).

**A start that did not finish (GRUB, until Vintner).** XP shows "Windows did not start successfully" when the last
start ended before it was complete. Here `11_wineexplorer_advanced` also adds code that GRUB runs before any entry
boots:

- It remembers the grubenv variable `wex_boot_pending` and sets it to 1 (`save_env`).
- If it was already 1, it makes the Advanced Options entry the default for this start and passes `wexfailed=1`, so
  the menu shows the failure screen (section 4). A one-time entry chosen with `grub-reboot` still wins.
- The Advanced Options entry writes the remembered value back before it loads Linux. A start through F8 therefore
  leaves the state as it was, whatever happens (`docs/SAFE_MODE_WORKORDER.md`, decision 3). On the failure screen
  that value is 1, so that start counts as an ordinary one.

`wex-startup-check.service` (`scripts/wex-startup-check.sh`) clears the variable once the start is complete, which
here means the logon screen is up:

- `logonui.exe` or the GTK greeter runs for the `lightdm` account. The program itself must be running: Explorer's
  `/desktop=Logon,... logonui.exe` command line names it before the Welcome screen exists, and a Wine that fails at
  that point must not count;
- or there is a graphical session after an automatic logon (a console or serial logon does not count);
- or, while Setup runs, `wex-setup.service` or `wex-oobe.service` is active.

It waits up to 15 minutes, enough for a prefix update after a Wine update, and otherwise fails and leaves the
variable set. Starts through the Advanced Options menu are not counted, except Start Normally on the failure screen.
Safe Mode boots do not reach `multi-user.target`, so the check does not run in them.

The unit is ordered before `boot-complete.target`, where Vintner Boot marks a start good (`docs/VINTNER_BOOT.md`
section 4.4), so the same check serves both loaders. Debian's own `recordfail` is not used. When it is set, Debian's
`grub.cfg` shows GRUB's menu for 30 seconds, and `grub-common.service` clears it as soon as `multi-user.target`
starts, before the logon screen exists.

## 4. The Advanced Options menu

`wex-advanced-options` (Python, curses) runs on `tty1` when the mode is `menu`, before `basic.target`, so no
timer, socket or service of the normal boot has started yet. A choice writes the mode to `/run/wineexplorer`,
reloads systemd (the generator runs again and reads it) and starts `default.target`. If the menu fails, the
unit's `OnFailure=` starts `graphical.target`.

The service first runs `plymouth quit`. plymouthd from the initramfs keeps reading `tty1` (its quit units are
masked), and with two readers the keys were split between them: F8 arrived as a lone ESC, and arrow keys were lost.
The menu also discards the keys pressed before it appears (the F8 presses that opened it). Unavailable items are
drawn in dark gray (bold black); the Linux console does not render the dim attribute.

| Item | Here |
|---|---|
| Safe Mode | `safe` |
| Safe Mode with Networking | `safenet` |
| Safe Mode with Command Prompt | `safecmd` |
| Enable Boot Logging | Normal start; when it is complete, `wex-bootlog` appends the loaded drivers (kernel modules) and the services that failed to `C:\WINDOWS\ntbtlog.txt` in the machine root |
| Enable VGA Mode | Grayed: needs an X fallback without the forced `modesetting` driver (`docs/VINTNER_BOOT.md` section 6) |
| Last Known Good Configuration | Grayed until Vintner offers `next-boot previous-good` |
| Directory Services Restore Mode | Grayed: does not apply |
| Debugging Mode | Normal start with the service manager logging at debug level |
| Disable automatic restart on system failure | Normal start with `kernel.panic=0` |
| Start WineExplorer Normally | Normal start, straight away |
| Reboot | Restart |
| Return to OS Choices Menu | The operating system list (one entry), as XP shows it after F8 |

Safe Mode items are grayed while setup is pending. A mode choice goes to the operating system list with the mode
at the bottom; ENTER starts it and F8 returns to the menu.

**The failure screen.** With `wexfailed=1` the menu shows the "did not start successfully" screen instead, laid out
as XP's, with an explanation in our own words.

- The list is the three Safe Modes, Last Known Good Configuration (grayed until Vintner) and Start WineExplorer
  Normally, which is highlighted.
- "Seconds until WineExplorer starts" counts down from 30 under the footer and then starts normally.
- An arrow key stops the countdown and removes its line, as on XP.
- Enter on a Safe Mode starts it at once, without the operating system list. The XP captures did not cover that
  case.
- The screen uses the menu's current layout (columns, highlight bar, footer row), so C5 of the work order will
  change both screens together.

## 5. Phase 2: Windows programs see Safe Mode (proposed)

The plan for this and the other open items is `docs/SAFE_MODE_WORKORDER.md`. In short, these need changes to
stock Wine (`docs/STOCK_CHANGES.md`):

- wineboot creates the volatile `SafeBoot\Option` key (`OptionValue` 1 or 2, `UseAlternateShell`) from `WEXMODE`,
  and `GetSystemMetrics(SM_CLEANBOOT)` returns it (`dlls/win32u/sysparams.c`, a constant 0 today).
- services.exe starts automatic services only if `SafeBoot\Minimal` or `SafeBoot\Network` lists them or their
  group; `wine.inf` gains those lists with Wine's own core services.
- wineboot runs only the `RunOnce` values whose name starts with `*` (Run keys and the Startup folder do not run
  in WineExplorer today, in any mode).
- The desktop draws "Safe Mode" in the corners on black, and Explorer shows the XP message box after logon;
  measured against the XP VM.
- The classic logon dialog after Ctrl+Alt+Del twice on the Safe Mode Welcome screen, as on XP.

## 6. Tests

`scripts/test_safemode.py` (49 tests, Linux) covers:

- mode parsing, generator output and PAM decisions;
- menu navigation and choices, and the logon screen choice;
- the failure screen: its items, the countdown, and arrow keys stopping it;
- the start-up check: which starts count, and what counts as the logon screen;
- the GRUB code that `11_wineexplorer_advanced` generates (GRUB's own syntax check runs in `update-grub` in the VM
  test);
- the postinst migration from the old implementation (run twice to show it is idempotent) and the unit it enables;
- that the VM test installs the Safe Mode files where the package does.

`installer/tests/safe-mode.sh` is the VM test (usage in `docs/TOOLING.md`). It boots a copy-on-write overlay of an
installed test disk once per phase, pressing F8 during GRUB's hidden timeout, and checks each start on the screen
and over the serial console. By default it first copies this checkout's Safe Mode files where the session package
installs them and runs the package postinst's Safe Mode part and `update-grub` in the guest; with
`WEX_SAFEMODE_FILES=installed` it tests what an ISO installed. Before and after every boot it records the checksums
of every file and link under `/etc` and `/var/lib/AccountsService` and of the accounts' `.dmrc`; a boot that
changes one fails.

| Phase | Checks |
|---|---|
| `safe` | OS list shows "Safe Mode"; the Welcome screen instead of automatic logon, with the start-up check's pattern matching exactly one `logonui.exe` process; `wex-safemode.target`; NetworkManager, cron, the Prefetcher and every timer inactive; Explorer session with `WEXMODE=safe`; Wine's 32-bit (`syswow64`) and 64-bit (`system32`) Notepad open |
| `safenet` | The same with `wex-safenet.target`, NetworkManager active and `WEXMODE=safenet` |
| `safecmd` | The Welcome screen; the recovery console after logging on, and no wineserver |
| `nowine` | Safe Mode with Command Prompt with `/usr/local/bin/wine` hidden for the boot: the GTK greeter takes over, and logging on still starts the recovery console |
| `bootlog` | Normal start with automatic logon; `ntbtlog.txt` gains driver lines |
| `startnormal` | Start WineExplorer Normally: automatic logon; timers, cron and NetworkManager active; the start-up check reports "not counted" and `wex_boot_pending` is back at 0 |
| `normal` | No F8: the same, the boot-mode generator wrote nothing, and the start-up check cleared `wex_boot_pending` |
| `failed` | With LightDM held back 90 seconds for this phase (a drop-in under `/usr/lib`, removed afterwards), the serial console shows `wex_boot_pending=1` once Linux runs: GRUB recorded the start. Then the Reset button (QMP `system_reset`) and the failure screen with its countdown. An automatic logon follows Linux within seconds, too soon to reset reliably without the hold, and GRUB's "Loading Linux" text was not readable on OVMF. Up stops the countdown and removes its line, and Safe Mode with Command Prompt starts at once; that start leaves `wex_boot_pending` at 1. The next start shows the screen again; left alone, the countdown starts normally, and that start counts and clears the variable |

Every phase starts with `wex_boot_pending` cleared on the overlay, so a phase that failed does not leave the next
one on the failure screen. After a run that showed the Welcome screen, the test checks that the greeter's prefix
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

Seen during the run and not part of this change: the installed image sets `LANG=en_US.UTF-8` without generating
that locale, so the recovery console prints `setlocale` warnings.
