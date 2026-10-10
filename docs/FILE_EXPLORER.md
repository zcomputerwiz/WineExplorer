# File Explorer: XP parity plan

Compared on 2026-10-09: Windows XP Pro VM (My Computer, My Documents) against the WineExplorer VM.
The folder window is `programs/explorer` (`explorer.c`, `browser_ui.c`, `browser_taskpane.c`). The file view and
menus inside it are Wine's shell32 `IShellView` (`shlview.c`, `shlview_cmenu.c`, `new_menu.c`, `shlfileop.c`). The
list itself is comctl32's list view.

## Works today

- Navigation through the address bar, Back/Forward/Up, Other Places and the Folders tree.
- Item menus with Open, Print, Cut, Copy, Create Link, Delete, Rename and Properties.
- Copy and paste into another folder, and New Folder from the task pane.
- Details view with columns, and the Details task-pane section.

## Done (stage 1, `afbfde1`)

Items 1, 2, 3 and 6 below. Create Shortcut and Paste Shortcut work and are named "Shortcut to X". New items are
selected for renaming. Not in New yet: Shortcut (XP's wizard), Briefcase, Compressed (zipped) Folder (needs a zip
folder handler).

## Gaps (in work order)

| # | Area | XP | Wine now | Where |
|---|---|---|---|---|
| 1 | Background menu | View ▸, Arrange Icons By ▸, Refresh, Paste, Paste Shortcut, Undo, New ▸, Properties | Line up Icons, Refresh, Paste, Paste as Link, Properties. The submenus are lost because the shell view asks for IDs from 0 and `MENU_002` submenus lie outside that range. | `shell32.rc` `MENU_002`, `shlview.c` `ShellView_DoContextMenu`, `shlview_cmenu.c` `BackgroundMenu_*` |
| 2 | New ▸ | Folder, Shortcut, then the `ShellNew` types (Text Document, Bitmap and so on) | Not shown; `new_menu.c` exists but the view does not use it | `shlview.c` / `new_menu.c` |
| 3 | Paste into the same folder | Creates "Copy of X", then "Copy (2) of X" | Does nothing: `SHFileOperation` refuses to copy a file onto itself | `shlfileop.c` |
| 4 | Item menu (Send To, Open With done) | Open, Edit, Print, Open With ▸, Send To ▸, Cut, Copy, Create Shortcut, Delete, Rename, Properties | Done except Edit (verb of the file type) | `shlview_cmenu.c`, `shell32.rc` |
| 5 | File and Edit menus (done) | Undo, Cut, Copy, Paste, Paste Shortcut, Copy To Folder…, Move To Folder…, Select All, Invert Selection | Done: the File menu is the item menu of the selection with the folder's New ▸ (as XP); Edit has Paste Shortcut, Copy To Folder…, Move To Folder… (Copy Items / Move Items dialogs) | `browser_ui.c` |
| 6 | File extensions | Known extensions hidden (`HideFileExt=1`) | Shown | default in `wine.inf` |
| 7 | Names (done: desktop.ini LocalizedResourceName on Documents, Pictures, Music, Videos) | "My Documents" from every entry point | Window titled "Documents" when opened from the desktop icon | desktop icon target / namespace |
| 8 | Views (Tiles, Thumbnails and per-folder views done) | Thumbnails, Tiles, Icons, List, Details; My Documents and My Computer default to Tiles | Tiles not implemented in comctl32 (`LV_VIEW_TILE` FIXME); no Thumbnails | `comctl32/listview.c`, `shlview.c` |
| 9 | My Computer (done; groups not in Details) | Groups (Files Stored on This Computer, Hard Disk Drives, Removable Storage); "Local Disk (C:)"; Control Panel only under Other Places | Flat list, "(C:)", Control Panel listed with the drives | `shfldr_mycomp.c`, list view groups |
| 10 | My Documents layout | Contains My Music, My Pictures and My Videos | Done: the folders stay in the profile (modern layout); Explorer puts folder shortcuts with the XP names and icons into My Documents | `shellfolders.c`, `shlfolder.c` |
| 11 | Task pane (done) | Folder tasks include Publish to the Web and Share this folder (grayed or hidden here) | Make a new folder only | `browser_taskpane.c` |
| 12 | Undo (done) | Undo of rename, move, copy and delete | None | `browser_ui.c` / shell32 |
| 13 | Taskbar grouping (done) | "2 Windows Explorer" when crowded | One button per window | `taskband.c` |
| 14 | Toolbar (Go done) | The Go button has its green arrow | Text only | `browser_ui.c` |

Checked: Ctrl+A/C/X/V, Delete to the Recycle Bin with the XP confirmation, F2, file Properties (XP General page), drag and drop onto folders, type-ahead, Search (in the folder shown, also through folder shortcuts). Not checked yet: drag and drop between
windows and the desktop, F2 rename in place, keyboard navigation and type-ahead, file Properties pages, Search.

## Stages

1. Menus and paste: items 1-7.
2. Views: item 8 (Tiles in comctl32, Thumbnails), item 9 and per-folder default views.
3. Undo, task pane, toolbar and grouping: items 11-14, then the unchecked areas above.

Also done: New > Shortcut with XP's Create Shortcut wizard (appwiz.cpl NewLinkHere), shell views that follow
changes made by other programs (folder watcher), XP Properties page, drag and drop into folders, taskbar grouping.

Also done: Picture/Music/Video Tasks (Set as desktop background, Play all; slide show, prints and the online stores
grayed), Shared Pictures/Music/Video in Other Places, a scrolling task pane, live wallpaper changes.

Also done: View as a slide show (full screen, 5 seconds a picture, arrows/click/Esc), the picture's preview and
dimensions in Details, the visual style's task icons and watermarks (shellstyle.dll) on the media task groups
and in the corner of their folders, XP's 210 pixel task pane.

Left for later: details view groups, the drives' free space line in Tiles, Undo of New Folder.
Also done: Filmstrip (picture folders), XP's thumbnail selection frame, folder thumbnails with their pictures.
