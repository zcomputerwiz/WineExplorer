<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# WineExplorer Desktop Environment — User Guide

Welcome to the WineExplorer Desktop Environment (WineExplorer DE). WineExplorer provides an authentic, fast, and familiar Windows XP and Server 2003 styled desktop experience running natively on Linux through Wine, integrating native Linux software and Windows applications into a single unified desktop.

---

## 1. The Desktop Surface

The desktop surface provides an authentic classic workspace with icon grid alignment, multi-selection, and context management.

### 1.1 Desktop Icons & Namespace Folders

The desktop displays both special system namespace folders and file system desktop directories:
- **My Computer**: Direct access to virtual storage drives (`C:`, `Z:`), Control Panel applets, and system properties.
- **My Documents**: Quick access to your personal documents folder (`C:\users\<username>\My Documents`).
- **My Network Places**: Network connection manager, network setup wizards, and shared locations.
- **Recycle Bin**: Deleted files repository with restore capabilities.
- **Desktop Folders**: Items stored in your personal desktop (`C:\users\<username>\Desktop`) and the shared public desktop (`C:\users\Public\Desktop`).

### 1.2 Icon Arrangement & Management

- **Grid Alignment**: Icons align to an authentic 75×75 pixel grid.
- **Marquee Selection**: Click and drag on an empty desktop area to draw a rubberband selection rectangle around multiple icons.
- **Desktop Context Menu**: Right-click any empty desktop space to access:
  - **Arrange Icons By**: Organize icons by Name, Size, Type, or Modified date; toggle **Auto Arrange**, **Align to Grid**, and **Show Desktop Icons**.
  - **Refresh**: Rescan the desktop folders and redraw all icons (`F5`).
  - **New**: Create a new Folder, Shortcut, or Text Document.
  - **Properties**: Open the **Display Properties** control panel (`desk.cpl`).
- **Wallpaper Styles**: Wallpapers can be displayed in Center, Tile, Stretch, Fit, or Fill position modes via Display Properties.

---

## 2. The Taskbar & TaskBand

The taskbar sits at the bottom edge of the screen, providing running application tracking, quick window controls, and notification applets.

### 2.1 Running Application Buttons

- **Button States**: Each running application displays a 16×16 icon, window title, and visual sunken/highlight states indicating whether the window is currently active. Flashing states indicate background alert notifications.
- **Taskbar Grouping**: When multiple windows from the same program are open or taskbar space becomes constrained, similar buttons group into a single consolidated button with a count badge.
- **Window Management Context Menu**: Right-click any taskbar button to access standard window controls: **Restore**, **Move**, **Size**, **Minimize**, **Maximize**, and **Close**.
- **Minimize Toggle**: Clicking the active application button minimizes the window to the taskbar; clicking an inactive button restores and focuses it.

### 2.2 Taskbar Customization & Properties

Right-click any empty taskbar space and select **Properties** to configure:
- **Lock the Taskbar**: When unlocked, you can drag the top border of the taskbar to expand it into multiple rows of buttons.
- **Auto-hide the taskbar**: Automatically slides the taskbar off-screen until you move your mouse cursor to the screen bottom edge.
- **Keep the taskbar on top of other windows**: Ensures maximized application windows do not cover the taskbar.
- **Group similar taskbar buttons**: Automatically aggregates related application windows.
- **Show Quick Launch**: Enables the Quick Launch toolbar adjacent to the Start button.
- **Show Desktop Button**: Click the Show Desktop icon on Quick Launch (or press `Win+D`) to instantly minimize all open windows and reveal the desktop.

---

## 3. The Start Menu

WineExplorer provides three authentic Start menu styles switchable via Taskbar and Start Menu Properties (`taskbar_props.c`).

### 3.1 Start Menu Variants

1. **Windows XP Style (Two-Column)**:
   - **User Header**: Displays your user account avatar and display name.
   - **Left Column**: Contains pinned programs at the top (e.g. Internet browser and E-mail client), separated by a line from your Most Frequently Used (MFU) applications list, and the **All Programs** cascading submenu.
   - **Right Column**: Direct shortcuts to My Documents, My Pictures, My Music, My Computer, My Network Places, Control Panel, Connect To, Printers and Faxes, Help and Support, Search, and Run.
   - **Footer**: Distinct buttons for **Log Off** (Switch User or Log Off) and **Turn Off Computer** (Stand By, Turn Off, Restart).
