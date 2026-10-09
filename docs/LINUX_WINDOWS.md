# Linux windows in the shell

The shell is a Wine virtual desktop: one X window that covers the screen, with no X window manager. Programs of
our wineserver live inside it. Anything else, such as native Linux programs, the Steam client, Proton games or a
second Wine, maps its windows on the X root window. Without a window manager those windows would have no frame,
no taskbar button and no stacking with Win32 windows.

## Design: Wine is the window manager

Explorer is the X11 window manager of the display. Each Linux window is reparented into the X window of a Win32
window that Explorer creates for it (the *host*). The host is then an ordinary top-level window. Taskbar buttons,
the Win32 z-order, activation, frames and captions from the visual style, minimize, maximize and the work area all
come from code we already have. Cygwin/X's multiwindow mode does the same in the other direction.

| Part | File | Role |
|---|---|---|
| X side | `dlls/wexhost/x11wm.c` (unix) | Own X connection with `SubstructureRedirect` on the root. Turns MapRequest, ConfigureRequest, Unmap/Destroy, PropertyNotify and ClientMessage into `WEX_WM_EVENT`s. Reparents, configures, closes and releases windows. Forwards keyboard focus. Grabs click-to-activate and the shell keys. Publishes EWMH. |
| API | `include/wexhost.h` (`WexWm*`), `dlls/wexhost/wexhost.c` | Event thread like the polkit agent's; the calls the shell makes. |
| Hosts | `programs/explorer/linux_windows.c` | One host per window: style from the window type and Motif hints, title, `_NET_WM_ICON`, position (NorthWest gravity), size limits, states, fullscreen. |
| Start | `programs/explorer/desktop.c` | `linux_windows_start()` in shell mode. `HKCU\Software\WineExplorer\Shell` `ManageLinuxWindows`=0 turns it off. |

Mapping between X and Win32:

| X / ICCCM / EWMH | Host |
|---|---|
| MapRequest (normal, dialog, utility, splash) | Create the host, then `WexWmManage` (reparent, save set, `WM_STATE` Normal, `_NET_FRAME_EXTENTS`, synthetic ConfigureNotify). Docks, desktops, menus, tooltips and notifications are mapped unmanaged. |
| `WM_TRANSIENT_FOR` | Owner window: no taskbar button, centered on the owner |
| `_MOTIF_WM_HINTS` without decorations (GTK/Steam client-side decorations) | `WS_POPUP` + `WS_EX_APPWINDOW` |
| `WM_NORMAL_HINTS` | Min/max track size. A user position (`USPosition`) is kept; a program position only when it is not the origin. |
| ConfigureRequest | `SetWindowPos` on the host; the answer is always a synthetic ConfigureNotify |
| `_NET_WM_STATE` (fullscreen, maximized, hidden, above), `WM_CHANGE_STATE` | Topmost borderless monitor rectangle, `SW_MAXIMIZE`, `SW_MINIMIZE`, `HWND_TOPMOST`. The host's state is written back, with `_NET_WM_STATE_FOCUSED` and `WM_STATE` Iconic/Normal. |
| `_NET_ACTIVE_WINDOW` request, or a click (passive button grab, replayed) | `SetForegroundWindow` on the host |
| Host close button | `WM_DELETE_WINDOW`, else `XKillClient` (`_NET_CLOSE_WINDOW` likewise) |
| Wine focuses the host's X window (FocusIn) | `XSetInputFocus` on the program's window, plus `WM_TAKE_FOCUS` (ICCCM 4.1.7) |
| Windows key alone, Ctrl+Esc while a Linux window has the keyboard | Key grabs on the program's window; sent to the shell window as `SC_TASKLIST` |
| Unmap or destroy by the program | Release the window and destroy the host |

The host's surface must not paint over the program's window, because winex11 draws window surfaces with
`IncludeInferiors`. Explorer gives the host a client surface (`WM_WINE_SETPIXELFORMAT`, format -1, as Vulkan
surfaces do): win32u and the server then keep the client area out of the host's surface clip. This message has to be
sent with `SendMessageTimeout`, because `SendMessage` within a thread calls the window procedure from user32 and only
win32u handles Wine's internal messages.

Raising on activation needed one stock change (see `STOCK_CHANGES.md`, `X11DRV_ActivateWindow`). It also fixed Win32
windows in the shell: before it, clicking a window or its taskbar button activated it without bringing it to the front.

## Borrowed code and references

- dwm (MIT/X Consortium): `checkotherwm`, `scan`, `sendevent`, `setfocus` and the list of expected errors.
- Gamescope `steamcompmgr.cpp` (BSD-2-Clause): size hints, unmanaged map and configure requests.
- Openbox (GPL-2.0): behavior only, such as program positions at the origin. No code.

The references are in `references/linux-de/{dwm,openbox}` and `references/gamescope`.

## Verified (WineExplorer-VM, X11, 2026-10-09)

xterm, xclock (Xt), zenity (GTK3) and gnome-text-editor (GTK4) were tested in the session:
- frames, titles and icons from `_NET_WM_ICON`;
- taskbar buttons, and switching with the taskbar or a click;
- stacking below the taskbar;
- keyboard input;
- maximize within the work area, minimize to the taskbar and restore, with `WM_STATE` and `_NET_WM_STATE` updated;
- close through `WM_DELETE_WINDOW`;
- the Windows key opening Start while xterm has focus;
- adopting a window that already existed when the shell started.

The content stays intact when the frame repaints.

VM note: with glamor (`modesetting` on VMSVGA), X drawing by non-Wine programs was broken: missing text and blank
windows. Wine was unaffected because it draws through SHM images. `/etc/X11/xorg.conf.d/20-wineexplorer-modesetting.conf`
now has `Option "AccelMethod" "none"`; the previous file is kept as `.before-accel-none`.

## Not done yet

- `_NET_WM_MOVERESIZE`: windows that draw their own title bar (GTK headerbars, Steam) cannot be moved or resized by
  it. Map it to `SC_MOVE`/`SC_SIZE` and check that Wine's move loop gets the pointer.
- GTK4 windows can appear at the origin; check which hint they send.
- Win+E/R/D and other Windows-key combinations, and Alt+Tab, while a Linux window has the keyboard.
- Docks and struts (`_NET_WM_STRUT_PARTIAL`) in the work area, and multiple monitors.
- Grouping and icons by program: windows without `_NET_WM_ICON` show Explorer's icon, and every host belongs to
  Explorer's process.
- If Explorer dies, the save set cannot bring the Linux windows back, because their parent is destroyed with Wine's
  connection. A normal shell exit releases them first.
- Launching Linux programs from the shell with a clean environment (no `WINEDLLOVERRIDES` and so on), `.desktop`
  entries in the Start menu, and the other freedesktop services listed in the design discussion.
- A compositing phase like DWM: redirect windows with the X Composite extension for live taskbar and Alt+Tab
  thumbnails and transparency. XP had no compositor, so it is optional and later.
- Gamescope cannot run as the session compositor at the same time, because it is a window manager itself. Run it per
  game (`gamescope -- game`) as a window instead.
