# WineExplorer Desktop Environment: Architectural Specification

## 1. High-Level Architecture & Operating Model

WineExplorer DE runs as an X11 session from a display manager such as GDM. Customized Wine remains the desktop runtime; Explorer owns the X11 root as the window manager and hosts native Linux and separate Wine/Proton windows in Win32 frames. Proton keeps its normal application runtime and prefixes. It does not replace the shell's Wine. See [LINUX_WINDOWS.md](LINUX_WINDOWS.md) for the implemented bridge and its limits.

The current stack is GDM → Xorg → Wine desktop/Explorer, with Linux and Proton clients connecting to the same X server. Gamescope is optional per application; nesting the entire shell under Gamescope would isolate it from the session's external windows. Native Wayland window management is future work.

```
+-----------------------------------------------------------------------------------+
|                              Linux Operating System                               |
|        (systemd-logind, DRM/KMS Display, evdev Inputs, PipeWire Audio, D-Bus)     |
+-----------------------------------------+-----------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|                        Xorg / X11 Desktop Session                                 |
|           - Explorer owns SubstructureRedirect on the X11 root                   |
|           - XRandR provides output modes and physical resolution changes         |
|           - Native Linux, separate Wine and Proton clients share the server     |
+-----------------------------------------+-----------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
|                       Wine Explorer Shell Desktop Session                         |
|                                                                                   |
|  +-----------------------------------------------------------------------------+  |
|  |                Desktop Surface Window (`SHELLDLL_DefView`)                  |  |
|  |  - Full feature parity with Windows desktop surface                         |  |
|  |  - Interactive icon grid (Auto-arrange, Align to grid, Registry persistence)|  |
|  |  - Rubberband selection marquee & multi-item drag-drop                      |  |
|  |  - Standard shell objects (My Computer, My Documents, Recycle Bin, Network)  |  |
|  |  - Wallpaper rendering (Center, Tile, Stretch, Fit, Fill)                   |  |
|  |  - Desktop context menu (Arrange, Line up, Refresh, New, Properties)        |  |
|  +-----------------------------------------------------------------------------+  |
|                                                                                   |
|  +-----------------------------------------------------------------------------+  |
|  |                      Taskbar Window (`Shell_TrayWnd`)                       |  |
|  |  +---------------+ +-----------------------------+ +---------------------+  |  |
|  |  | Start Button  | | TaskBand (`MSTaskListWClass`)| | System Tray & Clock |  |  |
|  |  | - Classic/XP/ | | - Running window buttons    | | - Notification icons|  |  |
|  |  |   Themed      | | - Active/flashing states    | | - Locale tray clock |  |  |
|  |  | - Win key hook| | - Grouping & window context | | - Calendar popup    |  |  |
|  |  +---------------+ +-----------------------------+ +---------------------+  |  |
|  +--------------------------------------+--------------------------------------+  |
|                                         |                                         |
|  +--------------------------------------+--------------------------------------+  |
|  |           Flexible Start Menu Engine (Classic / 2-Column / Modern)          |  |
|  |  - Classic Server 2003 / 2000 cascading single-column menu                  |  |
|  |  - Windows XP / 2003 Two-Column menu (Pinned + Recent + Shell Folders)      |  |
|  |  - Open-Shell inspired Modern menu with integrated real-time search box     |  |
|  +--------------------------------------+--------------------------------------+  |
|                                         |                                         |
|  +--------------------------------------+--------------------------------------+  |
|  |               Theme Engine (`.theme` & `.msstyles` Integration)             |  |
|  |  - Windows XP/2003 `.theme` file parser (`[VisualStyles]`, `[Colors]`, etc.) |  |
|  |  - UXTheme integration for control theming & classic 3D beveled fallback    |  |
|  |  - System color mapping (`SetSysColors`), sound schemes, cursor schemes     |  |
|  +--------------------------------------+--------------------------------------+  |
|                                         |                                         |
|  +--------------------------------------+--------------------------------------+  |
|  |              Display Manager & Power Management Integration                  |  |
|  |  - Authentic "Shut Down Windows" dialog (`IDD_SHUTDOWN`)                     |  |
|  |  - Linux power off / reboot via `systemd-logind` D-Bus & `systemctl`       |  |
|  |  - Graceful Win32 session shutdown (`ExitWindowsEx` / window broadcast)     |  |
|  +-----------------------------------------------------------------------------+  |
+-----------------------------------------------------------------------------------+
```

---

## 2. Core Subsystems Specification

### 2.1 Theme Engine (`theme_engine.c` / `theme_engine.h`)
- **Theme File Parsing**:
  - Full support for `.theme` files (INI format).
  - `[VisualStyles]`: Resolves `Path` (`.msstyles`), `ColorStyle`, `Size`, applying via `uxtheme!SetSystemVisualStyle`.
  - `[Control Panel\Colors]`: Parses 29 standard system RGB color definitions and applies them immediately via `SetSysColors`.
  - `[Control Panel\Desktop]`: Parses wallpaper path, `TileWallpaper` (0/1), and `WallpaperStyle` (0=Center, 1=Tile, 2=Stretch, 6=Fit, 10=Fill), invoking `SystemParametersInfoW(SPI_SETDESKWALLPAPER)`.
  - `[CLSID\{...}\DefaultIcon]`: Reads custom icon definitions for My Computer, My Documents, Recycle Bin (full/empty), and Network Places.
  - `[AppEvents\Schemes]`: Sound event triggers (Logon, Logoff, Asterisk, Critical Stop, Exclamation).