2. **Classic Start Menu (Server 2003 / NT Style)**:
   - Single-column cascading menu with an authentic vertical left-side gradient banner.
   - Fast cascading hierarchical menus for Programs, Documents, Settings, Search, Help and Support, Run, and Shut Down.
3. **Modern Start Menu (Hybrid Style)**:
   - Two-column layout extended with an integrated real-time search box at the bottom, inspired by Open-Shell, allowing instant filtering of applications and settings.

### 3.2 Linux Applications Integration

Both Start menu styles include a dynamic **Linux Applications** submenu. WineExplorer automatically scans your Linux host system (`/usr/share/applications/*.desktop`) via GIO and populates native Linux applications directly into the Start menu. Clicking a Linux application launches it seamlessly in its own themed desktop window.

### 3.3 Start Button Context Menu

Right-click the **Start** button to access:
- **Open**: Opens the current user's Start Menu programs directory.
- **Explore**: Opens Windows Explorer browsing the Start Menu hierarchy.
- **Search**: Launches Search Companion.
- **Properties**: Opens Taskbar and Start Menu Properties.

---

## 4. System Tray (Notification Area)

Located at the far right of the taskbar, the sunken system notification area hosts background applets, alerts, and system controls.

### 4.1 Inactive Icon Hiding

To prevent visual clutter, seldom-used tray icons are automatically hidden behind a chevron expander (`<` or `>`).
- Click the **chevron** button to expand and reveal hidden icons.
- Right-click the taskbar, select **Properties**, and click **Customize** under "Hide inactive icons" to set per-icon behavior:
  - *Hide when inactive* (default)
  - *Always hide*
  - *Always show*

### 4.2 System Tray Applets

- **Clock & Calendar**:
  - Displays the current time formatted according to your regional settings.
  - Double-click the clock to open **Date and Time Properties** (`timedate.cpl`), featuring a real-time ticking analog clock, monthly calendar, and worldwide time zone selector.
- **Volume Control Applet**:
  - Displays a speaker icon reflecting active volume or mute status.
  - Hover over the icon and scroll your mouse wheel to adjust the system volume up or down in 5% increments.
  - Left-click once to open the vertical Master Volume slider popup with a Mute checkbox.
  - Double-click to open the full multi-channel Sound Volume mixer (`sndvol32.exe`).
- **Power & Battery Monitor**:
  - Displays dynamic battery charge level and AC power plug status on portable computers (automatically hidden on desktop PCs without batteries).
  - Hover to view tooltip details: remaining battery percentage, charging state, and estimated remaining runtime.
  - Right-click to switch power schemes (Home/Office Desk, Portable/Laptop, Max Battery, Always On).
  - Double-click to open **Power Options Properties** (`powercfg.cpl`).
- **Network Connections Monitor**:
  - Displays Ethernet twin monitors (blinking on packet transmission) or Wi-Fi 5-bar signal strength.
  - Left-click to open the **Connect to Wireless Network** dialog (`IDD_WZCQCFG`) with network scanning and WPA/WPA2 password entry.
  - Double-click to open **Connection Status** (`IDD_STATMON_GENERAL_LAN`) with live byte counters and IP configuration.
  - Right-click for Quick Actions: Disable, Status, Repair, View Available Wireless Networks, and Open Network Connections (`ncpa.cpl`).

---

## 5. Themes, Visual Styles & Display Settings

Visual styles and customization are managed through **Display Properties** (`desk.cpl`), accessible by right-clicking the desktop and selecting **Properties** or via Control Panel.

### 5.1 Included Visual Styles

- **Luna Style (`Luna.theme`)**:
  Authentic Windows XP visual style featuring rounded title bars, rich blue taskbar, green Start button, and three color schemes:
  - *Default (Blue)*
  - *Homestead (Olive Green)*
  - *Metallic (Silver)*
- **Royale Style (`Royale.theme`)**:
  The Energy Blue visual style featuring sleek glossy title bars and vibrant blue accents.
- **Windows Classic (`Classic.theme`)**:
  Authentic Server 2003 / Windows 2000 3D beveled appearance, ideal for maximum performance and classic NT aesthetics.
- **Open Aero Glass Style**:
  When selected via theme settings or registry (`DwmFrameStyle=1`), captioned windows feature wide translucent glass borders, blurred titlebars, and Aero caption button metrics.

### 5.2 Display Properties Pages

