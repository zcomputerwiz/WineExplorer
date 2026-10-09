# Display Properties

`desk.cpl` uses the existing Explorer theme engine to apply themes. Themes and Appearance discover files in
`%WINDIR%\Resources\Themes` and `%APPDATA%\Microsoft\Windows\Themes`, including subdirectories. Opening the
theme/style dropdown refreshes discovery without reopening the applet. Removed files are not silently replaced by
Classic when applying a pending selection. Appearance asks each installed `.msstyles` for its actual color and size
names through `EnumThemeColors` and `EnumThemeSizes`; changing a color re-enumerates its size choices. Classic color
choices come from installed Classic `.theme` files. Invalid or missing visual styles report failure before applying.

The VM's installed themes are Windows XP, Windows XP (Olive Green), Windows XP (Silver), Windows Classic, and
Windows Server 2003. Luna exposes Default (blue), Silver, and Olive Green, each with Normal, Large Fonts, and Extra
Large Fonts. The repository also has a Royale theme description, but the VM does not have a Royale visual style:
descriptions in the repository are not treated as installed styles.

Applying Appearance writes an ordinary per-user `WineExplorer Custom.theme`, preserving the current theme's other
sections and the live wallpaper. Applying Themes sends the selected file to `Shell_TrayWnd` with `WM_COPYDATA`,
identifier `0x57455448`, and a bounded, terminated UTF-16 path. The shell validates the file and runs `theme_apply`.
Only dirty pages apply; changing resolution does not accidentally reapply a theme or wallpaper.

## Samples

XP was checked in the Windows XP Pro VM and against the reference `themeui` / Explorer sources. XP itself uses
static taskbar illustrations: Explorer bitmap 145 for auto-hide, otherwise 146 plus 1 when unlocked, 2 when grouping
is disabled, and 4 when Quick Launch is disabled. Notification area uses 180 plus 1 without the clock and 2 without
inactive-icon hiding; Start Menu uses 170/171. Those genuine resources now draw at their native size and refresh
with the checkboxes. The resource pack must be installed to use them; the existing open-resource fallback remains.

Display's Desktop and Settings use the original 185x163 monitor bezel, with the screen drawn separately. Desktop
renders the pending picture and placement through WIC; Settings draws its monitor number. Themes and Appearance
draw the selected visual style's window frames, caption buttons, scrollbar, and push button rather than a generic
approximation. Classic uses GDI controls. Theme samples include wallpaper and the shell's Recycle Bin icon.

`uxtheme` ordinal 16, `OpenThemeDataFromFile`, opens classes from an explicit theme file without activating it.
Theme system metrics now belong to that handle's file, and applying a previously parsed preview still applies its
system colors/fonts. Each preview owns and releases its file, class, and wallpaper handles.

## Resolution backend

The Settings page queries `wexhost.WexDisplayGet` for the active XRandR output and advertised modes. It does not add
preset resolutions. `wexhost` loads Xlib/XRandR at runtime, uses its own connection, and re-queries native output and
mode IDs before changing a mode. The UI merges equal dimensions and chooses the existing refresh rate or the closest advertised rate to
60 Hz. Color depth is shown as the actual current depth; unsupported depth and adapter controls are disabled.

On Apply, the host mode changes first. The applet pumps messages while Wine updates its virtual display and the
shell resizes and redraws. Once the desktop and taskbar agree on the new dimensions, a separate confirmation dialog
starts its 15-second deadline. No, Escape, closing the dialog, timeout, or readiness failure restores the previous
host and Wine modes. Yes persists the Wine mode and an XDG per-user display profile. The session launcher restores
the accepted host mode before creating Wine's desktop; XRandR validates it against the current monitor.

Wine's virtual desktop originally caps its synthetic mode list at the host's starting size. Querying the host allows
larger supported modes to be selected. Explorer now forwards `WM_DISPLAYCHANGE` through Wine's original desktop
procedure and lets its desktop view paint the shell surface. The X11 driver permits a virtual desktop resize without
waiting for fullscreen hints to be acknowledged by a nonexistent host window manager.

This backend handles one active XRandR output, including an Xwayland output when its compositor exposes mutable
RandR modes. Direct X11 is tested. Gamescope is not installed in the current VM, so its runtime behavior is not yet
verified; changing an Xwayland surface does not necessarily change the compositor's physical display. Pure Wayland
needs compositor output-management support, not a universal D-Bus display service. When the host backend cannot
handle a session, the applet uses only modes enumerated and tested by Wine's display driver. Multi-monitor physical
configuration and rollback if the entire CPL process is forcibly killed remain future work.

## Validation (2026-10-09)

- Targeted builds of Explorer, desk.cpl, uxtheme, wexhost PE/Unix, and winex11 succeeded.
- Blue, Olive Green, Silver, and Classic applied in the VM; the desktop was restored to XP Blue / Normal afterward.
- Adding and removing a theme refreshed the open list. Adding a temporary installed visual style refreshed the
  Appearance list and exposed its actual color/font choices. Temporary discovery files were removed after testing.
- `scripts/theme-preview-probe.c`: 13 checks passed, covering independent preview colors/fonts, unchanged live
  metrics, applying an already parsed file, restoring the original style, and file/class reference ownership.
- 800x600 -> 1024x768: X11 desktop, shell view, and taskbar resized. Explicit No and timeout restored 800x600;
  Yes persisted 1024x768 across a graphical-session restart. The VM was restored to accepted 800x600 afterward.
- Monitor asset SHA-256 matches the reference file. `git diff --check` passed. The repository-wide asset check
  reports the unrelated, pre-existing unlisted `wine/dlls/intl.cpl/intl.ico`; the new monitor asset is cataloged.

Independent DRM scanout validation and Gamescope prerequisites are recorded in
[DISPLAY_OUTPUT_TESTING.md](DISPLAY_OUTPUT_TESTING.md).
