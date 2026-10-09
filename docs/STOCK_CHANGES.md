# Changes to stock Wine / third-party code

Anything under `wine/` that is not new project code, and what we changed. Keep this list short: every entry makes
rebasing on upstream Wine harder. Baseline = commit `03476d7`.

## In place of stock behavior (deliberate)
| Area | Change | Gate |
|---|---|---|
| `programs/explorer/desktop.c`, `systray.c`, `explorer.c` | Windows-style shell (taskbar, Start menu, desktop view, tray applets) | only when the desktop has `EnableShell=1` (`HKCU\Software\Wine\Explorer\Desktops\<name>`) or is named `shell`; stock launchers/`PaintDesktop`/`WM_CLOSE` otherwise |
| `programs/explorer/desktop.c` | `/shell` switch (before the program to run) enables the shell for that desktop; used by the Winlogon-style session script | only when given |
| `programs/explorer/explorer.c` | `/wlan`, `/netstatus`, `/netprops` helper switches; `::{CLSID}` paths and `/e` accepted | token-based, only when given as arguments |
| `dlls/desk.cpl` | theme/background/appearance/settings pages added in front of Wine's Settings page; installed theme discovery, isolated style previews, theme-engine Apply, host-supported modes and keep/revert confirmation (see DISPLAY.md) | always |
| `dlls/uxtheme` (`system.c`, `msstyles.c`, `metric.c`) | implement private ordinal 16 `OpenThemeDataFromFile`; metrics follow the supplied theme handle; activating a preview applies metrics even when its classes are already parsed | always |
| `dlls/winex11.drv/window.c` | virtual desktop resize does not wait for fullscreen hint acknowledgements; standalone shell sessions have no host window manager to supply them | virtual desktop root window only |
| `programs/wmplayer` | Wine's stub `main.c` replaced by ReactOS mplay32 | always |
| `programs/calc`, `programs/mspaint` | new programs (ReactOS calc; own mspaint) | always |
| `dlls/ntdll/unix/system.c` | `SystemBatteryState` / `SystemPowerCapabilities` read `/sys/class/power_supply` | always (replaces a FIXME stub) |
| `server/fd.c` | recompute the DST-adjusted timezone bias when DST starts/ends (a long-running session never saw it before) | always (bug fix, upstreamable) |
| `dlls/ntdll/unix/system.c` | drop the cached zone info when `/etc/localtime` changes | always (bug fix) |
| `dlls/win32u` (`window.c`, `input.c`, `defwnd.c`), `dlls/user32/misc.c` | `RegisterShellHookWindow`/`DeregisterShellHookWindow` implemented: the desktop's taskman window gets the registered `SHELLHOOK` message for window created/destroyed/activated/redraw/flash | always (no cost without a registered window except one server call per event) |
| `dlls/mmdevapi` (`audiovolume.c`, `unixlib.h`), `dlls/winepulse.drv` | `IAudioEndpointVolume`: master volume and mute of the default device through the driver (PulseAudio/PipeWire); two new unix calls `get/set_endpoint_volume`; other drivers (alsa, oss, coreaudio) return E_NOTIMPL; the wow64 table of winepulse has stubs, so 32-bit callers get E_NOTIMPL for now | always |
| `dlls/winex11.drv/window.c` (`sync_window_position`), `event.c` (`set_input_focus`) | in a virtual desktop, a window that is not the top one is restacked in X directly below the window it follows in the Win32 z-order (stock only ever raises, for the sake of window managers), and focusing a window no longer raises it in X; without it an activated window covers topmost ones such as the taskbar | only in a virtual desktop, unmanaged windows |
| `dlls/winex11.drv/event.c` (`X11DRV_ActivateWindow`) | in a virtual desktop the window that becomes active is raised to the top of the Win32 z-order, as Windows does on activation (Wine leaves it to the X window manager, and the row above removed the X raise); the X stacking follows, below topmost windows. Before, clicking a window or its taskbar button activated it without bringing it to the front | only in a virtual desktop, unmanaged windows |
| `dlls/win32u/sysparams.c` (`SPI_SETWORKAREA`/`SPI_GETWORKAREA`) | the work area is the monitor's and every process sees it, like on Windows and ReactOS (stock kept it per process, so windows maximized in other processes covered the taskbar): it is written to the monitor's registry key, the monitor serial is bumped so other processes reload, and a volatile copy in `HKLM\Software\Wine\WorkArea` (by monitor path) is applied again whenever the monitors are rebuilt from the driver, which every process entering a virtual desktop does; it is dropped once it no longer fits the monitor. `SPI_GETWORKAREA` reads the primary monitor every time. As on Windows, the rectangle has to lie on one monitor, and a non-zero uiParam (the taskbar passes TRUE) fits the maximized windows of that monitor to the new work area | always (only matters once something sets a work area) |
| `dlls/shell32` (`shell32.rc`, `shresdef.h`, `shell32.rgs`) | Windows XP names in the namespace: string 8964 "Recycle Bin" (was "Trash", with the delete prompts worded like XP), new string 9227 "My Documents" as the `LocalizedString` of CLSID_MyDocuments (string 46 stays the folder name "Documents") | always |
| `dlls/shell32/iconcache.c`, `shellord.c` (`FileIconInit`) | shell32's own icons can be replaced through `HKLM\...\Explorer\Shell Icons` like on Windows and ReactOS (stock Wine only did it for overlays): value `n` replaces icon index n, an icon asked for by resource id stands for index id - 1 for the first 54 icons (no gaps there), and beyond that a value named after the negative id (`-137`) is used, which Windows has no use for. A replacement that cannot be loaded falls back to shell32's icon. `FileIconInit(TRUE)` (was a stub) reloads the cached shell32 icons in place so a theme change shows without new image indexes. Explorer writes the values from the theme's icon pack | always (only matters when Shell Icons has values) |
| `server/winstation.c` (`remove_desktop_user`), `include/ntuser.h` | new Wine desktop flag `DF_WINE_SHELL_DESKTOP`: a desktop with it is not timed out when only its owner is left (stock wineserver unlinks the desktop's name and posts `WM_CLOSE` then; with a shell that happened whenever the shell was alone and one of its threads ended, and new programs could no longer open the desktop). Explorer sets it in shell mode | only on desktops that have the flag |
| `dlls/shell32/folders.c` (`IExtractIconW::GetIconLocation`) | a shortcut without an icon of its own shows the icon of its target, as on Windows and ReactOS (stock Wine showed the blank file icon, e.g. for the Start menu shortcuts in search results) | always (bug fix, upstreamable) |
| `dlls/comctl32/imagelist.c` (`add_dib_bits`) | an image without alpha clears the item's alpha flag; before, replacing a 32-bit image with one without alpha (`ImageList_ReplaceIcon`) kept the flag and the new image drew fully transparent | always (bug fix, upstreamable) |
| `dlls/shell32/shellord.c` (`SHFindFiles`) | opens the Search Results folder (`explorer.exe ::{E17D4FC0-...}`, whose window shows the Search Companion) like Windows and ReactOS, honoring `REST_NOFIND`; was a stub returning FALSE. The folder to look in and saved searches are not passed yet | always |
| `programs/iexplore/iexplore.inf` | CLSID_Internet gets its display name "Internet Explorer" (Windows' default value); Wine had none | always |
| `loader/wine.inf.in` | file-type associations (.txt/.bat/.cmd/.log, folder open command); the Windows icons of text, configuration, setup and batch files (shell32 -152, -151, -153) | always |
| `programs/control/control.c` | `printers` opens the Printers folder; no argument opens the Control Panel folder in Explorer like Windows (Wine's own Control Panel window only if that fails) | always |
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

## Themed non-client frame (visual phase)
- `include/ntuser.h`, `include/winuser.h`: new `DrawNonClientFrame` user32 callback, `struct draw_non_client_frame_params`,
  `user_api_hook.pNonClientFrameDraw` and `NCFRAME_*` flags.
- `dlls/win32u/defwnd.c`: `nc_paint` and the caption-button redraws ask the hook first; if it draws, only the rectangle is advanced.
- `dlls/user32/{user_main.c,hook.c,nonclient.c,user_private.h}`, `dlls/wow64win/user.c`: callback plumbing and default (declines).
- `dlls/uxtheme/{window.c,system.c,uxthemedll.h}`: `UXTHEME_NonClientFrameDraw` draws `Window` caption, frame and buttons from the active msstyles.

## Explorer window (file browser)
- `dlls/shell32/shell32.rc`: shell view column titles "File"/"Modified" are now "Name"/"Date Modified" (Windows wording).
- `programs/explorer/explorer.c`: window class is `CabinetWClass`; the window chrome moved to `browser_ui.c` (menu bar, Standard
  Buttons, Address bar with Go, activity logo, status bar) and `browser_taskpane.c` (File and Folder Tasks / Other Places /
  Details, drawn with the ExplorerBar class).  Toolbar and logo pictures come from ReactOS browseui (`res_browser/`).

## Menu accelerator underlines (Windows XP behavior)

* `dlls/user32/text.c`: `DrawText` honors `DT_HIDEPREFIX` (the `&` is removed, the letter is not underlined).
* `dlls/win32u/menu.c`: menus hide the accelerator underlines until a key is used while the menu is open (or
  `SPI_GETKEYBOARDCUES` is set), like Windows XP; menu items with a 32 pixel picture are exactly as high as the
  picture (the classic Start menu).
