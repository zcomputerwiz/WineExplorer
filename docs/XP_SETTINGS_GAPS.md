# XP settings parity: what is missing and why

Survey of 2026-11-22 (read-only, from source and docs; nothing was built or run, and most file:line citations were not
re-opened by a second reader, so check one before relying on it). Goal of the project: XP's dialogs with XP's settings;
what cannot work on Wine/Linux is shown grayed, with a reason a user can read.

Classes used below:

- **A** works.
- **B** grayed or left out because it does not apply to Wine/Linux. "stated" means the code or docs give the reason;
  "unstated" means the reason is only inferred here and should be written down (a tooltip, a comment, this table).
- **C** not implemented yet, but could be.
- **D** stub: the control exists but does nothing, or is grayed with no reason.

Paths are relative to `src/dlls` unless they start with `wine/`.

## Summary

- Most missing settings are **C or D, not B**. The grays that are genuinely "not applicable" are few and mostly stated.
- Several dialogs that `docs/CONTROL_PANEL.md` and `docs/ROADMAP.md` (Phases 8 to 11) call done are not: System
  (4 of XP's 7 tabs), Display (no Screen Saver tab, no Effects/Advanced), Mouse (scheme not applied), Sounds (event list
  not saved), Power Options (timeouts not saved), Network Connections (no folder, no TCP/IP editing).
- Silent grays break the project rule ("gray out what does not apply"): each needs either an implementation or a stated
  reason. The list is in "Silent grays" at the end.
- Policy: `mmsys.cpl` shells out to `wpctl`/`pactl`/`amixer` (against "prefer D-Bus over spawning tools"); `stobject`
  already uses mmdevapi. The roadmap says "authentic Server 2003 assets" were extracted; check that against the
  proprietary-material rule (`docs/ASSETS.md`).

## 1. System Properties (`sysdm.cpl`, 4 tabs; XP has 7)

