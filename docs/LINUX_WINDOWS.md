# Linux windows in the shell

The shipping X11 path uses a Wine virtual desktop: one X window that covers the screen, with no X window manager. Programs of
our wineserver live inside it. Anything else, such as native Linux programs, the Steam client, Proton games or a
second Wine, maps its windows on the X root window. Without a window manager those windows would have no frame,
no taskbar button and no stacking with Win32 windows.

The separate Wayland L2 lab path uses native compositor surfaces and explicit
Wine proxy frames rather than X reparenting. `WEX_WAYLAND_NATIVE_WINDOWS=1` enables
Explorer's experimental SSD adoption in an isolated native Wayland shell; it is
off by default. Read [WAYLAND_L2.md](WAYLAND_L2.md) for matching topic builds,
limits, recovery, acceptance and `scripts/check-wayland-shell.sh`. CSD/fullscreen
and initially minimized windows remain native in this first lab gate, and
interactive resize/maximize is disabled pending native negotiation.

## Design: Wine is the window manager

Explorer is the X11 window manager of the display. Each Linux window is reparented into the X window of a Win32
window that Explorer creates for it (the *host*). The host is then an ordinary top-level window. Taskbar buttons,
the Win32 z-order, activation, frames and captions from the visual style, minimize, maximize and the work area all
come from code we already have. Cygwin/X's multiwindow mode does the same in the other direction.

| Part | File | Role |
|---|---|---|
| X side | `dlls/wexhost/x11wm.c` (unix) | Own X connection with `SubstructureRedirect` on the root. Turns MapRequest, ConfigureRequest, Unmap/Destroy, PropertyNotify and ClientMessage into `WEX_WM_EVENT`s. Reparents, configures, closes and releases windows. Forwards keyboard focus. Grabs click-to-activate and the shell keys. Publishes EWMH. |
| API | `include/wexhost.h` (`WexWm*`), `dlls/wexhost/wexhost.c` | Event thread like the polkit agent's; the calls the shell makes. |
| Hosts | `src/dlls/wexshell/linux_windows.c` | One host per window: style from the window type and Motif hints, title, `_NET_WM_ICON`, position (NorthWest gravity), size limits, states, fullscreen. |
| Start | `src/dlls/wexshell/desktop.c` | `linux_windows_start()` in shell mode. `HKCU\Software\WineExplorer\Shell` `ManageLinuxWindows`=0 turns it off. |
| Applications | `dlls/wexhost/apps.c`, `WexAppsList` / `WexAppsLaunch`, `startmenu_view.c` | GIO discovers visible freedesktop entries and launches their desktop IDs. Both Start menu styles have a Linux Applications submenu. |

The desktop remains on customized Wine. Steam/Proton and other Wine installations use their own runtimes and prefixes, connecting to the same X server as normal Linux applications. The session launcher runs Explorer directly on X11. There is no implemented Gamescope service bridge; use Gamescope per application. A compositor/service bridge can be designed separately without changing the desktop's Wine runtime.

Mapping between X and Win32:

| X / ICCCM / EWMH | Host |
|---|---|
| MapRequest (normal, dialog, utility, splash) | Create the host, then `WexWmManage` (reparent, save set, `WM_STATE` Normal, `_NET_FRAME_EXTENTS`, synthetic ConfigureNotify). Docks, desktops, menus, tooltips and notifications are mapped unmanaged. |
| `WM_TRANSIENT_FOR` | Owner window: no taskbar button, centered on the owner |
| Automatic dialog placement / ConfigureRequest | Fit the full themed frame to the owner's monitor work area. Resizable dialogs shrink when their minimum size permits; oversized fixed-size dialogs retain their size with the caption on screen. Valid explicit positions remain unchanged. User dragging is unrestricted. |
| `_MOTIF_WM_HINTS` without decorations (GTK/Steam client-side decorations) | `WS_POPUP` + `WS_EX_APPWINDOW` |
| Click in foreign client / Win32 foreground change | Attach the foreground thread to Explorer's input queue while activating, then detach without rebuilding Explorer's queue. `WexWmFocus` and a foreground WinEvent hook synchronize X keyboard focus with the selected Win32 window. |
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
- GTK client-side title-bar move/resize, ignoring unwanted GTK origin positioning, shell-key replay and Alt+Tab across Linux and Win32 windows were implemented in the subsequent `4f87754` and `4634367` commits.

