# Current handoff (2026-10-09)

**Active desktop work is back in `D:\DeepThought\Projects\WineExplorer`.** The user has superseded the full Proton desktop migration: customized Wine 11.19 remains the desktop runtime and window manager. Proton runs applications independently, with its normal runtimes and prefixes, as it would under another Linux desktop. The ProtonExplorer copy/VM remains a separate compatibility lab; its migration plan is historical, not the current desktop implementation plan.

Read [LINUX_WINDOWS.md](LINUX_WINDOWS.md) for the new implementation. Claude's `72a1faf`, `4f87754` and `4634367` added the X11 window manager, themed Win32 hosts for external windows, client-side title-bar move/resize, shell keys and XP Alt+Tab across threads. Review also moved the WM running-state check inside the X connection lock, preventing a late request from using a connection after shutdown.

Claude left an untracked `wine/dlls/wexhost/apps.c`. It is now integrated into the Unix call table, PE exports and both Start menu styles. Programs / All Programs includes a sorted **Linux Applications** submenu, discovered through GIO and refreshed on opening. Launching by desktop ID strips the shell's `WINE*` variables from directly spawned clients. GIO handles desktop-entry field codes, visibility rules and terminal/D-Bus launches. Categories and per-application menu icons are the next presentation work; the current list is flat. See [START_MENU.md](START_MENU.md) and [LINUX_WINDOWS.md](LINUX_WINDOWS.md).

## Runtime and build

- **WineExplorer-VM**, SSH `2222`, user `wineuser`, key `D:/VirtualBox_VMs/vm_key`.
- Guest source `~/src/wine`, build `~/build/wine64`, installed `/usr/local`; source is copied, not a guest Git checkout.
- Visible desktop: GDM → Xorg `:0` → installed Wine Explorer `/desktop=Default,800x600 /shell`, prefix `~/.wine`. Xauthority `/run/user/1000/gdm/Xauthority`. The current Luna/Bliss session has My Computer and xterm open. The user authorized using and restarting this real session for verification.
- Gamescope is not installed. There is no compositor pass-through service. `wexhost` supplies system D-Bus APIs and XRandR display control. The session script now starts directly on X11, matching Claude's WM design; Gamescope remains optional per application. A future service/compositor bridge needs separate design and validation.
- Targeted build: `make -j8 dlls/wexhost/all programs/explorer/all` (directory names alone do not build them).
- Launcher verification used Xvfb `:6` and `~/launcher-check/prefix`, not the visible desktop. `scripts/linux-apps-probe.c` provides enumeration and launch checks. Verified visibility filtering, oversized metadata serialization, Unicode/ampersands, clean launch environment, missing-ID errors, dynamic install/remove changes, and both menu styles. Screenshots/logs are under guest `~/launcher-check`; host screenshots under ignored `build/launcher-*.png`.
- Changes are built, installed and running in the visible session. The user authorized real-session testing; GDM was restarted to load the new PE/Unix libraries. My Computer and xterm were restored. Verified the themed menu and a native NetworkManager editor launch, clean launch environment, repeated client-area click activation with typed input, cross-runtime Alt+Tab, and a maximized Linux window respecting the work area with the taskbar visible. Temporary focus tracing was removed.

## Native focus follow-up

Real-session testing found that clicking a Linux window's client area did not reliably activate it after a separate Wine process had focus, even though its title bar worked. The bridge now joins the input queues in the direction that preserves Explorer's queue when detaching; Wine recreates the first thread's input queue on detach and otherwise loses its active window and input time. Every foreign client click requests activation, rather than trusting cached `_NET_ACTIVE_WINDOW` state. FocusIn forwarding checks the current X focus so queued old events do not reclaim it.

`WexWmFocus` synchronizes the session's X keyboard focus with selected Win32 windows, forwarding to the foreign child for native hosts. A foreground WinEvent hook keeps both focus and native focused-state properties current. Alt+Tab completes outside the low-level hook and preserves the shell input queue. Repeat tests checked actual X keyboard focus and typed commands, not just which window appeared on top. No additional stock X11-driver changes were retained from the investigation.

## Next work

1. Linux application menu icons/categories and pinned/MFU integration.
2. Linux tray icons (StatusNotifierItem) and notifications; see [DBUS_PLAN.md](DBUS_PLAN.md).
3. Exercise a real Steam/Proton application with its independent prefix. Native Linux/X11 window hosting is verified; no Proton application was launched in this continuation.
4. Multi-monitor work areas/struts, foreign-window grouping and shell-crash recovery remain open. Native Wayland management and a session-wide Gamescope bridge are not implemented.

