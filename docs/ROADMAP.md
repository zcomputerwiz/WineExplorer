# WineExplorer DE Roadmap

## Latest verification (2026-10-12)

The installer port is merged and its full Wine build, interactive installation,
zero-input installation and negative boot/disk safety cases passed on QEMU/KVM.
See [INSTALLER_TEST_REPORT.md](INSTALLER_TEST_REPORT.md) and
[CODEX_HANDOFF.md](CODEX_HANDOFF.md). W1-W3 and compositor D1-D2 are implemented;
installed-system LightDM integration (W7) now also passes live, multi-user and
zero-input installation tests. Remaining logon phases, presentation polish and
EFI/physical-machine verification are still open.

## Desktop direction (2026-10-09)

Customized Wine remains the desktop runtime; Explorer manages X11 windows from native Linux and independent Wine/Proton applications. Proton is not replacing the desktop's Wine. The X11 WM, foreign-window hosts, XP Alt+Tab and Linux desktop-entry Start menu launcher are implemented. Next: application menu icons/categories, Linux tray/notifications, and real Steam/Proton runtime verification. Gamescope is optional per application; a session-wide compositor/service bridge is future work. See [HANDOFF.md](HANDOFF.md) and [LINUX_WINDOWS.md](LINUX_WINDOWS.md). Older completed phases below describe their original scope, not verification of a current Gamescope session.

Recent verification completed: native client-area focus and keyboard input,
cross-runtime Alt+Tab, native dialog work-area constraints, folder task-button
icons/navigation refresh, Start menu highlight colors, Luna frame transparency,
and tray alignment with icon hiding off/on and collapsed/expanded chevrons.
These fixes are deployed to WineExplorer-VM. Current operational state, build
steps and remaining limits are recorded in [HANDOFF.md](HANDOFF.md).

