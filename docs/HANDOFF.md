# Handoff - where the previous session stopped (2026-10-07)

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