- **Themes Tab**: Select and apply complete desktop themes with instant monitor preview.
- **Desktop Tab**: Choose background wallpapers, background color, positioning mode (Center, Tile, Stretch, Fit, Fill), and open the **Customize Desktop** dialog to toggle standard desktop icons.
- **Appearance Tab**: Switch visual styles (Windows XP style vs. Windows Classic), choose color schemes, adjust font sizes, and click **Advanced** to customize individual 3D metrics, border padding, and font typography.
- **Settings Tab**: Adjust screen resolution using the slider, select color depth (16-bit High Color or 32-bit True Color), configure multi-monitor layouts, and test settings with an authentic 15-second countdown rollback safety dialog.

---

## 6. Window Effects & Performance Options

Visual effects and animation behaviors can be configured in **System Properties** (`sysdm.cpl`):
1. Open System Properties (`Win+Pause` or right-click My Computer -> Properties).
2. Select the **Advanced** tab.
3. Under **Performance**, click **Settings** to open the **Visual Effects** options.

Available effects:
- **Drop shadows for icon labels on the desktop**: Displays clear readable drop shadows under desktop icon text over bright wallpapers.
- **Show window contents while dragging**: Smoothly drags full window contents instead of wireframe outlines (`SPI_SETDRAGFULLWINDOWS`).
- **Smooth edges of screen fonts**: Enables font antialiasing (`SPI_SETFONTSMOOTHING`).
- **Drop shadows under menus and mouse pointer**: Enables subtle depth shadows (`SPI_SETCURSORSHADOW`).

---

## 7. Live Thumbnails & Aero Peek

When desktop composition is enabled (`Composition=1` and `DwmService=1` in the administrator configuration), advanced window previews become active:

- **Taskbar Live Thumbnails**: Hovering your mouse over any taskbar button displays a scaled live thumbnail preview of the window's current contents (or its iconic live preview bitmap if minimized).
- **Alt+Tab 2D Thumbnail Switcher**: Pressing `Alt+Tab` displays an interactive grid of live window thumbnails alongside the application title, rather than static icons.
- **Aero Peek**: Hovering over a taskbar thumbnail temporarily makes all other desktop windows transparent, giving you a clean preview of your desktop and the selected window without switching focus. Moving the mouse away restores all windows instantly.

---

## 8. Verified Keyboard Shortcuts

The following keyboard shortcuts have been verified by inspection of the WineExplorer source code.

### 8.1 System & Shell Hotkeys (Confirmed in Source)

| Shortcut | Action | Source Implementation |
|---|---|---|
| `Win + R` | Open the **Run** dialog | `src/dlls/wexshell/systray.c` (`IDHK_RUN`) |
| `Win + E` | Open **My Computer** in Windows Explorer | `src/dlls/wexshell/systray.c` (`IDHK_EXPLORE`) |
| `Win + D` | Toggle **Show Desktop** (minimize / restore all windows) | `src/dlls/wexshell/systray.c` (`IDHK_DESKTOP`) |
| `Win + M` | **Minimize all** open windows | `src/dlls/wexshell/systray.c` (`IDHK_MINIMIZE_ALL`) |
| `Win + Shift + M` | **Restore all** minimized windows | `src/dlls/wexshell/systray.c` (`IDHK_RESTORE_ALL`) |
| `Win + F` | Open **Search Companion** | `src/dlls/wexshell/systray.c` (`IDHK_FIND`) |
| `Win + Pause / Break` | Open **System Properties** (`sysdm.cpl`) | `src/dlls/wexshell/systray.c` (`IDHK_SYS_PROPERTIES`) |
| `Win + L` | **Lock** the computer / switch session | `src/dlls/wexshell/systray.c` (`IDHK_LOCK`) |
| `Ctrl + Esc` | Open or toggle the **Start Menu** | `src/dlls/wexshell/desktop.c` (`IDHK_TASKLIST`) |
| `Windows Key` | Open or toggle the **Start Menu** (press and release alone) | `src/dlls/wexshell/desktop.c` (`winkey_hook`) |

### 8.2 Window Switching (Alt+Tab) Shortcuts (Confirmed in Source)

| Shortcut | Action | Source Implementation |
|---|---|---|
| `Alt + Tab` | Open window switcher and advance selection forward | `src/dlls/wexshell/alttab.c` |
| `Alt + Shift + Tab` | Step selection backward in window switcher | `src/dlls/wexshell/alttab.c` |
| `Arrow Keys` (Left/Right/Up/Down) | Navigate selection grid in window switcher | `src/dlls/wexshell/alttab.c` |
| `Enter` | Activate selected window in switcher | `src/dlls/wexshell/alttab.c` |
| `Escape` | Cancel window switching and dismiss switcher dialog | `src/dlls/wexshell/alttab.c` |
| Release `Alt` | Switch to and focus currently highlighted window | `src/dlls/wexshell/alttab.c` |

