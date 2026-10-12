# WineExplorer DE - Code Review (2026-10-07)

Scope: everything added or changed after the upstream Wine import (`03476d7`..`d0809c3`, ~15k lines of
custom code, 326 files). Read in full: `timedate.cpl`, `startmenu_view.c`, `taskband.c`, `theme_engine.c`,
`tray_clock.c`, `net_backend.c`, `power_mgmt.c`, `audio_backend.c` (top half), `desktop_view.c` (top half),
`taskbar_props.c` (top), plus the diffs against stock Wine for `explorer.c`, `desktop.c`, `systray.c`,
`ntdll/unix/sync.c`, `comctl32/monthcal.c`, `shell32/dialogs.c`, `desk.cpl/main.c`, headers, scripts.

Not read (listed so nobody assumes they were checked): `calc`, `wmplayer`, `mspaint`, `sysdm.cpl`,
`main.cpl`, `mmsys.cpl`, `powercfg.cpl`, `ncpa.cpl`, `netshell`, `wlanui`, `sndvol32` internals,
`wlan_dialog/status`, `net_props`, `nettray/volumetray/powertray` bodies, `desk.cpl` pages other than Settings.
`ntoskrnl.exe` is stock upstream Wine that the baseline import missed (see Repo hygiene); not reviewed.

Nothing here was built or run. Findings are from reading code; items marked **(verify)** need a run.

---

## Status after the follow-up session (2026-10-07)

