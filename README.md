# WineExplorer Desktop Environment (WineExplorer DE)

A desktop environment for Linux under development, built by extending Wine and its Wine Explorer virtual desktop architecture.

## Overview

The goal of this project is to evolve Wine's built-in `explorer.exe` and its virtual desktop mode into a full-featured, standalone Linux Desktop Environment. By combining:
- **Wine Explorer** as the core desktop process and Win32 shell runtime,
- **X11** with Explorer itself managing native Linux and separate Wine/Proton windows,
- **Proton** as an independent application runtime, retaining its own prefixes and normal Steam/launcher integration,
- **Gamescope** as an optional per-application compositor for games,
- **ReactOS Explorer** architecture as an open-source structural and algorithmic guide for NT-style shell components,

---

## Workspace Structure

For the current deployed VM state, build instructions, verified changes and
remaining work, read [the handoff](docs/HANDOFF.md). The live VM runs directly on
X11 with customized Wine as the desktop; Proton remains an independent runtime.
Linux tray integration, multi-monitor management and compositing are still
planned features. [The roadmap](docs/ROADMAP.md) distinguishes current direction
from earlier phases.

```
D:\DeepThought\Projects\WineExplorer\
├── archives/               # Original repository zip archives
├── docs/                   # Architecture, design specifications, and guides
│   ├── ARCHITECTURE.md     # System architecture and compositor integration
│   ├── ROADMAP.md          # Phased development roadmap
│   └── HANDOFF.md          # Current VM state, verification and next pickup
├── references/             # Upstream reference codebases (READ-ONLY, excluded from git)
│   ├── gamescope/          # Valve's Gamescope Wayland compositor
│   ├── open-shell/         # Open-Shell start menu reference
│   ├── reactos/            # ReactOS shell and explorer implementation
│   └── wayland/            # Wayland protocol and core libraries
├── scripts/                # Development, build, and runner scripts
└── wine/                   # Active Wine codebase under development
    ├── programs/explorer/  # Primary target: Wine Explorer desktop environment
    ├── dlls/shell32/       # Shell API, desktop folder handling, shortcuts
    ├── dlls/user32/        # Window management and message routing
    └── dlls/winewayland.drv/ # Wayland driver integration
```

---

## Clean-Room Reference Policy

- **ReactOS / Open-Shell**: Open-source implementations used to study clean NT shell architecture, COM interfaces, and taskbar message handling.
- **Wine**: The active base upstream tree where our extensions and enhancements are developed.

---

## Core Components

1. **Virtual Desktop & Compositor Bridge**
   - Wine virtual desktop window manager enhancement.
   - X11 window management and Win32 hosts for external application windows.
   - Independent Proton runtimes; optional Gamescope per application.
   - Resolution switching and multi-monitor awareness.

2. **Taskbar & Window Management**
   - Running application buttons with icons, titles, and active state tracking.
   - Window minimize, maximize, restore, close, and bring-to-front routing.
   - Taskbar grouping and thumbnail previews.

3. **Start Menu**
   - Classic / NT 5.2 style hierarchical menu.
   - Pinned and recent applications.
   - Linux XDG application desktop entries (`/usr/share/applications`) integrated into the Start menu.
   - Integrated Run dialog, Log Off, and Shut Down options.

4. **System Tray (Notification Area)**
   - Standard Win32 `Shell_NotifyIcon` support.
   - Linux StatusNotifierItem (SNI) and XEmbed tray bridge.
   - Interactive clock, calendar popup, volume, and power management applets.

5. **Desktop Surface**
   - Icon grid placement, selection marquee, and drag-and-drop.
   - Wallpaper management (centered, tiled, stretched).
   - Desktop context menu (Arrange, Refresh, New, Properties).