| XP item | Here | Class |
|---|---|---|
| General | Wine version, owner, organisation, product ID, CPU, RAM | A. `sysdm.rc:54` still says "Service Pack 2 (Gamescope)"; `:342` hard-codes "Administrator" |
| Computer Name, Change (domain, workgroup rename) | works through polkit and wexhost, join/leave/rename with the join engine | A |
| Change: workgroup edit, DNS suffix | read-only / disabled | B stated (`compname.c:324,367`, `netid.c:12-16`: Samba's setting) |
| Hardware: Device Manager | opens `devmgmt.msc`; Update, Disable, Uninstall grayed | A, B stated (kernel owns drivers) |
| Hardware: Hardware Profiles | message box claiming "managed by the display manager" | D (the claim is untrue; gray it with a reason) |
| Hardware: Driver Signing, Add Hardware Wizard | absent | B unstated (no driver signing; Linux finds hardware) |
| Advanced: Performance Settings | Visual Effects tab only; OK applies 2 of 9 effects; "Let Windows choose" does nothing | D; the 7 others are C (SPI_* calls and registry values) |
| Performance: Advanced tab (scheduling, memory, virtual memory), Data Execution Prevention | absent | B unstated (Linux owns swap, scheduling, NX); a read-only page with grayed controls would match XP |
| Advanced: User Profiles | message box | D, C feasible (profile storage in `docs/E1_S12_PROFILE_STORAGE.md`) |
| Advanced: Startup and Recovery | boot timeout, recovery, auto restart via `org.wineexplorer.Boot1`; dump/event controls grayed | A, B stated (no kdump; the journal logs) |
| Advanced: Environment Variables | lists User and System variables; New/Edit/Delete/OK have no handlers | D; easy C (write the registry, broadcast `WM_SETTINGCHANGE`) |
| Advanced: Error Reporting | absent | B unstated (no WER) |
| System Restore | absent | C (a ROADMAP idea: snapshot before elevated installers) |
| Automatic Updates | absent | B stated for the applet (`CONTROL_PANEL.md:23`), not for the tab; a wexhost apt toggle is C |
| Remote (Assistance, Desktop) | absent | B plausible (no RDP server here), unstated; Remote Desktop could be C via xrdp |

## 2. Display Properties (`desk.cpl`; XP has 5 tabs)

Ours: Themes, Desktop, Appearance, Settings, plus Wine's leftover "Virtual Desktop" tab (XP has none).

| XP item | Here | Class |
|---|---|---|
| Themes: list, preview, open `.theme` | works | A |
| Themes: Save As, Delete | no handlers (`desk.rc:47-48`); roadmap claims them | D; C (the engine already writes `WineExplorer Custom.theme`) |
| Desktop: picture, Browse, Position, Color | works (bmp, jpg, png only) | A |
| Desktop: Active Desktop, HTML wallpaper | absent | B unstated |
| Customize Desktop: icon checkboxes | always checked, not saved | D |
| Customize Desktop: Change Icon, Restore Default | no handlers | D |
| Customize Desktop: opening it | loads its template from `GetModuleHandleW(NULL)` (`desktop_page.c:215`), so likely fails when `desk.cpl` runs under `control.exe` or `rundll32` | D, probable bug |
| Customize Desktop: General tab | absent | C |
| Customize Desktop: Web tab, Desktop Cleanup | absent | B unstated (no Active Desktop) |
| **Screen Saver tab** (list, Settings, Preview, wait, Power) | absent, no doc mentions it | C (idle timer in the shell, `.scr` runner, `SPI_SETSCREENSAVE*`; Power could open `powercfg.cpl`) |
| Appearance: style, colour scheme, font size | works | A |
| Appearance: Effects..., Advanced... (item colours and fonts) | no handlers | D; C |
| Settings: resolution slider with keep/revert | works (wexhost, XRandR) | A |
| Settings: Color quality | disabled, one entry; roadmap claims 16/32 bit | D (B probable, the compositor decides; unstated) |
| Settings: Advanced (Adapter, Monitor, Troubleshoot, Color Management) | button disabled | D. Adapter and Monitor refresh rate are C (`host_modes` carries rates); Troubleshoot is B; Color Management is C (ICC) |
| Settings: monitor list, Identify | one "Display:" label | unverified (roadmap claims multi-monitor) |

## 3. Taskbar and Start Menu (`wexshell`)

The most complete of the dialogs.

| XP item | Here | Class |
|---|---|---|
| Taskbar tab (lock, auto-hide, on top, group, Quick Launch, clock, hide inactive, Customize) | wired | A |
| Notification area Customize (per-icon behaviour, Restore Defaults) | wired; the "Past Items" list is not done | A, C |
| Start Menu tab (Start menu or Classic, two Customize) | wired; extra Skin tab is ours | A |
| Customize Start Menu: General | wired; Internet and E-mail combos hold one fixed entry | A, C (default-browser list) |
| Customize Start Menu: Advanced | tree wired; about 12 items shown `disabled` ("offered like on Windows but not available here", `startprops.c:50`): Display as a menu (5 folders), drag and drop, Favorites menu, Scroll Programs, System Administrative Tools | grays without a per-item reason. Cascading menus and Scroll Programs are C; Favorites is B (no IE) |
| Customize Classic Start Menu | all 11 tree options disabled; Remove and Sort grayed ("the menu always lists folders sorted") | D/C; Sort B stated |
| Language bar toolbar | absent | B unstated (no TSF) |

## 4. Date and Time, Regional and Language

| XP item | Here | Class |
|---|---|---|
| Date and Time, Time Zone | works through timedated and polkit; the calendar is a month calendar control, not XP's month and year drop-downs | A |
| Time Zone: adjust for daylight saving | disabled, informational ("the host's zone rules always apply") | B stated |
| **Internet Time tab** | absent; wexhost already has `WexTimedateGet` (ntp_enabled, can_ntp, ntp_synchronized) and `WexTimedateSetNtp` | C, the cheapest item in this survey |
| Regional Options, Customize (Numbers, Currency, Time, Date, Sorting) | works (ReactOS port, registry overrides) | A |
| Languages: Details (text services, language bar) | disabled | B stated in code ("keyboard layouts come from the system"); `CONTROL_PANEL.md:39` says "future work": the two disagree |
| Languages: complex-script and East Asian file install | disabled | B stated (fonts come from the system) |
| Advanced: non-Unicode language, code pages, default profile | page for admins; all three disabled | B plausible, unstated |

## 5. Mouse and Keyboard (`main.cpl`)

No class B items: every gap could be done.

| XP item | Here | Class |
|---|---|---|
| Buttons (swap, double-click speed and test, ClickLock) | works | A |
| Pointers: scheme list, Save As, Delete | scheme only marks "changed" (`mouse_pointers.c:237`); no handlers for Save/Delete | D |
| Pointers: Browse, Use Default | change an in-memory icon, nothing written to `HKCU\Control Panel\Cursors` | D (the user32 loader already reads that key) |
| Pointers: Apply | `SPI_SETCURSORS` returns TRUE without doing anything (`wine/dlls/win32u/sysparams.c:6797`) | D/C |
| Pointer Options: speed, precision, snap-to, trails | stored | A (host effect unverified) |
| Pointer Options: hide while typing, show location on Ctrl | private values, nothing renders them | D |
| Wheel | works; always shown (XP shows it only with a wheel) | A |
| Keyboard Speed: delay, rate, blink | stored, not pushed to the host repeat rate; "None" blink maps to 1200 ms | A (stored) |
| Hardware: device list | reads `/proc/bus/input/devices`; status hard-coded "Working"; no Troubleshoot | A/D/C |

## 6. Sounds and Audio Devices (`mmsys.cpl`, `sndvol32`; XP has 5 tabs, ours 3)

| XP item | Here | Class |
|---|---|---|
| Volume: slider, mute, taskbar icon | works, but shells out to `wpctl`/`pactl`/`amixer` | A; policy issue |
| Volume: Speaker setup | combo filled, no handler | D (B possible: Wine takes the layout from the mix format) |
| Volume: Speaker Volume, Advanced | both just start `sndvol32` | D |
| Sounds: scheme, Save As, Delete | no handlers | D |
| Sounds: event list | hard-coded 21 rows, no apply; the real store `HKCU\AppEvents\Schemes` is read by `themeui/theme_engine.c` | D; C |
| Sounds: Browse, Play, Stop | works | A |
| Audio: device combos | list real winmm devices; the choice is never applied | D |
| Audio: Advanced, MIDI Volume, About; "use only default devices" | dead or missing | D |
| Voice tab | absent (`wine/dlls/sapi` exists) | C |
| Hardware tab | absent | C |
| `sndvol32`: master volume, `/t` tray mode | works | A |
| `sndvol32`: Wave, SW Synth, CD, Mic, Line, balance, Advanced, Options | fake sliders with fixed values, no handlers, Mic/Line shown without hardware | D |

## 7. Power Options (`powercfg.cpl`; XP has 6 tabs, ours 4)

| XP item | Here | Class |
|---|---|---|
| Power Schemes: scheme list | persists `CurrentPowerPolicy`; the tray reads it | A |
| Power Schemes: 8 timeouts, Save As, Delete | hard-coded per scheme, never saved; no handlers | D |
| Alarms tab | absent | C (UPower thresholds) |
| Power Meter: source and percent | works; per-battery detail is static text, one battery | A, D |
| Advanced: taskbar icon, password on resume, power button, lid | defaults only, nothing read or saved | D; C (wexhost `WexPowerCan`/`WexPowerDo` over logind) |
| Hibernate | enable box always checked, not saved; space figures real | D; C |
| APM tab | absent | B (XP shows it only on non-ACPI machines; unstated) |
| UPS tab | absent | C (UPower, NUT or apcupsd) |

## 8. Network Connections (`ncpa.cpl`, `netshell`, `wlanui`, `wzcdlg`)

| XP item | Here | Class |
|---|---|---|
| Network Connections folder | missing: `ncpa.c` calls `StartNCW`, which opens the Wi-Fi picker; the folder CLSID is registered but not served | C (the docs' "done" is wrong) |
| New Connection Wizard | not a wizard | D |
| LAN and wireless status: General, Support | works (packets only); wireless Support tab lacks Details and View Wireless Networks | A/C |
| Properties: General (adapter, protocols, show icon) | works; Configure is a message box; "notify when limited" not saved | A/D |
| Properties: Install, Uninstall | message boxes with reasons | B stated |
| Properties: component list | IPv4 and IPv6 only ("only components the host really has") | B stated |
| **TCP/IP properties** (IP, DNS, WINS, Options) | a message box, no editing | C, highest value (NetworkManager D-Bus) |
| Authentication tab (802.1X) | absent | C (NetworkManager) |
| Wireless Networks tab | absent; wireless "Properties" reuses the LAN dialog | C |
| Advanced tab (Firewall, ICS) | disabled "inert" with the reason "Windows services; the host handles them" | B stated, weak (ICS could map to NetworkManager "shared") |
| Repair, Wi-Fi connect | report success whatever the result | D |
| Bridges | absent | C (low) |
| `wlanui` | `WlanUIEditProfile` runs `explorer /wlan`; 3 exports are stubs | D |

The Control Panel **Category View** ("Pick a category") is not implemented either; `wexbrowse` shows the folder in Icons view only.

## 9. Folder Options and Explorer

| XP item | Here | Class |
|---|---|---|
| **Folder Options** (General, View with Advanced list, File Types, Offline Files) | missing; Tools > Folder Options is `MF_GRAYED` with no reason (`wexbrowse/browser_ui.c:491`); no `Options_RunDLL` in our shell32 | C (ReactOS port planned, `CONTROL_PANEL.md:26`); Offline Files B (no client-side caching) |
| Hidden files, file extensions | read from the registry (`Hidden`, `HideFileExt`); no UI | C |
| View menu: Toolbars, Status Bar, Explorer Bar > Folders, views, Go To, Refresh | works | A |
| View menu: Arrange Icons By, Choose Details, Customize This Folder, Explorer Bar Search/Favorites/History | absent | C |
| Details columns | fixed five (`shell32/shfldr_fs.c:178-182`); no chooser, no Author/Title/dimensions | A, C |
| File, Edit, Favorites menus | works | A |
| Tools: Map/Disconnect Network Drive | absent | C (`WNetConnectionDialog` plus Samba) |
| Tools: Synchronize | absent | B (Offline Files) |
| Properties: General (file, folder) | works for read-only, hidden, archive, opens with; folder size and "Contains" show "(unknown)" (TODO, `shlview_cmenu.c:1002`); no Advanced button | A/C. Compress and Encrypt are B (no NTFS), Archive and Index C |
| Properties: Sharing | absent (the task pane shows "Share this folder" grayed) | B unstated; C via Samba usershares |
| Properties: Security, Summary, Customize | absent | C (Security needs a POSIX permission mapping; Summary is the OLE property set; Customize is `desktop.ini`) |
| Properties: Previous Versions | absent | B unstated (no VSS analogue) |
| Drive and My Computer Properties (General, Tools, Sharing, Quota) | missing; My Computer opens `sysdm.cpl` | C |
| Send To | works (Desktop, My Documents, `SendTo`); no Mail Recipient, Compressed Folder | A, C |
| Open With | works, hidden under the `NoFileAssociate` policy | A |
| **Recycle Bin Properties** | `rundll32 shell32.dll,Options_RunDLL 7` does not exist; no capacity slider, no "do not move to Recycle Bin", no delete-confirmation box | D; small C |
| New menu | Folder, Shortcut; no Briefcase, Compressed Folder | A, C |
| Task pane: Publish to the Web, E-mail, order prints | grayed ("not available on Wine") | B stated |

## 10. Start menu items, logon, accounts

| XP item | Here | Class |
|---|---|---|
| Start menu styles, pinned, most used, All Programs (plus "Linux Applications") | works; the Linux list is flat and capped at 512 | A, C |
| Run, Search Companion | works; Help, Internet search, Indexing grayed with stated reasons; "Computers and people" grayed with none | A, B stated, D |
| Help and Support | starts `hh.exe` with no arguments; no Help and Support Center | D (B plausible: remove or wire to something real) |
| Windows Update, Windows Catalog | `wupdmgr.exe` does not exist; Catalog opens a dead link | D |
| Log Off, Switch User, Turn Off Computer | works (logind, LightDM greeter); Stand By grayed when logind says no (B stated); Shift for Hibernate not wired (`WEX_POWER_HIBERNATE` exists) | A, C; Disconnect B |
| Welcome screen, classic Log On | works; no "N programs running", no password hint | A, C |
| Fast User Switching | works; no toggle UI | A, C |
| **User Accounts** (`nusrmgr`), Users and Passwords (`netplwiz`) | missing; wexhost has create/list/get and root password only, no delete, rename, picture or password change | C (AccountsService with polkit; `docs/WINLOGON_AND_DWM.md` W6) |
| **Windows Security dialog** (Ctrl+Alt+Del in a session) | only the Welcome screen handles the key | C/D (`WINLOGON_AND_DWM.md` W5) |

## 11. Administrative tools and other applets

| XP item | Here | Class |
|---|---|---|
| **Computer Management** (`compmgmt.msc`) | the console `src/dlls/compmgmt` (2026-11-24): the XP tree, Device Manager hosted as a node, the other nodes grayed with a reason; the desktop "Manage" item launches it | A (the shell), the snap-ins below |
| Snap-ins: Event Viewer, Shared Folders, Local Users and Groups, Services, Performance Logs, Disk Management, WMI Control | nodes of Computer Management, grayed with a reason until each exists (Services: probe of the service manager first; Local Users and Groups: design note; Event Viewer: after the event log service, docs/EVENTLOG_DESIGN.md) | C (the Wine tree has `wevtapi`, the services program; `NetUserAdd` and `NetLocalGroupAdd` are stubs); Disk Management B/C |
| Local Security Policy, Group Policy editor | works; not every XP node | A |
| Administrative Tools folder | absent (`devmgmt`, `gpedit` exist) | C |
| Task Manager | stock Wine: Applications, Processes, Performance; no Networking, Users, Shut Down menu | A, C |
| Accessibility Options | missing; Wine implements only Show Sounds and keyboard preference of the SPI settings; the rest is `WINE_SPI_FIXME` | C, with the Wine-unsupported controls grayed (B) |
| Game Controllers | stock Wine (`joy.cpl`) | A, not XP's tabs |
| Printers and Faxes | `shfldr_printers.c` `EnumObjects` returns `E_NOTIMPL` | D |
| Fonts | no applet; the folder exists | C |
| Add or Remove Programs | stock Wine, named "Add/Remove Programs", one page | A, C (XP name and left band) |
| Internet Options | stock Wine, named "Internet Settings"; Privacy, Programs, Advanced absent | A, C |
| Scheduled Tasks | none (`taskschd`, `schtasks` exist in the tree) | C |
| My Network Places | stock stub with FIXMEs, only "Entire Network" | D, C (Samba or gvfs) |
| Windows Firewall, Security Center | left out ("not Windows' firewall or antivirus") | B stated |
| Add Hardware, Automatic Updates, Network Setup Wizard, Phone and Modem, Wireless Setup Wizard | left out | B stated |
| Scanners and Cameras | left out ("SANE later") | C |
| Speech | left out | C ("later, if Wine's SAPI speaks") |
| Paint | 9 tools, File/Edit/Help menus only | A, C |
| Accessories not present | Character Map, Magnifier, On-Screen Keyboard, Narrator, Sound Recorder, Disk Cleanup, Backup, Tour, Clipboard Viewer, games other than Minesweeper | C |
| Notepad, WordPad, Command Prompt, Regedit, Minesweeper, Calculator | present | A |

## Highest-value C items

1. Folder Options and `Options_RunDLL` (also fixes the Recycle Bin dialog and the grayed Tools item).
2. Internet Time tab (backend exists), Environment Variables handlers, the Customize Desktop module-handle fix: three
   small items.
3. Network Connections folder and TCP/IP properties over NetworkManager; honest Repair and Connect results.
4. Persistence for Pointers, Sounds and Power Options (registry keys already read elsewhere); Power through wexhost.
5. Screen Saver tab; Appearance Effects and Advanced; Themes Save As and Delete.
6. User Accounts and Users and Passwords over AccountsService; Windows Security dialog.
7. Computer Management with Services, Event Viewer and Local Users and Groups snap-ins.
8. Printers and Faxes over `EnumPrinters`; Scheduled Tasks folder; Fonts folder; XP names for Add or Remove Programs and
   Internet Options.

## Silent grays (give each a reason or an implementation)

Hardware Profiles (and its false message), Driver Signing, Error Reporting, Remote, Display Settings colour quality and
Advanced, the Start-menu "Display as a menu" items and the classic menu options, Regional Options Advanced controls,
the Network Advanced tab's reasons, the Sharing and Previous Versions pages of file Properties, "Computers and people"
in the Search Companion, and the Tools > Folder Options menu item.

## Documentation to correct

- `docs/CONTROL_PANEL.md`: the "done" rows for System, Display, Keyboard, Mouse, Network Connections, Power Options and
  Sounds; add per-control reasons.
- `docs/ROADMAP.md`: Phases 8 to 11 (Performance Visual Effects, colour depth, save/delete themes, multi-monitor,
  powercfg timeouts, the sound scheme, `ncpa.cpl`); several paths say `wine/dlls` where the code is in `src/dlls`.
- `docs/CONTROL_PANEL.md:39` and `languages.c`: "Languages details future work" against "keyboard layouts come from the
  system".
- `sysdm.rc:54` ("Service Pack 2 (Gamescope)"), `sysdm.rc:342`, `timedate.rc:49` (placeholder text).
- Dead resources in `main.cpl`: Printers, Fonts and Admin icons and strings.
