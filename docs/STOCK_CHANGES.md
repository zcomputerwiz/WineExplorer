# Changes to stock Wine / third-party code

Anything under `wine/` that is not new project code, and what we changed. Keep this list short: every entry makes
rebasing on upstream Wine harder. Baseline = commit `03476d7`.

## In place of stock behavior (deliberate)
| Area | Change | Gate |
|---|---|---|
| `programs/explorer/desktop.c`, `systray.c`, `explorer.c` | Windows-style shell (taskbar, Start menu, desktop view, tray applets) | only when the desktop has `EnableShell=1` (`HKCU\Software\Wine\Explorer\Desktops\<name>`) or is named `shell`; stock launchers/`PaintDesktop`/`WM_CLOSE` otherwise |
| `programs/explorer/explorer.c` | `/wlan`, `/netstatus`, `/netprops` helper switches; `::{CLSID}` paths and `/e` accepted | token-based, only when given as arguments |
| `dlls/desk.cpl` | theme/background/appearance/settings pages added in front of Wine's Settings page | always |
| `programs/wmplayer` | Wine's stub `main.c` replaced by ReactOS mplay32 | always |
| `programs/calc`, `programs/mspaint` | new programs (ReactOS calc; own mspaint) | always |
| `dlls/ntdll/unix/system.c` | `SystemBatteryState` / `SystemPowerCapabilities` read `/sys/class/power_supply` | always (replaces a FIXME stub) |
| `loader/wine.inf.in` | file-type associations (.txt/.bat/.cmd/.log, folder open command) | always |
| `programs/control/control.c` | `printers` opens the Printers folder | always |
| `programs/shell32`-visible: none | Run dialog placement is done by explorer (CBT hook), not by shell32 | - |

## Reverted (were changed earlier, now stock again)
- `dlls/comctl32/monthcal.c` (today-circle removal): timedate.cpl now creates its calendar with
  `MCS_NOTODAY | MCS_NOTODAYCIRCLE`, which is what the XP applet looks like.
- `include/commctrl.h`, `include/prsht.h` (`SNDMSG` redefinition).
- `dlls/ntdll/unix/sync.c` (`NtSetSystemTime` running `sudo date`): clock changes go through the applet's elevation
  prompt only.
- `dlls/shell32/dialogs.c` (Run dialog placement).

## License headers
New project files carry the Wine-style LGPL-2.1+ header (`scripts/add-license.py` adds it to anything missing it).
Modified Wine files keep their original header. ReactOS-derived programs keep theirs:
- `programs/calc` - ReactOS Calc, LGPL-2.1+ (header in each file); `copying.txt` is the GPL text shipped with it.
- `programs/wmplayer` - ReactOS mplay32 (ReactOS license: GPL-2.0-or-later).
- `dlls/ntoskrnl.exe` - stock Wine.