**WebView2 update:** Claude's `66c09b0` recorded a positive rendering result on the separate ProtonExplorer VM using baseline Wine 11.19, Proton's per-app Windows 7 override for `msedgewebview2.exe`, and SwiftShader. See [WEBVIEW2.md](WEBVIEW2.md). This does not require migrating the desktop to Proton and is not yet a shell feature.

The Date/Time notes below are historical context and may describe code since replaced; use the current implementation and topical docs first.

# Historical handoff (2026-10-07)

**Last area of work: `timedate.cpl` (Date and Time Properties)**, plus Start menu / tray polish in the same
commits (`aa0c06c`, `504e692`, `d0809c3`). It now runs through the new D-Bus stack (see below). Full review: `docs/CODE_REVIEW.md`.

## What exists

`wine/dlls/timedate.cpl/` (~1.9k lines):

| File | Role |
|---|---|
| `timedate.c` | `CPlApplet` entry, builds the 2-page property sheet, activates manifest (res 124) |
| `dateandtime.c` | Date & Time page: `SysMonthCal32`, `SysDateTimePick32` (time), analog clock, `PSN_APPLY` -> `date -s` through UAC |
| `clock.c` | `TimeDateClock` owner-drawn analog clock; messages `CLM_STOPCLOCK/STARTCLOCK/SETTIME` |
| `timezone.c` | Time Zone page: reads `HKLM\...\Time Zones` (53-entry fallback table), world map bitmap, DST checkbox, applies via `SetTimeZoneInformation`, registry, and `timedatectl set-timezone` through UAC |
| `uac.c` | Vista-style consent/credential dialog; runs `sudo`/`su` via `/bin/sh` and temp files (see review A2) |
| `world.bmp` | Map (ReactOS world map, updated in `aa0c06c`) |

Related changes made for it elsewhere:
- `wine/dlls/comctl32/monthcal.c`: today-circle removed globally (review C1).
- `wine/dlls/ntdll/unix/sync.c`: `NtSetSystemTime` falls back to `sudo -n date -s` (review A3).
- `scripts/disable_timesync.py`: patches the VirtualBox Guest Additions init script in the Debian VM
  (`/opt/VBoxGuestAdditions-7.2.20/init/vboxadd-service`) to add `--disable-timesync`, so a manually set guest
  clock sticks. VM-only; re-run after Guest Additions upgrades.
- `wine/programs/explorer/tray_clock.c`: double-click launches `control.exe timedate.cpl`.
- `wine/programs/control/control.c`: `PRINTERS` etc. routed to the Printers folder (unrelated).
- Registry used: `HKCU\Software\WineExplorer\Time\SelectedTimeZone` (persisted zone key; takes priority over
  the host zone when the page opens), `HKLM\System\CurrentControlSet\Control\TimeZoneInformation\TimeZoneKeyName`.

## Behavior verified as working (from commit messages; re-check on the VM)
- Time editing with the clock freezing while editing and resuming on blur; date selection tracking;
  `PSN_APPLY` persists across the VM's time-sync service (once `disable_timesync.py` has been run).
- Time zone list sorted by bias, selection persisted, host zone synced via `timedatectl`.
- 64-bit-safe subclassing of the DTP (`SetWindowSubclass`).

## Current state of timedate.cpl (after the D-Bus refactor)
- Elevation is no longer done by the applet. `Apply` calls `wexhost` (`WexTimedateSetTime` / `SetTimezone`), which
  calls systemd `timedate1` over the system bus. polkit then asks the session's authentication agent, which is
  **explorer** (`programs/explorer/uac_agent.c`, registered at shell start); it shows the UAC-style dialog.
  Verified in the VM: the agent registers (`polkitd: Registered Authentication Agent for unix-session:...`),
  the dialog appears, dismissing it cancels, a correct password authorizes (PAM through `polkit-agent-helper-1`).
- Time is set as a *relative* wall-clock delta (chosen - now), so Wine/host zone differences cannot shift it.
- Zone selection prefers the host zone (`timedate1` property) over saved registry values; zone change goes to the
  host first and only then to Wine's registry; unmapped zones update Wine only.
- The calendar is created with `MCS_NOTODAY | MCS_NOTODAYCIRCLE` (matches XP; comctl32 is stock again).
- `uac.c` and the UAC dialog resource left the applet; the dialog now lives in explorer.

## Time zone / audio / shell hook (done after the first handoff)
- Wine follows the host zone (libc); `SetTimeZoneInformation` is a no-op in Wine, so the applet only changes the host
  zone (`timedate1`) and no longer touches Wine's registry. The "DST checkbox" is informational. The stale-DST symptom
  was a wineserver bug (bias only recomputed on year change): fixed in `server/fd.c`; ntdll's cache now follows
  `/etc/localtime`. Verified live: changing the host zone updates the tray clock within seconds.
