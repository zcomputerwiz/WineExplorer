# WineExplorer DE Roadmap

## Phase 1: Environment & Repository Baseline (Current)
- [x] Unpack all provided source archives into organized workspace.
- [x] Establish read-only isolated references directory for clean-room inspection (`references/`).
- [x] Initialize Git repository at `D:\DeepThought\Projects\WineExplorer\`.
- [x] Configure `.gitignore` and `.gitattributes` to safeguard reference trees and enforce clean LF endings.
- [x] Document high-level architecture, module decomposition, and reference guide.
- [ ] Commit repository baseline and initial Wine tree.

## Phase 2: Shell Analysis & Spec Extraction
- [ ] Catalog exact visual metrics: taskbar height (default 28/30px), Start button dimensions, font settings (Tahoma 8pt), tray icon padding (16x16 with 2px borders), 3D border bevels.
- [ ] Compare ReactOS implementation (`taskband.cpp`, `traywnd.cpp`, `taskswnd.cpp`).
- [ ] Map Wine's existing `programs/explorer/` (`desktop.c`, `startmenu.c`, `systray.c`, `appbar.c`) and identify missing primitives.

## Phase 3: Taskbar & Window Management (TaskBand)
- [ ] Implement `RegisterShellHookWindow` / shell hook message routing in Wine Explorer.
- [ ] Create TaskBand window control (`MSTaskListWClass`) to hold running application buttons.
- [ ] Add active/inactive window button rendering with 3D bevels and 16x16 application icons.
- [ ] Implement window switching, minimize-on-click, bring-to-front, and window context menu (Restore, Move, Minimize, Maximize, Close).

## Phase 4: Start Menu Enhancement & XDG Integration
- [ ] Redesign classic Start Menu hierarchy matching Windows Server 2003 / Classic NT layout.
- [ ] Implement Start button with authentic logo, text, and pressed/hover states.
- [ ] Add standard shell folders: Programs, Documents, Settings, Search, Help, Run, Shut Down.
- [ ] Implement Linux XDG desktop entry parser (`/usr/share/applications`) to populate Programs menu with native Linux applications.
- [ ] Implement launcher bridge to spawn native Linux binaries or execute Wine applications.

## Phase 5: System Tray & Status Applets
- [ ] Modernize notification area to handle modern Win32 tray icons with balloon tooltips.
- [ ] Implement tray clock with accurate formatting and popup calendar.
- [ ] Build bridge for Linux StatusNotifierItem (SNI) / system status (volume, battery/power, network).

## Phase 6: Desktop Surface & File Management
- [ ] Interactive desktop icon view (`SHELLDLL_DefView` / `SysListView32`).
- [ ] File system integration for `~/Desktop` / `CSIDL_DESKTOPDIRECTORY`.
- [ ] Right-click context menus (Arrange Icons, Refresh, New, Properties).
- [ ] Wallpaper background positioning and auto-scaling.

## Phase 7: Gamescope & Linux Session Packaging
- [ ] Create Gamescope session launch script (`wine-explorer-session.sh`).
- [ ] Configure dynamic resolution handling and fullscreen desktop container.
- [ ] Provide desktop entry and display manager session (`/usr/share/wayland-sessions/wine-explorer.desktop`).
