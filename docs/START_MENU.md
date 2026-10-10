# Start menu, taskbar and notification area settings

The shell offers the two Start menu styles of Windows XP, chosen in *Taskbar and Start Menu Properties*
(right-click the Start button, Properties): **Start menu** (two columns) and **Classic Start menu**.
The dialogs are the ones of the Windows shell (`explorer/dlg.rc`), including the preview pictures, which come from the
genuine resource pack (`xpres_explorer.dll`, ids 170 and 171).

## Linux applications

Programs / All Programs now contains **Linux Applications** in both styles. GIO discovers visible `.desktop` entries, using localized names and the normal NoDisplay, Hidden, TryExec and desktop-visibility rules. The list is sorted and rebuilt each time it opens, so installation/removal does not require restarting Explorer. Labels display literal ampersands.

Selection launches the desktop ID through `WexAppsLaunch`, not a Windows shortcut or a shell command. Directly spawned clients retain display/session variables but receive none of the desktop runtime's `WINE*` variables. Steam and other launchers can select their own Proton runtime and prefix. Explicit environment settings in a desktop entry still apply; D-Bus activation uses the service activation environment. Launch failure shows an error dialog. GIO is loaded at runtime; its absence hides the submenu.

The first version is a flat list, capped at 512 entries. Category folders, application-specific menu icons and native pinned/MFU integration remain future work. Technical details and verification are in [LINUX_WINDOWS.md](LINUX_WINDOWS.md); `scripts/linux-apps-probe.c` exercises the API independently of the menus.

## Where things are stored

| What | Where |
| --- | --- |
| Style (0 classic, 1 two columns) | `HKCU\...\Explorer\Advanced\Start_Style` |
| *Customize Start Menu* options | `HKCU\...\Explorer\Advanced\Start_Show*`, `Start_MinMFU`, `Start_LargeMFUIcons`, `Start_NotifyNewApps` (names of Windows XP) |
| Internet / E-mail shown, submenu delay | `HKCU\Software\WineExplorer\StartMenu` |
| Pinned programs | `HKCU\Software\WineExplorer\StartMenu\Pinned` (`0000` = `path|title`) |
| Launch counts of the most used list | `HKCU\Software\WineExplorer\StartMenu\Mfu` (full path, lower case, or file name for programs in the Windows directory) |
| Notification area behavior per icon | `HKCU\Software\WineExplorer\TrayIcons` (`program|id` = 0 hide when inactive, 1 always hide, 2 always show) |
| Toolbar order, widths, added toolbars | `HKCU\...\Explorer\Taskbar\Toolbars` (`Order`, `<name>Width`, `Custom<n>Folder`) |

## Options shown but not available

Shown greyed like the options Windows has but that need things Wine does not have: *Display as a menu* for My
Computer, My Documents, My Music, My Pictures and Control Panel; *Favorites menu*; *Enable dragging and dropping*;
*Scroll Programs*; *System Administrative Tools*; every option of *Customize Classic Start Menu* (Add... and
Advanced work, Remove... and Sort are disabled); the program Properties item of the Start panel.

## Taskbar

Task buttons own local copies of their window icons. Separate-process Explorer
folders use their published X11 icon pixels when the Wine icon handle cannot be
copied, preserving My Computer/Documents/Control Panel icons and updating during
navigation. Start menu flyout and Classic highlights draw the original icons
with their mask/alpha instead of inverting their colors. See
[LINUX_WINDOWS.md](LINUX_WINDOWS.md) and [THEMES.md](THEMES.md).

Notification background spacing accounts for the theme's left content margin
even when hiding is disabled or no chevron is needed. Hiding off, hiding on,
collapsed and expanded chevrons were checked in the live Luna session; icons
remain in the cyan area without overlapping each other, the clock or task band.
See [SYSTEM_TRAY.md](SYSTEM_TRAY.md) for settings and verification.

* The taskbar menu (right-click) is the one of the shell: Toolbars (Address, Links, Desktop, Quick Launch, added
  toolbars, New Toolbar...), Cascade, Tile, Show the Desktop, **Undo** of the last Cascade/Tile/Minimize All, Task
  Manager, Lock the Taskbar, Properties. Right-click on the notification area or clock adds Adjust Date/Time and
  Customize Notifications.
* An unlocked taskbar lets you drag the gripper of a toolbar: it changes the width, or the place when the pointer
  passes a neighbor.
