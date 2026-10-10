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
| 5 | Edit menu | Undo, Cut, Copy, Paste, Paste Shortcut, Copy To Folder…, Move To Folder…, Select All, Invert Selection | No Undo, Paste Shortcut, Copy To or Move To | `browser_ui.c` |
| 6 | File extensions | Known extensions hidden (`HideFileExt=1`) | Shown | default in `wine.inf` |
| 7 | Names (done: desktop.ini LocalizedResourceName on Documents, Pictures, Music, Videos) | "My Documents" from every entry point | Window titled "Documents" when opened from the desktop icon | desktop icon target / namespace |
| 8 | Views | Thumbnails, Tiles, Icons, List, Details; My Documents and My Computer default to Tiles | Tiles not implemented in comctl32 (`LV_VIEW_TILE` FIXME); no Thumbnails | `comctl32/listview.c`, `shlview.c` |
| 9 | My Computer | Groups (Files Stored on This Computer, Hard Disk Drives, Removable Storage); "Local Disk (C:)"; Control Panel only under Other Places | Flat list, "(C:)", Control Panel listed with the drives | `shfldr_mycomp.c`, list view groups |
| 10 | My Documents layout | Contains My Music, My Pictures and My Videos | Done: the folders stay in the profile (modern layout); Explorer puts folder shortcuts with the XP names and icons into My Documents | `shellfolders.c`, `shlfolder.c` |
| 11 | Task pane | Folder tasks include Publish to the Web and Share this folder (grayed or hidden here) | Make a new folder only | `browser_taskpane.c` |
| 12 | Undo | Undo of rename, move, copy and delete | None | `browser_ui.c` / shell32 |
| 13 | Taskbar grouping | "2 Windows Explorer" when crowded | One button per window | `taskband.c` |
| 14 | Toolbar | The Go button has its green arrow | Text only | `browser_ui.c` |

Not checked yet: drag and drop between windows and the desktop, Delete to the Recycle Bin and its confirmation
dialog, F2 rename in place, keyboard navigation and type-ahead, file Properties pages, Search.

## Stages

1. Menus and paste: items 1-7.
2. Views: item 8 (Tiles in comctl32, Thumbnails), item 9 and per-folder default views.
3. Undo, task pane, toolbar and grouping: items 11-14, then the unchecked areas above.
