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

`scripts/test_safemode.py` (27 tests, Linux) covers mode parsing, generator output, PAM decisions, menu navigation
and choices, the postinst migration from the old implementation (run twice to show it is idempotent), and that the
VM test installs the Safe Mode files where the package does.

`installer/tests/safe-mode.sh` is the VM test (usage in `docs/TOOLING.md`). It boots a copy-on-write overlay of an
installed test disk once per phase, pressing F8 during GRUB's hidden timeout, and checks each start on the screen
and over the serial console. By default it first copies this checkout's Safe Mode files where the session package
installs them and runs the package postinst's Safe Mode part and `update-grub` in the guest; with
`WEX_SAFEMODE_FILES=installed` it tests what an ISO installed. Before and after every boot it records the checksums
of every file and link under `/etc` and `/var/lib/AccountsService` and of the accounts' `.dmrc`; a boot that
changes one fails.

| Phase | Checks |
|---|---|
| `safe` | OS list shows "Safe Mode"; the Welcome screen instead of automatic logon; `wex-safemode.target`; NetworkManager, cron, the Prefetcher and every timer inactive; Explorer session with `WEXMODE=safe`; Wine's 32-bit (`syswow64`) and 64-bit (`system32`) Notepad open |
| `safenet` | The same with `wex-safenet.target`, NetworkManager active and `WEXMODE=safenet` |
| `safecmd` | The Welcome screen; the recovery console after logging on, and no wineserver |
| `nowine` | Safe Mode with Command Prompt with `/usr/local/bin/wine` hidden for the boot: the GTK greeter takes over, and logging on still starts the recovery console |
| `bootlog` | Normal start with automatic logon; `ntbtlog.txt` gains driver lines |
| `startnormal` | Start WineExplorer Normally: automatic logon; timers, cron and NetworkManager active |
| `normal` | No F8: the same, and the boot-mode generator wrote nothing |

Run 2026-11-08 on the round 3 GPT installs (`docs/VINTNER_BOOT.md` section 4.1) with this branch's files: all
seven phases passed on SeaBIOS and on OVMF in one run, about 13 minutes per firmware, with the Welcome screen in
Safe Mode and the GTK greeter taking over when Wine was hidden. No boot changed a file under `/etc`
or `/var/lib/AccountsService`, or a `.dmrc`. The round 3 image predates the prefetch service, so the Prefetcher
reads "absent" there; its mask is covered by the generator's unit test until the A2 install
(`docs/SAFE_MODE_WORKORDER.md`).

Seen during the run and not part of this change: the installed image sets `LANG=en_US.UTF-8` without generating
that locale, so the recovery console prints `setlocale` warnings.