## Phase 1: Environment & Repository Baseline (Completed)
- [x] Unpack all provided source archives into organized workspace.
- [x] Establish read-only isolated references directory for clean-room inspection (`references/`).
- [x] Initialize Git repository at `D:\DeepThought\Projects\WineExplorer\`.
- [x] Configure `.gitignore` and `.gitattributes` to safeguard reference trees and enforce clean LF endings.
- [x] Document high-level architecture, module decomposition, and reference guide.
- [x] Import upstream Wine baseline commit.

## Phase 2: Theme Engine & Classic/Themed Infrastructure (Completed)
- [x] Specify `.theme` INI parser architecture (`[VisualStyles]`, `[Control Panel\Colors]`, `[Control Panel\Desktop]`, `[CLSID]`, `[AppEvents]`).
- [x] Implement `theme_engine.c` / `theme_engine.h`:
  - [x] `.theme` file reader and system color updater (`SetSysColors`).
  - [x] Wallpaper path and style configuration (`SPI_SETDESKWALLPAPER`).
  - [x] UXTheme bridge (`OpenThemeData`, `DrawThemeBackground`, `SetSystemVisualStyle`).
  - [x] Authentic Server 2003 Classic 3D beveled rendering fallback.

## Phase 3: Desktop Surface & File Management Parity (Completed)
- [x] Implement `desktop_view.c` / `desktop_view.h`:
  - [x] Special namespace desktop items (My Computer, My Documents, Recycle Bin, My Network Places).
  - [x] Physical desktop folder enumeration (`CSIDL_DESKTOPDIRECTORY`, `CSIDL_COMMON_DESKTOPDIRECTORY`).
  - [x] 75x75 icon grid alignment, auto-arrange, and registry position persistence.
  - [x] Rubberband selection marquee (drag-to-select).
  - [x] Desktop right-click context menu (Arrange By, Refresh, New Folder/Shortcut, Properties).
  - [x] Wallpaper rendering engine (Center, Tile, Stretch, Fit, Fill).

## Phase 4: Taskbar & TaskBand (Window Management) (Completed)
- [x] Implement `taskband.c` / `taskband.h`:
  - [x] Window tracking via `RegisterShellHookWindow` / WinEvents.
  - [x] Running application buttons with 16x16 icon, title, active sunken state, and flashing state.
  - [x] Window activation, restore, and minimize-on-click logic.
  - [x] Window context menu (Restore, Move, Size, Minimize, Maximize, Close).
  - [x] Taskbar button grouping when space is constrained.

## Phase 5: Flexible Start Menu (Classic, 2-Column XP, Modern Search) (Completed)
- [x] Implement `startmenu_view.c` / `startmenu_view.h`:
  - [x] Classic Server 2003 / NT single-column cascading menu with side banner.
  - [x] Windows XP / 2003 two-column layout with user header, pinned programs, and shell folders.
  - [x] Open-Shell inspired modern layout with integrated real-time search box.
  - [x] Authentic Start button with Windows flag icon and theme-aware rendering.

## Phase 6: System Tray, Clock & Applets (Completed)
- [x] Implement `tray_clock.c` / `tray_clock.h`:
  - [x] Sunken notification tray area.
  - [x] Win32 `Shell_NotifyIcon` with balloon tooltips.
  - [x] Locale-formatted clock with interactive calendar popup.
  - [x] Show Desktop (minimize-all toggle) button.

## Phase 7: Power Management & Display Manager Session (Completed)
- [x] Implement `power_mgmt.c` / `power_mgmt.h`:
  - [x] Windows XP's "Turn off computer" (Stand By, Turn Off, Restart) and "Log Off Windows" (Switch User, Log Off)
        dialogs over the grayed screen (2026-10-10; the earlier dialog ignored its buttons).
  - [x] Log Off ends the login session through logind (TerminateSession); Switch User shows LightDM's custom Wine greeter.
        Tested on the VM: Log Off reaches the greeter, Restart reboots, Cancel returns.
  - [x] Clean Win32 process shutdown sequence.
  - [x] Linux system shutdown via systemd-logind D-Bus / `systemctl poweroff` / `systemctl reboot`.
- [x] Gamescope Display Manager session scripts (`wine-explorer-session.sh` & `wine-explorer.desktop`).

## Phase 8: Desktop Control Panels & Properties Parity (Completed)
- [x] Implement complete Display Properties (`desk.cpl`):
  - [x] Themes page (`IDD_THEMES`) with theme switcher, save/delete, and live preview.
  - [x] Desktop background page (`IDD_BACKGROUND`) with wallpaper picker, position modes, and Customize Desktop dialog (`IDD_CUSTDESKTOP`).
  - [x] Appearance page (`IDD_APPEARANCE`) with visual style selector, color scheme, font size, and Advanced Appearance dialog (`IDD_ADVAPPEARANCE`).
  - [x] Settings page (`IDD_SETTINGS`) with resolution slider, color depth combo, and multi-monitor setup.
  - [x] Monitor preview control (`DeskCplPreview`) rendering authentic classic desktop, active/inactive frames, and 3D bevels.
- [x] Implement complete Date and Time Properties (`timedate.cpl`):
  - [x] Date & Time page (`IDD_DATEANDTIME`) with Month Calendar and time spinners.
  - [x] Real-time analog clock control (`TimeDateClock`) with ticking hour, minute, and second hands.
  - [x] Time Zone page (`IDD_TIMEZONE`) with worldwide time zones and interactive world map meridian indicator.
  - [x] Integrate with tray clock double-click action.
- [x] Implement Taskbar and Start Menu Properties (`taskbar_props.c` / `taskbar_props.h`):
  - [x] Taskbar tab (`IDD_TASKBARPROP`) with lock, autohide, keep on top, grouping, quick launch, clock, and inactive icons options.
  - [x] Start Menu tab (`IDD_STARTMENUPROP`) with style selector (Classic Server 2003, 2-column XP, Modern search hybrid).
  - [x] Live previews for taskbar and start menu layouts.
  - [x] Authentic Taskbar context menu (Cascade, Tile H/V, Show Desktop, Task Manager, Lock Taskbar, Properties).
  - [x] Authentic Start button context menu (Open, Explore, Search, Properties).
- [x] Implement System Properties applet (`sysdm.cpl`):
  - [x] General page (`IDD_GENERAL`) with Windows emblem, OS version, registered owner/org, CPU specs, and RAM totals.
  - [x] Computer Name page (`IDD_COMPNAME`) with description, full computer name, and workgroup.
  - [x] Hardware page (`IDD_HARDWARE`) with Device Manager launcher and Hardware Profiles.
  - [x] Device Manager (`dlls/devmgr`, opened by `devmgmt.msc` through a minimal `programs/mmc`): the Linux kernel's devices
    (wexhost `WexHardwareList`: sysfs, udev database, ACPI companions, libkmod) shown as Windows XP would: devices by
    type/connection, resources by type, hidden devices (virtual adapters, volumes, Non-Plug and Play Drivers = unbound
    kernel modules); General/Driver/Details/Resources pages and Driver File Details. Update/Disable/Uninstall grayed.
    Open: Resources by connection lists like by type (bridge windows need root); Power Management tab; class
    properties; Show/Hide Console Tree and Action Pane do nothing.
  - [x] Advanced page (`IDD_ADVANCED`) with Performance Options, User Profiles, Startup & Recovery, and Environment Variables.
  - [x] Performance Options dialog (`IDD_PERFORMANCE`) with Visual Effects checklist (desktop icon drop shadows, window dragging, font smoothing).
  - [x] Environment Variables dialog (`IDD_ENVVARS`) for User and System variables.
  - [x] Desktop "My Computer" icon right-click context menu (Open, Explore, Manage, Properties -> `sysdm.cpl`).
- [x] Install authentic theme files (`wine/resources/themes/`): `Classic.theme`, `Luna.theme`, `Royale.theme`.

## Phase 9: Wireless Network Configuration & System Tray Network Monitor (Completed)
- [x] Investigate network applet landscape in Wine and ReactOS (Wine lacks `wlanui`, `netshell.dll`, and network tray monitors; ReactOS relies on NT NDISUIO).
- [x] Extract authentic Server 2003 / Windows XP network assets:
  - 13 icons: `net_sig0.ico`..`net_sig5.ico`, `net_blank.ico`, `net_disconnect.ico`, `net_plugged.ico`, `net_recv.ico`, `net_xmit.ico`, `net_xmitrecv.ico`, `net_wireless.ico`.
  - Bitmap: `wzcui.bmp` (network list signal badges).
- [x] Implement Linux host NetworkManager (`nmcli`) & `iphlpapi` backend (`net_backend.c` / `net_backend.h`):
  - Async pipe execution for host `nmcli` scanning, connecting, disconnecting, and radio toggling.
  - Signal level calculation (0-100% mapped to 5 bars).
  - Win32 `iphlpapi` integration for live IP addresses, subnet mask, default gateway, and interface octets.
  - Robust mock / fallback networks for containerized or virtual test environments without Wi-Fi adapters.
- [x] Implement System Tray Network Monitor ("Connections Tray") (`nettray.c` / `nettray.h`):
  - Dynamic tray icon reflecting Wi-Fi 5-bar signal strength, Ethernet dual monitors, or disconnected state.
  - Multi-line tooltip with SSID, speed, signal rating, and status.
  - Single-click to open Wireless Quick Connect (`IDD_WZCQCFG`).
  - Double-click to open Connection Status (`IDD_STATMON_GENERAL_LAN`).
  - Right-click context menu: Disable, Status (bold/default), Repair, View Available Wireless Networks, Open Network Connections.
  - Non-overlapping taskbar tray positioning (left of the 72px clock).
- [x] Implement "Connect to Wireless Network" dialog (`IDD_WZCQCFG`, `wlan_dialog.c` / `wlan_dialog.h`):
  - Available networks list view with authentic 5-bar signal icons, SSID, and security badges.
  - Unsecured network warning panel with required confirmation checkbox.
  - WPA/WPA2 password entry with confirmation and show/hide password toggle.
  - Refresh and direct Connect/Disconnect actions.
- [x] Implement "Wireless Network Connection Status" dialog (`IDD_STATMON_GENERAL_LAN`, `wlan_status.c` / `wlan_status.h`):
  - General tab: status, SSID, live duration timer, speed, custom-drawn 5-bar green signal meter, live byte/packet counters, and blinking twin PC icon.
  - Support tab: DHCP address type, IP, subnet mask, default gateway, and Repair action.
- [x] Native Linux Wireless GUI Deep Integration:
  - Zero-fork `/proc/net/wireless` reader in `net_backend.c` for microsecond signal level calculations.
  - Zero-fork `/sys/class/net/<dev>/operstate` reader for interface status.
  - System D-Bus Wi-Fi hardware scan trigger (`org.freedesktop.NetworkManager.Device.Wireless.RequestScan`).
- [x] Implement Network Connections Control Panel Applet (`ncpa.cpl` in `wine/dlls/ncpa.cpl/`):
  - Standard `CPlApplet` implementation supporting `CPL_INIT`, `CPL_GETCOUNT`, `CPL_INQUIRE`, `CPL_NEWINQUIRE`, `CPL_DBLCLK`, and `CPL_STARTWPARMSW`.
  - Launches wireless connection manager and network connections folder.
  - Authentic `netfol.ico` resource and metadata strings.
- [x] Implement Network Connections Shell Extension (`netshell.dll` in `wine/dlls/netshell/`):
  - Registered COM classes: `CLSID_NetworkConnections` (`{7007ACC7-3202-11D1-AAD2-00805FC1270E}`) and `CLSID_ConnectionTray` (`{7007ACCF-3202-11D1-AAD2-00805FC1270E}`).
  - Windows NT 5.2 compatible API exports: `StartNCW` (New Connection Wizard), `RepairConnection`, `NcIsValidConnectionName`, `NcFreeNetconProperties`, `HrLaunchConnection`, `HrLaunchConnectionEx`, `HrCreateDesktopIcon`, `HrRenameConnection`, `InvokeDunFile`.
  - Server 2003 authentic `netfol.ico` and `foldwiz.ico` icon resources.
- [x] Implement `wlanui.dll` (`wine/dlls/wlanui/`):
  - Exported `WlanUIEditProfile` and `WLInvokeProfileUI` bridging to the wireless network UI.
- [x] Deep shell integration:
  - Start Menu "Connect To..." and "Settings -> Network Connections".
  - Desktop "My Network Places" double-click and right-click Properties.
  - Command-line parameters: `explorer.exe /wlan` and `explorer.exe /netstatus`.

## Phase 10: Volume Control & Power/Battery Ecosystem (Completed)
- [x] Investigate upstream Wine, ReactOS, and Linux native audio/power interfaces:
  - Wine lacks `sndvol32.exe`, `mmsys.cpl`, `powercfg.cpl`, and had `fill_battery_state` stubbed as FIXME on Linux.
  - Native Linux audio uses PipeWire (`wpctl`), PulseAudio (`pactl`), and ALSA (`amixer`).
  - Native Linux power management uses kernel sysfs `/sys/class/power_supply/`.
- [x] Implement Linux Audio Backend (`audio_backend.c` / `audio_backend.h`):
  - PipeWire / WirePlumber (`wpctl get-volume @DEFAULT_AUDIO_SINK@`, `wpctl set-volume`, `wpctl set-mute`).
  - PulseAudio (`pactl get-sink-volume @DEFAULT_SINK@`, `pactl set-sink-volume`, `pactl set-sink-mute`).
  - ALSA (`amixer sget Master`, `amixer sset Master`).
  - Win32 `waveOutGetVolume` / `waveOutSetVolume` and in-memory mock fallback.
- [x] Implement System Tray Volume Applet (`volumetray.c` / `volumetray.h`):
  - Dynamic speaker icon (`IDI_VOLUME`) and muted icon (`IDI_MUTE`).
  - Volume percentage and muted tooltip states.
  - Mouse scroll wheel volume increment/decrement (5% steps).
  - Left-click popup vertical volume slider (`trayvol_dialog.c` / `IDD_TRAYMASTER`).
  - Double-click mixer application launcher (`sndvol32.exe`).
  - Right-click context menu: "&Open Volume Control", "&Adjust Audio Properties".
- [x] Implement Sound Volume Control application (`sndvol32.exe` in `wine/programs/sndvol32/`):
  - Multi-strip mixer layout: Master Volume, Wave, SW Synth, CD Player, Microphone, Line In.
  - Authentic 3D beveled balance and volume trackbars with tick marks.
  - Options -> Properties (`mmsys.cpl`), Help -> About (`ShellAboutW`).
  - Tray mode support via `/t` command-line switch.
  - Authentic Server 2003 assets: `mixrcont.ico`, `mute.ico`, `lspeak.ico`, `rspeak.ico`, `asound.ico`.
- [x] Implement Sounds and Audio Devices Control Panel Applet (`mmsys.cpl` in `wine/dlls/mmsys.cpl/`):
  - `CPlApplet` export supporting Volume, Sounds, and Audio device property sheets.
  - Volume page: device name, master volume slider, mute toggle, "Place volume icon in the taskbar", speaker setup presets.
  - Sounds page: Windows sound scheme selector, program event sound association list, WAV playback preview (`PlaySoundW`), and browse dialog.
  - Audio page: default playback, recording, and MIDI devices.
  - Authentic Server 2003 assets: `speaker.ico`, `syssound.ico`, `playback.ico`, `volspkr.bmp`, `play.bmp`, `stop.bmp`.
- [x] Implement Linux Kernel Battery & AC Power Integration (`wine/dlls/ntdll/unix/system.c`):
  - Implemented Linux `/sys/class/power_supply` reader for `fill_battery_state` in Wine's ntdll.
  - Resolves `NtPowerInformation(SystemBatteryState)` and Win32 `GetSystemPowerStatus` on Linux.
  - Dynamically sets `PowerCaps->SystemBatteriesPresent` based on hardware presence.
- [x] Implement Linux Power & Battery Backend (`power_backend.c` / `power_backend.h`):
  - Zero-fork direct reading of `/sys/class/power_supply/BAT*` and `AC*`.
  - Battery capacity percentage, charging status, online AC state, and remaining runtime calculation.
- [x] Implement System Tray Power Applet (`powertray.c` / `powertray.h`):
  - Dynamic battery icon (`IDI_BATT`) and AC plug icon (`IDI_BATTERYPLUG`).
  - Tooltips: "On AC power", "% remaining (charging)", "H:MM hours (% remaining)".
  - Automatic visibility management: hides on desktop machines without batteries.
  - Right-click power scheme switcher (Home/Office Desk, Portable/Laptop, Presentation, Always On, Minimal Power, Max Battery).
  - Double-click launches `powercfg.cpl`.
- [x] Implement Power Options Control Panel Applet (`powercfg.cpl` in `wine/dlls/powercfg.cpl/`):
  - `CPlApplet` export supporting Power Schemes, Power Meter, Advanced, and Hibernate pages.
  - Power Schemes page: scheme presets, monitor, disk, and standby timeouts for AC and battery.
  - Power Meter page: live AC/battery source indicator, battery percentage progress bar, and battery health details.
  - Advanced page: taskbar icon toggle, password on resume, power button and lid close behavior.
  - Hibernate page: hibernate toggle, RAM requirement calculation, and disk free space.
  - Authentic Server 2003 assets: `pwrmng.ico`, `batt.ico`, `plug.ico`, `hibernate.ico`.

## Phase 11: Display Properties Dynamic Reactivity & Mouse Properties (`main.cpl`) (Completed)
- [x] Investigate display resolution and color depth handling across Wine, Explorer, and GameScope:
  - Wine's `NtUserChangeDisplaySettings` sets the primary source mode and broadcasts `WM_DISPLAYCHANGE` to all windows.
  - Resolved Explorer deficiency: Subclassed desktop window (`desktop_wnd_proc`) previously ignored `WM_DISPLAYCHANGE` and `WM_SIZE`, leaving the virtual desktop window and view surface static.
- [x] Implement Dynamic Virtual Desktop & Compositor Reactivity in Wine Explorer:
  - `desktop.c`: Intercept `WM_DISPLAYCHANGE` and `WM_SIZE`. Calls `SetWindowPos` on virtual desktop window to new dimensions. Wine's X11 driver executes `XMoveResizeWindow`, triggering GameScope compositor auto-scaling and nested resize.
  - `desktop_view.c` / `desktop_view.h`: Added `desktop_view_on_display_change`. Handles `WM_DISPLAYCHANGE` and `WM_SIZE`, resizes view window, recalculates auto-arrange or clamps icons to visible screen boundaries so no icons are left off-screen, and repaints wallpapers across all position modes (Center, Tile, Stretch, Fit, Fill).
  - `systray.c` / `tray_clock.c` / `startmenu_view.c` / `taskband.c`: Repositions and dynamically resizes taskbar, Start button, Show Desktop button, taskband, and tray clock without duplicate window recreation.
- [x] Implement Enhanced Display Settings & Confirmation Dialog (`desk.cpl`):
  - Added 16-bit / 32-bit color depth selection from `IDC_SET_BPP_COMBO` with device capability detection.
  - Added authentic Windows XP / Server 2003 15-second countdown confirmation dialog (`IDD_CONFIRMSETTINGS` / `confirm_dialog_proc`): "Your desktop has been reconfigured. Do you want to keep these settings?\n\nReverting in %d seconds".
  - Implemented automatic rollback to previous resolution and bit depth on "No" or timer expiration.
- [x] Implement Mouse & Keyboard Control Panel Applet (`main.cpl` in `wine/dlls/main.cpl/`):
  - Extracted and integrated 20 authentic Server 2003 binary icons and bitmaps: `mouse.ico`, `keybd.ico`, `prtfldsh.ico`, `fntfldsh.ico`, `admfldsh.ico`, `mouse.bmp`, `fldr_clo.ico`, `fldr_opn.ico`, `clklock.ico`, `dblclk.ico`, `delay.ico`, `pspeed.ico`, `pt02.ico`, `repeat.ico`, `sglclk.ico`, `snapto.ico`, `sonar.ico`, `vanish.ico`, `wheel.ico`.
  - Exported standard `CPlApplet` supporting both Mouse (`@0`) and Keyboard (`@1`) applets, wiring directly into `control.exe`.
  - Mouse Buttons page (`DLG_MOUSE_BUTTONS`): Left/Right button swap (`SwapMouseButton`) with live owner-drawn mouse button highlight preview, double-click speed slider (`GetDoubleClickTime` / `SetDoubleClickTime`), interactive double-click folder test area, ClickLock toggle and settings dialog (`IDD_CLICKLOCK_SETTINGS_DLG`).
  - Mouse Pointers page (`DLG_MOUSE_POINTER`): Schemes dropdown (Default, Inverted, Black, Standard Large, None), owner-drawn cursor list with authentic system cursor roles, live preview icon, pointer shadow toggle (`SPI_SETCURSORSHADOW`), Use Default, and Browse (`GetOpenFileNameW` for `*.cur;*.ani`).
  - Mouse Pointer Options page (`DLG_MOUSE_MOTION`): Motion speed trackbar (`SPI_SETMOUSESPEED`, 1-20), Enhance pointer precision (`SPI_SETMOUSE`), Snap To default dialog button (`SPI_SETSNAPTODEFBUTTON`), pointer trails with Short/Long slider (`SPI_SETMOUSETRAILS`), Hide pointer while typing (`MouseVanish`), Show location on CTRL key (`MouseSonar`).
  - Mouse Wheel page (`DLG_MOUSE_WHEEL`): Vertical scrolling lines per notch with updown spinner buddy (`SPI_SETWHEELSCROLLLINES`, 1-100) or one screen at a time (`WHEEL_PAGESCROLL`).
  - Keyboard Speed page (`DLG_KEYBD_SPEED`): Character repeat delay trackbar (`SPI_SETKEYBOARDDELAY`, 0-3), repeat rate trackbar (`SPI_SETKEYBOARDSPEED`, 0-31), test text box, cursor blink rate trackbar (`GetCaretBlinkTime` / `SetCaretBlinkTime`).
  - Hardware page (`DLG_HARDWARE`): Full list view of detected pointing devices and keyboards with Linux/Wayland/libinput connection status and device properties.
  - Added `WINE_CONFIG_MAKEFILE(dlls/main.cpl)` to `wine/configure.ac`.

## Phase 12: Verification, Packaging & Deployment (Next Steps)
- [ ] Test build configuration with Wine build system (`./configure && make`).
- [ ] Verify Gamescope session startup with display manager.
- [x] Provide comprehensive administration and user documentation ([ADMIN_GUIDE.md](ADMIN_GUIDE.md), [USER_GUIDE.md](USER_GUIDE.md)).




## Future: system tier (session 0)

Windows runs services in session 0 under SYSTEM and keeps one registry for the machine; each user session only adds
its own `HKEY_USERS\<SID>`. Wine starts `services.exe`, `plugplay`, `rpcss` and `mountmgr` inside every user's
`wineserver`, each with its own copy of `HKLM`. After the prefix split (`docs/PREFIX_SPLIT_WORKORDER.md`, which shares
the machine files and merges `HKLM` on save), the next step is one machine-level instance run by a system account that
owns the services and `HKLM`, with user sessions connecting to it. This is the prefix split's option (c) and needs
cross-user `wineserver` work, so the two are one project. Decide per service whether it belongs in that tier or is
better served by a Linux daemon through `wexhost` (D-Bus). References: ReactOS `base/system/services`,
`ntoskrnl/config` (design only), `dll/win32/userenv` (profiles).

- Unified tokens, program elevation and Chromium functional compatibility:
  `TOKEN_SECURITY_WORKORDER.md` (Gemini), with sources/licenses in
  `TOKEN_SECURITY_REFERENCES.md`. Functionality first; hostile-code containment is
  a later hardening stage. `ELEVATION_WORKORDER.md` is historical input. Keep the
  backend seam suitable for the later system tier without requiring it up front.

## Future: stock browser installers (2026-11-02)

Priority is below the DWM phases (`DWM_ARCHITECTURE.md`). Goal: the vendors' own installers run unmodified in the combined
WoW64 build and produce a browser that starts, without manual extraction of the payload. Order: Chromium installer
(done 2026-11-05: the official `mini_installer.exe` installs per-user and system-level and the fixture passes; see
`BROWSER_INSTALLER_TEST_PLAN.md`), Google Chrome (MSI and stub), then Microsoft Edge (MSI and stub). Chrome and Edge are
blocked on services running without the SYSTEM token and on the registry DACL (`CHROME_INSTALLER_ANALYSIS.md` section 11,
`EDGE_INSTALLER_ANALYSIS.md`). No test
against stock Wine is needed.

- Google Chrome enterprise MSI (`googlechromestandaloneenterprise64.msi`, tried 2026-11-02 in the QEMU guest with
  `ChromiumLab/mk-gchrome.py`): the MSI starts, its `DoInstall` action runs Google's embedded installer, which fails
  (`err:msi:execute_script ... DoInstall returned 1627`) after trying to create the COM classes
  `{6d40bdad-bf13-535b-bc3b-eeee65b1d745}` and `{8018f647-bf07-55bb-82be-a2d7049f7ce4}` ("not registered"; our
  `dllhost.exe` surrogate then exits with "Can't create instance"). Next: trace `+ole,+reg,+service,+msi` to find which registration or
  service step is missing, fix it in the Wine fork or the machine image, rerun; then the consumer `ChromeSetup.exe`.
- Edge: its installer was not tried; the validated route uses an extracted tree.
- Check after each: the installer exits 0, the files and uninstall entry exist, the browser starts and the functional
  fixture passes (`validate-*.py`), including a screenshot.

## Future: UWP / package compatibility, track U1 (2026-11-05)

Fixture/oracle and unpackaged baseline work started; packaged Wine support remains pending. Priority is below the
DWM, browser-installer and system-tier work; independent tooling and an owned UWP lab can proceed alongside them.
Current evidence and dispatch: `docs/uwp/EXECUTION_PLAN.md`. Target: unmodified packaged
native UWP apps, starting with a self-owned, gated, non-XAML C++/WinRT fixture (packaged identity, WinRT activation,
async, clean exit), then CoreWindow, then a separate native XAML track (Uno is reference only), PRI/XBF,
and only later the installed Calculator (private test, never committed). Everything is off by default behind one gate.
Plan, review, repository split and first backlog: `docs/uwp/` (start with `docs/uwp/README.md`).

## Track E1: production elevation and installer authorization (2026-11-05)

New open track, scheduled immediately before and alongside "Future: stock browser installers": machine-wide installers
(Chrome, Edge, most application installers) need elevation, so E1 gates them and the ISO acceptance of machine installs.
Completed phases are not renumbered. Work and order are in `docs/ELEVATION_ROADMAP_FIT.md` (reconcile the T3 documents and
measure the installed configuration; lab installer diagnosis with the compile-time test broker, which is a disposable
fixture and never shipped; decide the trust architecture; filtered-administrator flow; standard-user credentials with an
authenticated identity; secure presentation on X11 and Wayland; image integration). The shared system tier (session 0) is
not a prerequisite. Lab status: the test broker works in the QEMU guest (`CHROME_INSTALLER_ANALYSIS.md` section 14).

### E1 follow-ups (added 2026-11-11, not started)

Both come after the standard-user password elevation leaves the lab (status: `docs/E1_S18E_REMEDIATION.md`). The ideas
were confirmed by looking at how the Raven project handles installers (`docs/uwp/NOTE_RAVEN_REUSE.md` has the licence
notes; concepts only, no code).

* **Installer detection.** A program without a `requestedExecutionLevel` that looks like an installer is treated as
  asking for elevation, as Windows does: by file name (setup, install, update, patch), by known installer formats
  (Windows Installer, IExpress, WiX bundles) and by version resource. Without it only manifests and the `runas` verb
  lead to the prompt, and an unmarked `setup.exe` runs limited and fails later. Needs: the rule set written from the
  Windows behaviour, the policy value that turns it off (`EnableInstallerDetection`), cases for 32-bit and 64-bit
  programs, and no detection for programs that state a level.
* **Snapshot before an elevated installer.** Before a detected installer runs elevated, the machine side (machine
  root and machine registry hives) is snapshotted so that a bad install can be undone; this gives System Restore a
  real function in the shell (restore point list, restore, discard). Open design points: what exactly is in a
  snapshot (machine root only, or the installing administrator's profile as well), the storage (reflink copy where
  the filesystem has it, otherwise a hardlink tree or an archive), who may restore (an administrator, through the
  same elevation), retention, and behaviour while other users' programs run from the machine root.

## Future: Group Policy and domain join (added 2026-11-11, not started)

Design note: `docs/GROUP_POLICY.md`. When the host is joined to an Active Directory domain, the domain's policy
reaches the Windows side: Samba's `samba-gpupdate` fetches the GPOs and a small extension of ours writes the registry
policy; Windows programs are told about the join. Depends on the machine registry of the system tier (machine policy
is written to the machine root with the system group) and feeds track E1 (consent settings and the account lockout
policy can then come from the domain). Order:

1. Join detection in `wexhost` and the two stock-Wine changes that report it (`NetGetJoinInformation`,
   `DsRoleGetPrimaryDomainInformation`), tested against a Samba AD controller in the lab with a 32-bit program.
2. The registry-policy extension: machine policy through a system-group writer, user policy through `wexhost`;
   account lockout and consent settings from the security template.
3. Refresh signalling and the applied-GPO list in `userenv` (ReactOS code for the events), then `gpupdate`,
   `gpresult` and a local policy editor, with what does not apply to Wine grayed out.

Independent of the domain: local policy tools over the existing values (`secpol.msc`, `net accounts`,
`NetUserModalsGet/Set` level 3 for the lockout policy). Open decisions are listed in the note: Winbind or SSSD as the
supported join, the per-logon trigger for user policy, protecting the user policy keys, and the licence of the
extension (Samba's API is GPL-3.0-or-later).

## Future: DWM graphics follow-ups (added 2026-11-11, not started)

Two ideas kept from comparing our compositor design with the Raven project's presentation code (concepts only; the
comparison found no reason to rework the design in `DWM_ARCHITECTURE.md`). Both belong to existing DWM phases.

* **Transparent DirectComposition without CPU composition (P7b, P14).** Today a transparent DComp target is composed
  on the CPU every frame (X11, GL renderer) and is not done on Wayland. For the common simple scene, one swapchain
  visual over the program's own GDI content (Chromium's shape), present the two as separate layers that the
  compositor stacks, instead of reading the underlay back: on Wayland the swapchain is already its own surface
  (`wex_render_surface_v1`), so the GDI content would become a second layer above or below it with per-pixel alpha;
  on X11 the equivalent is the ARGB window surface over the GL client window. Open points: order and alpha when GDI
  content is above the swapchain, rounded clips across layers, thumbnails and Peek sampling the combined result,
  and falling back to the current CPU path for scenes that are not this simple.
* **Stretching a stale-size swapchain during resize (Wayland).** While a window is resized, the last presented GPU
  frame can be scaled over the new client area (`wp_viewport`) until the program presents at the new size, instead
  of showing a gap or the old size. First step: measure what `wexcomp` shows today during an interactive resize of a
  GPU-presenting window (this was not checked), then decide.