- **Control Rendering**:
  - Checks `IsThemeActive()` and `OpenThemeData()`.
  - When themed: delegates to `uxtheme` (`DrawThemeBackground`, `DrawThemeText`).
  - When classic: uses authentic Server 2003 / NT 3D beveled rendering (`DrawEdge`, `COLOR_3DFACE`, `COLOR_3DHILIGHT`, `COLOR_3DSHADOW`, `COLOR_3DDKSHADOW`).

### 2.2 Desktop Surface (`desktop_view.c` / `desktop_view.h`)
- **Shell Parity**:
  - Renders an interactive icon grid directly on the desktop.
  - Integrates virtual namespace objects (My Computer, My Documents, Recycle Bin, My Network Places).
  - Enumerates physical files from `CSIDL_DESKTOPDIRECTORY` and `CSIDL_COMMON_DESKTOPDIRECTORY`.
- **Layout & Interaction**:
  - Standard Windows 75x75 grid spacing.
  - Options: Auto-Arrange, Align to Grid.
  - Position persistence in registry (`HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Streams\Desktop`).
  - Selection marquee (semi-transparent or dotted lasso selection box).
  - Drag-and-drop icon movement.
- **Desktop Context Menu**:
  - View (Large, Medium, Classic icons)
  - Arrange Icons By (Name, Size, Type, Modified, Auto Arrange, Align to Grid)
  - Refresh (re-reads directory and repositions)
  - Paste / Paste Shortcut
  - New (Folder, Shortcut, Text Document)
  - Properties (launches Display Properties / Theme settings dialog)
- **Wallpaper Engine**:
  - Multi-mode wallpaper rendering: Centered, Tiled, Stretched, Fit, Fill.
  - Solid background color fill behind wallpaper.

### 2.3 Taskbar & TaskBand (`taskband.c` / `taskband.h`)
- **Shell Hook Window Tracking**:
  - Registers as shell hook window (`RegisterShellHookWindow`).
  - Listens for `HSHELL_WINDOWCREATED`, `HSHELL_WINDOWDESTROYED`, `HSHELL_WINDOWACTIVATED`, `HSHELL_REDRAW`, `HSHELL_FLASH`.
- **Button Visuals**:
  - Inactive: Raised 3D border, standard face.
  - Active: Sunken 3D border, dithered checkerboard face.
  - Flashing: Toggles highlight color when window requests attention (`FlashWindowEx`).
  - 16x16 icon + window caption text.
- **Interactions**:
  - Left click on inactive window: Brings window to foreground, unminimizes if needed.
  - Left click on active window: Minimizes window.
  - Right click: Authentic window context menu (Restore, Move, Size, Minimize, Maximize, Close).
- **Taskbar Grouping**:
  - Groups multiple windows belonging to the same executable when taskbar width is limited.
  - Shows popup menu when clicked.

### 2.4 Start Menu Engine (`startmenu_view.c` / `startmenu_view.h`)
- **Multiple Style Switcher**:
  1. **Classic Server 2003 / NT Style**:
     - Cascading hierarchical menu.
     - Left-side vertical title banner.
     - Programs, Favorites, Documents, Settings, Search, Help, Run, Log Off, Shut Down.
  2. **Windows XP / 2003 Two-Column Style**:
     - Top user header banner.
     - Left column: Pinned internet/email apps, frequently used programs, "All Programs" submenu.
     - Right column: My Documents, My Pictures, My Music, My Computer, Control Panel, Connect To, Printers and Faxes, Help and Support, Search, Run.
     - Bottom bar: Log Off and Turn Off Computer.
  3. **Modern / Open-Shell Inspired Style**:
     - Real-time search box at bottom of Start menu.
     - Live program and document filtering as the user types.
- **Start Button**:
  - Classic 3D beveled button with Windows flag icon and "Start" label.
  - Themed Luna / Royale pill-shaped button.
  - Hotkey hooks: Windows key (`VK_LWIN`, `VK_RWIN`) and Ctrl+Esc.

### 2.5 System Tray & Clock (`tray_clock.c` / `tray_clock.h`)
- **Tray Area**:
  - Sunken notification area bezel.
  - Win32 `Shell_NotifyIcon` processing with rich balloon tooltip popups (`NIIF_INFO`, `NIIF_WARNING`, `NIIF_ERROR`).
  - Inactive icon hiding with collapse/expand chevron button (`<` / `>`).
- **Tray Clock**:
  - Displays localized short time.
  - Double-click launches Date/Time control panel (`timedate.cpl`).
  - Single-click opens popup calendar with month/year navigation.
- **Show Desktop / Minimize All Button**:
  - Quick Launch / Show Desktop toggle button to minimize and restore all open windows.

### 2.6 Power Management & Display Manager Session (`power_mgmt.c` / `power_mgmt.h`)
- **Shut Down Dialog**:
  - Authentic "Shut Down Windows" dialog matching Server 2003 / XP.
  - Choices: Shut down, Restart, Log off, Stand by.
- **Linux Execution**:
  - First broadcasts `WM_QUERYENDSESSION` / `WM_ENDSESSION` to gracefully close Win32 applications.
  - Executes Linux system shutdown:
    - Calls systemd-logind via D-Bus: `org.freedesktop.login1.Manager.PowerOff(true)`.
    - Fallback to `systemctl poweroff` or `shutdown -h now`.
    - For reboot: `org.freedesktop.login1.Manager.Reboot(true)` or `systemctl reboot`.
    - For log off: Exits the Explorer desktop and terminates the display manager session.