Application bridge checks use a separate Xvfb display and fresh prefix, leaving the visible Luna desktop untouched. GIO filters NoDisplay and OnlyShowIn, preserves localized names and handles Exec field codes, Terminal and D-Bus activation. Directly spawned applications inherit the Linux session variables but none of the shell's `WINE*` variables. Explicit environment assignments in an application's own desktop entry still take effect. D-Bus-activated applications use their service's activation environment, which this launcher does not change.

The submenu is sorted by localized display name and rebuilt on every opening. Install/remove changes were checked without restarting Explorer. IDs remain separate from Windows shortcut paths; names containing ampersands are displayed literally. Missing GIO hides the submenu, and launch errors are reported. Current limits: 512 displayed entries, names up to 511 UTF-16 units, desktop IDs below 512 UTF-8 bytes; flat listing with no per-application menu icons or category hierarchy yet. The bridge returns category and PNG metadata for a later presentation pass.

The content stays intact when the frame repaints.

The task band keeps an owned local copy of each window icon. Wine icon handles from another process cannot be drawn directly; when copying `WM_GETICON` or the class icon fails, the task band reads the window's `_NET_WM_ICON` pixels through `wexhost` before falling back to the executable icon. This preserves shell namespace icons for My Computer, Documents, and other Explorer folders without matching localized names or adding CLSID-specific taskbar rules. Shell redraw notifications refresh icons during navigation; replacement and window removal release the old copy.

Additional real-session checks under Luna: a Linux application launched from All Programs (NetworkManager connection editor) has no inherited `WINE*` variables; Linux client-area clicks after My Computer bring the host forward and deliver typed input to xterm; repeated switching and Alt+Tab work across both runtimes; maximizing a Linux host respects the work area and leaves the taskbar visible. The click path does not rely on cached native active-window metadata, and queued FocusIn events are validated against current X focus. Alt+Tab's completion runs after its keyboard hook returns.

Dialog placement regression checks: build `scripts/linux-dialog-probe.c` with `cc -Wall -Wextra -o linux-dialog-probe linux-dialog-probe.c -lX11`, then run on a separate 800×600 Xvfb desktop with the 30-pixel taskbar. It checks a LibreOffice-sized save dialog, subsequent program move/resize requests, centering on an offscreen owner, valid explicit positioning, and oversized fixed-size dialogs. It also verifies that the foreign client fits inside its host, rather than merely clipping an oversized client with a smaller frame.

VM note: with glamor (`modesetting` on VMSVGA), X drawing by non-Wine programs was broken: missing text and blank
windows. Wine was unaffected because it draws through SHM images. `/etc/X11/xorg.conf.d/20-wineexplorer-modesetting.conf`
now has `Option "AccelMethod" "none"`; the previous file is kept as `.before-accel-none`.

## Not done yet

- Docks and struts (`_NET_WM_STRUT_PARTIAL`) in the work area, and multiple monitors.
- Grouping by program: every host belongs to Explorer's process. Windows without `_NET_WM_ICON` now use a generic program icon.
- If Explorer dies, the save set cannot bring the Linux windows back, because their parent is destroyed with Wine's
  connection. A normal shell exit releases them first.
- Linux application menu categories/icons, pinned/MFU entries and the remaining freedesktop services (StatusNotifierItem, notifications).
- A compositing phase like DWM: redirect windows with the X Composite extension for live taskbar and Alt+Tab
  thumbnails and transparency. XP had no compositor, so it is optional and later.
- Gamescope cannot run as the session compositor at the same time, because it is a window manager itself. Run it per
  game (`gamescope -- game`) as a window instead.
