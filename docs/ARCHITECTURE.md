# WineExplorer Desktop Environment: Architectural Specification

## 1. High-Level Architecture

WineExplorer DE transforms Wine's virtual desktop mode into a full Linux Desktop Environment session.

```
+-------------------------------------------------------------------------------+
|                             Linux System & Kernel                             |
|              (DRM/KMS Display, evdev Inputs, pipewire Audio, D-Bus)           |
+---------------------------------------+---------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
|                       Gamescope (Wayland Compositor)                          |
|         - Handles display modes, resolution changes, fractional scaling        |
|         - Wayland compositor socket (`WAYLAND_DISPLAY`)                       |
|         - Embedded Xwayland server (`DISPLAY=:X`)                              |
+---------------------------------------+---------------------------------------+
                                        |
                                        v
+-------------------------------------------------------------------------------+
|                         Wine Virtual Desktop Session                          |
|                                                                               |
|  +-------------------------------------------------------------------------+  |
|  |                         Wine Explorer (Shell Root)                      |  |
|  |                                                                         |  |
|  |  +-------------------------------------------------------------------+  |  |
|  |  | Desktop Window (`Progman` / `SHELLDLL_DefView` / `SysListView32`)  |  |  |
|  |  | - Wallpaper rendering & desktop icons grid                           |  |  |
|  |  | - File management, shortcut launching, selection marquee            |  |  |
|  |  +-------------------------------------------------------------------+  |  |
|  |                                                                         |  |
|  |  +-------------------------------------------------------------------+  |  |
|  |  | Taskbar Window (`Shell_TrayWnd`)                                  |  |  |
|  |  |  +---------------+  +--------------------------+  +---------------+  |  |
|  |  |  | Start Button  |  | TaskBand (`MSTaskListWClass`) | System Tray   |  |  |
|  |  |  | (Start Menu)  |  | - Window switch buttons  |  | - Notify icons|  |  |
|  |  |  |               |  | - Active / Flash states  |  | - Clock/Applets| |  |
|  |  |  +---------------+  +--------------------------+  +---------------+  |  |
|  |  +-------------------------------------------------------------------+  |  |
|  +------------------------------------+------------------------------------+  |
|                                       |                                       |
|  +------------------------------------+------------------------------------+  |
|  |                     Window Management & IPC Layer                       |  |
|  |  - Win32 Top-Level Window Tracking (HSHELL hooks / WinEvents)           |  |
|  |  - Linux XDG Desktop Entry Integration (`/usr/share/applications`)      |  |
|  |  - Native Linux App Launcher Bridge (`xdg-open` / process spawner)      |  |
|  |  - D-Bus Session Bus Bridge (logind power, upower, Notifications)       |  |
|  +-------------------------------------------------------------------------+  |
+-------------------------------------------------------------------------------+
```

---

## 2. Key Modules & Subsystems

### 2.1 The Explorer Root Process (`programs/explorer/`)
In Wine, `explorer.exe` initializes the virtual desktop when `/desktop` is supplied:
- Current implementation initializes a desktop window and an appbar (`desktop.c`, `appbar.c`).
- It creates a rudimentary start menu (`startmenu.c`) and notification icon listener (`systray.c`).
- **Enhancement Target**:
  - Modularize `programs/explorer/` into clean subsystems:
    1. `desktop_wnd.c`: Desktop background, icon view, and context menu.
    2. `taskbar.c` / `taskband.c`: Taskbar container, window button management, focus tracking.
    3. `startmenu.c`: Hierarchical classic start menu with XDG desktop parsing and search.
    4. `tray.c` / `clock.c`: System tray notification sink, status icons, and clock applet.
    5. `xdg_bridge.c`: Integration with Linux desktop entries, mime types, and system actions.

### 2.2 Compositor & Display Layer: Gamescope Integration
Gamescope provides an ideal container for WineExplorer DE:
- Seamless handling of nested screen resolutions (e.g. running 1080p inside 4K display, integer/FSR scaling).
- Dynamic resolution switching without disturbing the underlying Linux display server or Wayland compositor.
- Direct KMS/DRM session mode when running as the primary desktop session.
- Both Xwayland and native Wayland surfaces supported.

### 2.3 Window Management & Task Tracking
To display running windows in the taskbar:
- Use standard Win32 shell hooks (`RegisterShellHookWindow`, `WH_SHELL` events):
  - `HSHELL_WINDOWCREATED`: Add button to TaskBand.
  - `HSHELL_WINDOWDESTROYED`: Remove button.
  - `HSHELL_WINDOWACTIVATED`: Highlight active task button.
  - `HSHELL_REDRAW`: Update title or icon.
- Provide minimize, restore, maximize, and task context menu (Restore, Move, Size, Minimize, Maximize, Close).

### 2.4 Reference Implementations & Clean-Room Strategy
  - Reference for exact dimensions, 3D border styles (classic bevels, `EDGE_RAISED`, `EDGE_SUNKEN`), color tokens (`COLOR_3DFACE`, `COLOR_3DSHADOW`, etc.), button padding, font sizing (Tahome 8pt), and icon alignments.
  - Strictly clean-room: read visual layout and logic specifications, implement in clean C code.
- **ReactOS**:
  - Reference for clean-room implementation of `taskband.cpp`, `traywnd.cpp`, and shell COM interfaces.
- **Open-Shell**:
  - Reference for menu categorization, recent item algorithms, and keyboard navigation.