### 8.3 Desktop Surface Shortcuts (Confirmed in Source)

| Shortcut | Action | Source Implementation |
|---|---|---|
| `F3` | Launch **Search Companion** from desktop | `src/dlls/wexshell/desktop_view.c` |
| `F5` | **Refresh** desktop icons and view | `src/dlls/wexshell/desktop_view.c` |
| `Enter` | Open or execute the currently selected desktop item | `src/dlls/wexshell/desktop_view.c` |
| `Delete` | Delete selected desktop items | `src/dlls/wexshell/desktop_view.c` |
| `Ctrl + A` | **Select all** items on the desktop | `src/dlls/wexshell/desktop_view.c` |
| `Arrow Keys` (Up/Down/Left/Right) | Move selection cursor across desktop icons | `src/dlls/wexshell/desktop_view.c` |
| `Alphanumeric Keys` (Type-ahead) | Jump directly to the next icon matching the typed letters | `src/dlls/wexshell/desktop_view.c` |

### 8.4 Start Menu Navigation Shortcuts (Confirmed in Source)

| Shortcut | Action | Source Implementation |
|---|---|---|
| `Escape` | Close the Start menu | `src/dlls/wexshell/startmenu_view.c` |
| `Up / Down Arrow` | Move selection up or down within current menu column | `src/dlls/wexshell/startmenu_view.c` |
| `Tab / Shift + Tab` | Move focus between menu zones and columns | `src/dlls/wexshell/startmenu_view.c` |
| `Left / Right Arrow` | Switch between Left (Programs) and Right (Places) columns | `src/dlls/wexshell/startmenu_view.c` |
| `Enter / Space` | Activate or launch the highlighted Start menu item | `src/dlls/wexshell/startmenu_view.c` |

### 8.5 Shortcuts Marked Not Verified (Not Implemented in Repository)

The following shortcuts are standard in physical Windows installations but are **not verified** or not present in the current WineExplorer codebase:
- `Win + B`: Focus system notification area (not verified).
- `Win + U`: Open Utility Manager / Accessibility tools (not verified).
- `Win + F1`: Open Windows Help and Support Center (not verified).
- `Alt + Esc`: Direct cycle through open windows without showing switcher dialog (not verified).
- `Win + Tab`: 3D Flip switcher (not verified; WineExplorer provides 2D thumbnail or icon grid Alt+Tab).
- `Aero Snap` (`Win + Left/Right Arrow`): Snapping windows to half-screen edges (not verified in window manager).
- `Aero Shake`: Shaking titlebar to minimize background windows (not verified).

---

## 9. Where Files Live

WineExplorer integrates Windows paths with the Linux filesystem using an intuitive directory layout:

### 9.1 Virtual Windows Drives

- **`C:\` (System Drive)**:
  - `C:\windows`: Shared system binaries, fonts, and libraries (`/var/lib/wineexplorer/machine/drive_c/windows`). Standard users have read-only access.
  - `C:\Program Files` & `C:\Program Files (x86)`: Application installation folders. Software installed here is accessible to all users.
  - `C:\ProgramData`: Shared application data for all users.
  - `C:\users\<username>\`: Your personal user profile directory:
    - `C:\users\<username>\Desktop`: Files appearing on your personal desktop.
    - `C:\users\<username>\My Documents`: Your personal documents, pictures, and music folders.
    - `C:\users\<username>\Application Data`: Per-user application settings and configurations.
- **`Z:\` (Linux Root Drive)**:
  - `Z:\` maps directly to the root (`/`) of your Linux host filesystem.
  - Your personal Linux home directory is accessible at `Z:\home\<username>\`.
  - External USB drives, optical media, and removable disks appear under `Z:\media\` or `Z:\run\media\`.

### 9.2 Linux File System Locations

If you are working from a Linux terminal:
- **User Prefix**: `~/.wine/` (contains your personal registry `user.reg` and user profile directories).
- **Shared Machine Root**: `/var/lib/wineexplorer/machine/` (shared system files and `system.reg`).
- **Linux Applications**: Desktop entry files located in `/usr/share/applications/` and `~/.local/share/applications/` automatically appear in your Start menu under **Linux Applications**.