Fixed: A1 (Wi-Fi injection; no shell anywhere in `hostcmd.c`), A2 (password never touches the filesystem; later
replaced entirely by the polkit agent), A3 (`NtSetSystemTime` reverted), A4 (taskband no longer destroys foreign
icons), A5 (stock virtual-desktop defaults restored; shell is enabled per desktop by the session script acting
as Winlogon), A6 (token-based switches only), A7 (timezone dirty flag), A8 (no invented network/audio/hardware/
registration data), B3 (audio polled on a worker thread; no UI-thread process spawns), C1-C6 (stock changes reverted
or gated, headers restored, see `docs/STOCK_CHANGES.md`), E (git attribute warning, stray script).
New: `wexhost` (D-Bus: timedate1 + polkit authentication agent), see `docs/DBUS_PLAN.md`.
Findings that turned out different from the review: `RegisterShellHookWindow` is a **stub** in Wine (B1: only the
200 ms poll works); mmdevapi `IAudioEndpointVolume` is a **stub** reporting 100% (so volume still uses the host
sound server's tools until a native PipeWire/Pulse client exists).
Open: A-items none; B1, B2, B4-B16 (except B3/B13 partial), D1-D5, D7.

---

## A. Fix before building more (security / data loss / wrong behavior)

### A1. Shell command injection in Wi-Fi connect - `net_backend.c:365,369`
SSID and passphrase are pasted into `/bin/sh -c "nmcli dev wifi connect \"%s\" password \"%s\""` (wrapper at
`:57`, `:96`). SSIDs are attacker-controlled by any nearby access point: an SSID like `"; cmd #` or `$(cmd)`
executes as the user when someone clicks it. Passphrase appears in `ps`. Also truncates silently at 512/1024 B.
Fix: never go through `sh`. Spawn `nmcli` with an argv (CreateProcess with a properly quoted command line, or
better a small helper that `execv`s), pass the passphrase with `nmcli --ask`/stdin or use the NetworkManager
D-Bus API. Same wrapper pattern is in `audio_backend.c:39,76` and `power_mgmt.c:52` (those take constant
strings today, so only the pattern is dangerous).

### A2. UAC emulation leaks the password via /tmp - `uac.c:108-187`
`run_native_cmd` writes the typed password to `Z:\tmp\.uac_in_<pid>_<tick>` (`CreateFileA`, default mode is
typically 0644 -> readable by every local user while sudo runs, filename is guessable, no `O_EXCL`/symlink
protection), and reads exit status through another `/tmp` file polled for 10 s.
Also: `su user -c ...` (`:230`) needs a controlling tty and will fail on Debian with piped stdin, so the
"other administrator" path probably never works **(verify)**; the 10 s timeout reports "wrong password" even if
the command later succeeds; the whole thing blocks the UI thread in `Sleep(50)` loops.
Fix: feed `sudo -S` through an anonymous pipe as the child's stdin (no file), use the process handle +
`GetExitCodeProcess` instead of the status file (**verify** exit codes of native children work), drop `su`
(or use polkit/`pkexec` and write a polkit auth agent that shows this dialog - the natural long-term design).

### A3. `NtSetSystemTime` silently runs `sudo -n date` - `ntdll/unix/sync.c` (+13 lines)
Any Windows process calling `SetSystemTime`/`SetLocalTime` can now set the host clock whenever passwordless
sudo exists. This bypasses the UAC dialog entirely and runs `system()` inside ntdll. The timedate applet's own
`SetLocalTime` call (`dateandtime.c:175`) is therefore redundant *and* the reason elevation is bypassed.
Fix: revert to `STATUS_PRIVILEGE_NOT_HELD`; do the elevation only in the applet (A2). If a clock-set path is
wanted in ntdll, gate it behind an env var/registry value that only the elevated helper sets.

### A4. Taskband destroys icons it does not own - `taskband.c:246,270,681`
`item->icon` comes from `WM_GETICON` / `GetClassLongPtr(GCLP_HICONSM)` / shared `LoadIcon(IDI_APPLICATION)`
(`get_window_task_icon`), then `DestroyIcon()` is called on removal. Those handles belong to other windows or
are shared; destroying them breaks the other app's title-bar icon (and can crash it). `taskband_update_window`
(`:305`) also overwrites `item->icon` without freeing. Fix: never `DestroyIcon` these; if a copy is wanted use
`CopyIcon` once and destroy only the copy.

### A5. Every `wine explorer /desktop=X` now becomes a full shell - `desktop.c` `get_default_enable_shell`
Default changed from "only for the name `shell`" to always TRUE. Any user of Wine virtual desktops gets taskbar,
Start menu, desktop icons, and `WM_CLOSE` no longer ends the desktop. Fix: keep upstream default, and have the
session script use `/desktop=shell,WxH` (or write `EnableShell` to the registry in the session setup).

### A6. Substring command-line matching in `explorer.c:867-882`
`wcsstr(commandline, L"/wlan")` etc. runs before parsing and matches anywhere, so a path/argument such as
`explorer /e,C:\my-wlan-notes` pops the Wi-Fi dialog and `ExitProcess(0)`. The same switches are then handled
again in the token loop (duplicate, dead). Delete the `wcsstr` block; keep the token-based branches.

### A7. Time Zone page applies on every OK once visited - `timezone.c:536`
Wine's `PROPSHEET_Apply` sends `PSN_APPLY` to every page that has been created, dirty or not. The tz page never
checks a dirty flag, so visiting the tab and pressing OK re-runs `SetTimeZoneInformation`, rewrites registry,
and triggers a **second UAC prompt** (`set_system_timezone_linux`). Fix: track `g_tz_modified` (set in the
`CBN_SELCHANGE`/checkbox handlers, cleared after apply) exactly like `g_time_modified` on the other page, and
batch both pages into one elevation if both changed.

### A8. Fabricated data shown as real - `net_backend.c:302-347,573-676`
When scanning/reading fails the UI shows invented networks ("Corporate-Access", "Guest-Network"...), invented
IP `10.0.2.15`, gateway `10.0.2.2`, link speed, signal 85 %, and byte counters that count up from a fake base.
The adapter name is always "Intel(R) PRO/1000 MT Desktop Adapter" unless the driver is pcnet32 or wireless.
For a "fully functional" DE this misreports state (and makes real failures invisible). Show "no networks" /
blank values; keep mocks behind a `WINEEXPLORER_MOCK=1` env var for screenshot work.

---

## B. Correctness / robustness

- **B1. Shell hook never delivered.** `RegisterShellHookWindow(g_taskband.hwnd)` (`taskband.c:655`) but the
  `SHELLHOOK` message is only handled in the *tray* window proc (`systray.c` default case). The taskband proc
  ignores it, so `taskband_handle_shell_hook` is dead and everything works off the 200 ms `EnumWindows` poll.
  Either handle the message in `taskband_proc` or register the tray window. Poll cost is small but flashing
  (`HSHELL_FLASH`) and instant activation are lost.
- **B2. Classic start-menu banner shows over unrelated menus.** `WM_ENTERMENULOOP` on the tray window calls
  `startmenu_on_enter_menu_loop` (`systray.c`), which `FindWindowW("#32768")` and *shows* `g_banner_wnd` next to
  whatever menu opened (`startmenu_view.c:561-582`). After the classic Start menu has been opened once, the
  taskbar right-click menu gets the blue banner. Also `FindWindowW` is system-wide. **(verify)** Fix: only act
  when `g_startmenu_open`, and match the menu by the HWND returned from `GetMenuInfo`/`MN_GETHMENU`, or draw the
  banner by owner-drawing the menu instead of a separate topmost window.
- **B3. Blocking UI thread with child processes.** `volumetray`/`nettray` poll every 500 ms; each audio poll
  spawns `/bin/sh -c wpctl ...` and `WaitForSingleObject`s up to 1 s on the shell thread; net scan waits up to
  3 s and reads only after the child exits (output > pipe buffer deadlocks until timeout). The Start menu,
  taskbar and tray all share that thread. Wine already ships `mmdevapi` with `IAudioEndpointVolume` (PulseAudio /
  PipeWire-pulse) - use that for volume/mute in `audio_backend`, `sndvol32`, `mmsys.cpl` (the same wpctl/pactl/
  amixer block is copy-pasted three times: `audio_backend.c`, `sndvol32.c:104-176`, `mmsys.c:107-166`). For
  Wi-Fi use D-Bus from a worker thread; poll `/proc`/`/sys` only.
- **B4. Work area is never reserved.** Nothing calls `SPI_SETWORKAREA` for the taskbar (grep: only a read in
  `initialize_systray`), so maximized windows cover the taskbar **(verify)**. Windows XP reserves it; `appbar.c`
  has the machinery.
- **B5. Taskbar settings mostly cosmetic.** `autohide`, `always_on_top`, `hide_inactive_icons`, `show_clock`,
  `group_buttons` are stored in the struct and shown in the dialog but nothing consumes them (grep across
  explorer). Only `lock_taskbar`, `group_buttons`, `Start_Style` persist (`taskbar_props.c`).
  `Start_Style` is also written to the *real* Windows value `Start_ShowRun` (a 0/1 "show Run" flag) and read
  back from it, which will mis-set the style if a stock registry/other program touches it.
- **B6. Desktop view is a re-implementation, not a shell view.** `SHELLDLL_DefView` is spoofed by a hand-drawn
  icon grid (`desktop_view.c`). Missing vs. roadmap claims: position persistence (no `RegSetValue` anywhere in
  the file), change notifications (no `SHChangeNotify`), rename (F2), drag-drop, Fit/Fill wallpaper (only
  Center/Tile/Stretch are drawn), non-BMP wallpapers (`LoadImageW` is BMP-only; XP's default is Bliss.jpg).
  Consider hosting a real `IShellView` (shell32 `SHCreateShellFolderView`) and styling it, which gets
  drag-drop, rename, notifications, properties and context menus from Wine's shell32 for free.
- **B7. Shell32 icon indices are guessed.** Start menu / desktop use hard-coded `ExtractIconEx("shell32.dll", N)`
  indices (235, 271, 254, 227, ...) that come from the XP numbering. Wine's shell32 resource order differs
  **(verify visually)**, and `ExtractIconEx` per item per paint is slow (see B9). Pick icons by resource ID or
  ship the XP icon set the project already extracted and load by our own IDs.
- **B8. Power actions.** `ExitWindowsEx(EWX_SHUTDOWN|EWX_FORCE)` then `systemctl poweroff` then `ExitProcess`
  (`power_mgmt.c:70`): `EWX_FORCE` kills apps without WM_QUERYENDSESSION, and `systemctl` is launched
  asynchronously before Wine has finished. Run the Wine shutdown first (non-forced, with the XP "End Program"
  behavior), wait for wineserver, then power off. Log Off is just `ExitProcess`; make sure the session script
  tears down wineserver cleanly.
- **B9. Paint paths do real work.** Two-column Start menu `WM_PAINT` creates 3 fonts, loads the avatar bitmap
  and calls `ExtractIconEx` for 8+ icons *every repaint* (including each hover change); header gradient is 56
  `ExtTextOut` calls. Cache fonts/icons at show time. Tray clock repaints every second although it displays
  hh:mm (sync to the minute boundary).
- **B10. Hit-testing duplicates layout constants.** `two_col_hit_test` (`startmenu_view.c:862`) re-states pixel
  rows (62/104/150/36/398/444...) separately from `WM_PAINT`. Any tweak to match XP will desync them. Derive
  both from one layout table.
- **B11. Show-desktop toggle** restores every iconic window, including ones the user had minimized earlier;
  caps at 256 windows; `ShowWindow(SW_MINIMIZE)` blocks on hung apps (use `SW_FORCEMINIMIZE` or post).
- **B12. No keyboard navigation** in the two-column Start menu (only Esc) and no Win-key handling; accessibility
  and XP parity gap.
- **B13. timezone.c details.** `malloc` results unchecked (`:132,176`); `wcsncpy(..., size-1)` into un-zeroed
  `malloc` memory can leave unterminated strings (`:146,181-184`); registry `Std`/`Dlt` values not guaranteed
  terminated; fallback table hard-codes DST rules (`DaylightDate` Mar/5th, `StandardDate` Oct/5th) which are
  wrong for the southern hemisphere and many northern zones; `g_tz_iana_map` covers only 53 zones so selecting
  any other registry zone updates Wine but silently does nothing on the host; `get_active_timezone_key`
  prefers an HKCU value over the real host zone (stale after an external `timedatectl` change) and uses
  `popen()` on the UI thread. `map_subclass_proc` still uses `SetWindowLongPtr` + a global `g_orig_map_proc`
  (the 64-bit fix in the last commit was applied to the DTP only); `draw_world_map` ignores `selected_idx`, so
  the "meridian indicator" listed in ROADMAP Phase 8 is not drawn.
- **B14. dateandtime.c.** `date -s` interprets the typed time in the *host's* zone, but the page shows Wine's
  zone - they can differ after a zone change in the same session. `SetTimer(hwnd,1,1000-g_selected_time.wMilliseconds)`
  re-arms from a stale struct once `g_time_modified` is set (harmless today because the timer is killed).
- **B15. Single-instance statics.** Both applets keep global page state (`g_selected_time`,
  `g_selected_tz_idx`, `g_orig_map_proc`); fine for modal sheets, breaks if the CPL is ever opened twice.
- **B16. `parse_command_line` + `ExitProcess(0)`** inside the parser skips normal cleanup for `/wlan`,
  `/netstatus`, `/netprops`.

## C. Changes to stock Wine that affect everyone (decide, don't just keep)

- **C1. `comctl32/monthcal.c`** removes the "today circle" and `MCS_NOTODAYCIRCLE` handling *globally* and shifts
  the Today label, to make one dialog look right. Breaks upstream tests and every app. Make it opt-in via a style
  or a private flag the applet sets, or draw the calendar the XP way when the visual style is active.
- **C2. `include/commctrl.h`, `include/prsht.h`** redefine `SNDMSG` as `SendMessageW` under `__WINESRC__`
  (so macro helpers like `PropSheet_Changed` compile). That silently changes semantics of every ListView/TreeView
  macro in the whole Wine tree. Call `SendMessageW` directly in the new code instead of touching public headers.
- **C3. `shell32/dialogs.c`** Run dialog is now force-positioned bottom-left (XP does that when launched from
  Start, not from `Win+R`/other callers). Restrict to the Start-menu caller.
- **C4. `desktop.c`** removes `PostQuitMessage` on `WM_CLOSE` and drops `initialize_launchers` (upstream feature).
- **C5. `explorer.c`** removes `#include <initguid.h>` and the PIDL error path (silently continues).
- **C6. License headers.** `desk.cpl/desk_private.h` and `resource.h` lost the upstream LGPL/CodeWeavers header,
  and new files have no license notice. Wine is LGPL-2.1+; keep the headers on modified files and add an LGPL
  notice to new ones. `programs/calc` (ReactOS, GPL-2 text in `copying.txt`) and `mspaint` are separate
  executables - fine to ship, but record their licenses in a `docs/LICENSES.md`.
  *Resolved 2026-10-11:* the project is GPL-2.0-or-later as a whole; `LICENSE.md` records the licenses of each part.
- **C7. `programs/wmplayer/main.c`** (Wine's stub wmplayer) and `dlls/desk.cpl/desk.svg` were deleted from the
  baseline; only those two upstream files are missing vs `archives/wine-master.zip`.

## D. Design / fidelity gaps vs. the stated goal (XP Classic + Luna)

- **D1. Luna is not implemented.** `OpenThemeData`/`DrawThemeBackground` appear nowhere. When "visual styles
  active" the taskbar/start/task buttons are still drawn with `DrawEdge`; only widths and the two-column menu
  change. `Luna.theme` points at `%ResourceDir%\Themes\Luna\Luna.msstyles`, which is not in the repo. Need a
  decision: (a) author a msstyles-compatible Luna from scratch / from ReactOS-style assets, (b) extract the
  Luna bitmaps from the XP VM and render them directly, or (c) fixed bitmap skin in explorer. This blocks the
  main visual target; everything drawn with GDI in `startmenu_view.c`, `taskband.c`, `systray.c` will be
  redone.
- **D2. Roadmap says "done" for things that are not:** tray "calendar popup" (only a double-click to the CPL;
  XP shows a date tooltip), taskbar button grouping (not in `taskband.c`), desktop drag-drop / persistence /
  Fit / Fill (B6), taskbar auto-hide (B5), meridian indicator (B13). Treat the roadmap checkboxes as intent.
- **D3. Start menu content is hard-coded.** Left-column "frequent programs" is a constant list, no MFU
  tracking, "My Recent Documents" opens the folder rather than a flyout, "All Programs" is a classic popup
  rather than the in-place XP list, no Connect To / Printers flyouts.
- **D4. timedate.cpl vs XP:** no Internet Time tab (XP Pro has three), the Time page has no up/down spinner on
  the time box (`DTS_UPDOWN` missing) and an extra "Current time is displayed above." label that XP does not
  have, analog clock face/hands differ from XP's (dots and flat hands vs XP's shaded face), no "Current time
  zone: X Standard Time" status line, no DST notice. UAC dialog uses the Vista-era layout with "Segoe UI",
  which is not present under Wine (falls back).
- **D5. Two Settings tabs in desk.cpl.** The new `IDD_SETTINGS` page was added in front of Wine's original
  `IDD_DESKTOP` page, which is still in `pages[]` with its own resolution list and `ChangeDisplaySettings`
  apply **(verify)**. Remove or merge to avoid double-apply and a duplicate tab.
  `main.cpl`, `mmsys.cpl`, `powercfg.cpl`, `ncpa.cpl`, `sndvol32`, `netshell`, `timedate.cpl`...). You described
  them as needed reference; the concern is redistribution if this repo is ever published or shared. Decide the
  policy (private repo only / keep a `scripts/extract-assets.py` that pulls from the local `references/` and
  gitignore the output / redraw). Not a code defect; flagging because it is hard to undo once pushed.
- **D7. Session scripts.** `setup-default-session.sh` hard-codes the user `wineuser` and sets
  `WaylandEnable=false` (X11), while the project goal says GDM + Wayland + gamescope; `wine-explorer-session.sh`
  writes a fixed world-readable `/tmp/wine-session.log`, uses `exec gamescope` with no nested-session flags
  (`-e`, `--expose-wayland`), and passes `/desktop=Default` (see A5).

## E. Repo hygiene

- `wine/.gitattributes` makes every git command print `[attr]generated ... not allowed` (macro definition is
  only legal in a top-level `.gitattributes`). Move/rename it or delete the `[attr]` line.
- `check_res.py` (repo root) is a one-off debug script with a hard-coded `/home/wineuser/build/wine64/...` path;
  delete it or move it to `scripts/`.
- `.gitignore` has `*.exe`, `*.dll`, `*.res`, `*.patch`, `bin/`, `obj/`, `build/`, `*.a`, `*.o`. That already
  swallowed upstream's `dlls/ntoskrnl.exe/` once (re-added later with a `!` rule). Today the baseline matches
  the upstream zip (only 2 files missing), but prefer anchoring these to build output directories.
- Commits are very large mixed bundles (e.g. "Fix start menu and tray" touches timedate, comctl32, explorer,
  control and binary assets). Smaller commits per component will make bisecting on the VM workable.
- There are no tests. Wine's `tests/` convention is the right place for the pure logic (timezone matching,
  menu hit-testing tables, `parse_command_line`).

---

## Suggested order of work
1. A1, A2, A3 (security), A4 (other apps' icons), A5/A6 (behavior of stock features).
2. A7/A8 + B13/B14 to finish timedate.cpl cleanly (see `HANDOFF.md`).
3. Decide D1 (Luna approach) before more GDI drawing is written.
4. B3 (audio via mmdevapi, background worker for network), B2, B4, B1.
5. Revisit C-items so the tree stays rebase-able on upstream Wine.
