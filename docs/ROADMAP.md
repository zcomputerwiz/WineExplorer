# WineExplorer DE Roadmap

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
  - [x] Authentic "Shut Down Windows" dialog (`IDD_SHUTDOWN`).
  - [x] Clean Win32 process shutdown sequence.
  - [x] Linux system shutdown via systemd-logind D-Bus / `systemctl poweroff` / `systemctl reboot`.
- [x] Gamescope Display Manager session scripts (`wine-explorer-session.sh` & `wine-explorer.desktop`).

## Phase 8: Verification, Packaging & Theming Assets (Next Steps)
- [ ] Install default Windows Classic (`Classic.theme`) and Luna theme definitions into prefix `C:\windows\resources\themes\`.
- [ ] Test build configuration with Wine build system.
- [ ] Create test harness and launcher documentation for Gamescope Wayland compositor session.