- `IAudioEndpointVolume` really works now (set from a Wine process, `wpctl` shows the change). explorer's
  `audio_backend.c` uses it again (no process spawns); `hostcmd.c` remains for nmcli.
- `RegisterShellHookWindow` works; taskband gets created/destroyed/activated/redraw/flash events (the 2 s poll is
  only a safety net). One hook window per desktop (it is stored as the taskman window).
- VM gotcha: `pkill wineserver` leaves orphan Wine processes that block new clients; use `/tmp/cleanwine.sh`-style
  cleanup (`wineserver -k`, kill `windows.system32` processes) before restarting the session.

## Known defects / unfinished (timedate)
1. XP fidelity (compare with the XP VM, Control Panel -> Date and Time): add the **Internet Time** tab (use
   `timedate1.SetNTP`, `NTPSynchronized`); the XP page uses month combo + year spinner above the grid and a
   "Current time zone: ..." line; the time box needs a proper spinner look; remove "Current time is displayed above."
   The analog clock face/hands differ from XP. World map: draw the selected zone's band (`draw_world_map` ignores
   `selected_idx`).
2. After a manual `SetTimeZoneInformation` the prefix can end up with a zone lacking DST rules (the VM showed
   Wine at UTC-6 while the host was on CDT). Prefer the registry's full `TZI` data and re-derive from the host zone
   at page open; generate `g_tz_iana_map` for every registry zone from CLDR `windowsZones.xml`.
3. `timezone.c`: the fallback table's DST rules are wrong for many zones; `g_orig_map_proc` subclass should use
   `SetWindowSubclass`.
4. The UAC dialog still has Vista wording/layout; replace with Longhorn-beta art when resources are chosen. No
   "verified publisher"-style fields are shown any more (they were invented).
5. polkit agent: `unix-group` identities are expanded from `/etc/group` only (no LDAP/NSS users); only one prompt
   at a time (a second request is cancelled).

## Everything else the next person needs to know

- Roadmap (`docs/ROADMAP.md`) says Phases 1-11 are complete; several boxes are overstated (review D2). Trust the
  code and the review, not the checkboxes.
- Highest-priority review items outside timedate: Wi-Fi command injection (A1), taskband `DestroyIcon` on
  foreign icons (A4), virtual-desktop default-shell change (A5), `explorer.c` substring switch matching (A6).
- Luna rendering does not exist yet (review D1); decide the approach before writing more shell GDI code.

## Working environment (known facts)

- Windows host: repo at `D:\DeepThought\Projects\WineExplorer`; `wine/` is a copy of upstream Wine master with
  our changes in-tree (not a submodule). `references/` is gitignored (ReactOS, Wine, Wayland, Open-Shell,
- Debian VM (VirtualBox) with GDM autologin as user `wineuser`, Guest Additions 7.2.20; Wine built in
  `/home/wineuser/build/wine64` (seen in `check_res.py`); installed under `/usr/local/bin/wine`; session script
  `/usr/local/bin/wine-explorer-session.sh`, desktop entry `scripts/wine-explorer.desktop`.
- Workflow stated by the owner: build on both the host and the VM; binaries are `scp`/`ssh`'d to the VM; VMs are
  driven with `VBoxManage` (`C:\Program Files\Oracle\VirtualBox\VBoxManage.exe`, not on the bash PATH). A
  Windows XP Pro VM is the visual and behavioral reference.
- **Now known:** VMs `WineExplorer-VM` and `Windows XP Pro` (`VBoxManage controlvm <name> screenshotpng`); SSH `wineuser@127.0.0.1:2222` with key `D:\VirtualBox_VMsm_key` (NAT forward); source on the VM in `~/src/wine`, build in `~/build/wine64`; everything is wrapped by `scripts/vm.sh` (sync/build/install/shot/ssh). Gotchas: the VM clock was far in the future once (stale `.res` files stopped rebuilding; sync uses `tar -m` and `find -newermt` + `touch` fixes it); the prefix keeps copies of builtin modules in `system32` (`vm.sh install` refreshes them); `pkill -f control.exe` kills your own ssh command.
- **Previously unknown (resolved above):** VM names, SSH host/port/key, exact `configure` and `make`
  invocations, which `make` target to rebuild just one CPL/program, how screenshots are taken
  (`VBoxManage controlvm <vm> screenshotpng`?), where the XP reference screenshots live inside `references/`.
  Fill these into a `docs/WORKFLOW.md` or `CLAUDE.md` after the first successful build/deploy cycle.

## Targets
First visual target: **XP Classic and Luna** themes matching the XP VM pixel-for-pixel where feasible. Longhorn
beta is a later source of UAC and possibly extra resources.
