# WineExplorer DE Roadmap

## Phase 1: Environment & Repository Baseline (Completed)
- [x] Unpack all provided source archives into organized workspace.
- [x] Establish read-only isolated references directory for clean-room inspection (`references/`).
- [x] Initialize Git repository at `D:\DeepThought\Projects\WineExplorer\`.
- [x] Configure `.gitignore` and `.gitattributes` to safeguard reference trees and enforce clean LF endings.
- [x] Document high-level architecture, module decomposition, and reference guide.
- [x] Import upstream Wine baseline commit.

## Phase 2: Theme Engine & Classic/Themed Infrastructure
- [x] Specify `.theme` INI parser architecture (`[VisualStyles]`, `[Control Panel\Colors]`, `[Control Panel\Desktop]`, `[CLSID]`, `[AppEvents]`).
- [ ] Implement `theme_engine.c` / `theme_engine.h`:
  - [ ] `.theme` file reader and system color updater (`SetSysColors`).
  - [ ] Wallpaper path and style configuration (`SPI_SETDESKWALLPAPER`).
  - [ ] UXTheme bridge (`OpenThemeData`, `DrawThemeBackground`, `SetSystemVisualStyle`).
  - [ ] Authentic Server 2003 Classic 3D beveled rendering fallback.

## Phase 3: Desktop Surface & File Management Parity
- [ ] Implement `desktop_view.c` / `desktop_view.h`:
  - [ ] Special namespace desktop items (My Computer, My Documents, Recycle Bin, My Network Places).
  - [ ] Physical desktop folder enumeration (`CSIDL_DESKTOPDIRECTORY`, `CSIDL_COMMON_DESKTOPDIRECTORY`).
  - [ ] 75x75 icon grid alignment, auto-arrange, and registry position persistence.
  - [ ] Rubberband selection marquee (drag-to-select).
  - [ ] Desktop right-click context menu (Arrange By, Refresh, New Folder/Shortcut, Properties).
  - [ ] Wallpaper rendering engine (Center, Tile, Stretch, Fit, Fill).

## Phase 4: Taskbar & TaskBand (Window Management)
- [ ] Implement `taskband.c` / `taskband.h`:
  - [ ] Window tracking via `RegisterShellHookWindow` / WinEvents.
  - [ ] Running application buttons with 16x16 icon, title, active sunken state, and flashing state.
  - [ ] Window activation, restore, and minimize-on-click logic.
  - [ ] Window context menu (Restore, Move, Size, Minimize, Maximize, Close).
  - [ ] Taskbar button grouping when space is constrained.

## Phase 5: Flexible Start Menu (Classic, 2-Column XP, Modern Search)
- [ ] Implement `startmenu_view.c` / `startmenu_view.h`:
  - [ ] Classic Server 2003 / NT single-column cascading menu with side banner.
  - [ ] Windows XP / 2003 two-column layout with user header, pinned programs, and shell folders.
  - [ ] Open-Shell inspired modern layout with integrated real-time search box.
  - [ ] Authentic Start button with Windows flag icon and theme-aware rendering.

## Phase 6: System Tray, Clock & Applets
- [ ] Implement `tray_clock.c` / `tray_clock.h`:
  - [ ] Sunken notification tray area.
  - [ ] Win32 `Shell_NotifyIcon` with balloon tooltips.
  - [ ] Locale-formatted clock with interactive calendar popup.
  - [ ] Show Desktop (minimize-all toggle) button.

## Phase 7: Power Management & Display Manager Session
- [ ] Implement `power_mgmt.c` / `power_mgmt.h`:
  - [ ] Authentic "Shut Down Windows" dialog (`IDD_SHUTDOWN`).
  - [ ] Clean Win32 process shutdown sequence.
  - [ ] Linux system shutdown via systemd-logind D-Bus / `systemctl poweroff` / `systemctl reboot`.
- [ ] Gamescope Display Manager session scripts (`wine-explorer-session.sh` & `wine-explorer.desktop`).
