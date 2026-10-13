# Work order: tray icons for Linux applications (StatusNotifierItem + XEmbed)

Status: ready, 2026-10-13. Applies to the shipping X11 session now (and carries over to Wayland unchanged, since it
is D-Bus).

## Problem

Linux applications put their notification-area icons on a desktop's tray through either the D-Bus
**StatusNotifierItem** protocol (SNI; Steam, Discord, most modern apps, via libappindicator/libayatana) or the old X11
**system tray** (`_NET_SYSTEM_TRAY_S0`, XEmbed). Our shell supports neither (`DBUS_PLAN.md` lists SNI as to-do), so
Steam has no tray icon and closing its window looks like quitting.

## Tasks

1. **SNI host in wexhost (unix side, D-Bus, `dlopen`ed libdbus as today):**
   - Own `org.kde.StatusNotifierWatcher` on the session bus if no one else does (register items, emit
     `StatusNotifierItemRegistered/Unregistered`, `IsStatusNotifierHostRegistered`=true) and register as a host.
   - For each item read `Id`, `Title`, `Status` (Passive/Active/NeedsAttention), `IconName`/`IconPixmap`,
     `AttentionIcon*`, `ToolTip`, `Menu`, `ItemIsMenu`; follow `NewIcon`, `NewStatus`, `NewToolTip`, `NewTitle`.
   - Icons: `IconPixmap` (ARGB32, network byte order) directly; `IconName` through the freedesktop icon theme lookup
     (gio's `GtkIconTheme`-free path: search `$XDG_DATA_DIRS/icons/<theme>/*/apps` and `pixmaps`, prefer 16/24 px).
   - Events up to the PE side through the existing wexhost event path (no polling).
2. **Tray side in explorer (`systray.c`, Win32):** show each item as a normal notification-area icon (same code
   path as Win32 tray icons, so "Hide inactive icons" and per-icon preferences work); tooltip from `ToolTip`/`Title`.
   - Left click → `Activate(x, y)` (or show the menu when `ItemIsMenu`); middle → `SecondaryActivate`; right click →
     the item's menu; scroll → `Scroll`.
   - Menus: read `com.canonical.dbusmenu` (`GetLayout`, `Event`, `AboutToShow`, `LayoutUpdated`) and show it as a
     Win32 popup menu (XP look), including checkmarks, radio items, separators, disabled items and submenus.
3. **XEmbed fallback:** apps that only speak `_NET_SYSTEM_TRAY` (older GTK2/Qt4 apps). Either own
   `_NET_SYSTEM_TRAY_S0` in wexhost's X11 window manager and embed the icon windows into a Win32 tray slot, or bridge
   them to SNI with **snixembed** (ISC; not packaged in Debian 13 — build it, or reimplement its small bridge in
   wexhost). Prefer the bridge: one display path.
4. Reference code (read, borrow where the license allows): waybar's tray module (MIT) for the SNI host and dbusmenu;
   snixembed (ISC) for XEmbed; `docs/DBUS_PLAN.md` for our D-Bus conventions.

## Acceptance

On the dev VM session (and Xvfb for screenshots): Steam (or `nm-applet --indicator` / another SNI app as a stand-in)
shows its icon in the notification area; tooltip; left click activates; right click opens its menu in the XP style
and menu items work; icon changes (status/attention) update without polling; the icon disappears when the app
exits; an XEmbed-only app (e.g. `stalonetray` test client or `xembed` demo) also shows up. Record results and
screenshots in `docs/TRAY_SNI.md`; list changes in `STOCK_CHANGES.md` only if stock Wine files are touched (should
not be needed: wexhost and explorer's shell code are ours).
