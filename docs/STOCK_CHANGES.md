# Changes to stock Wine / third-party code

Anything under `wine/` that is not new project code, and what we changed. Keep this list short: every entry makes
rebasing on upstream Wine harder. Refactor baseline = the real `wine-11.19` tag
(`455e3509b98a6919fd4ad1def4803e08c41c03b2`); see `REFACTOR_MAP.md` for the audit.
Commit `40c17e1` is the imperfect upstream import, which also includes post-tag upstream changes.

P4 established Wine as a gitlink to d0a0af0681 on wineexplorer/11.19.
Current pin is 71572805, including accepted opt-in Wayland L1 integration
and concurrent visual-style support. See WAYLAND_L1.md; X11 remains the default.
Project CPLs/programs, wexhost and theme definitions live under src.
L2 opt-in topic codex/wayland-l2 (c6dac92, including 180d405, not the production pin) extends the
private protocol through v10, starting with v4 native/controller atom properties and an
asynchronous reservation status. winewayland serializes requests and maps owned
native proxy HWNDs as independent toplevels while keeping ordinary owned-window
behavior. The standard wlr XML is vendored only to satisfy private protocol
interface definitions, with its original notice. Reservation leaves native
composition/focus unchanged. Explicit HOST opt-in now uses v7 numbered group
configuration and focus acknowledgements, preserving logical foreground during
native input and guarded temporary frame focus. Client size changes detach
before retry; callers must exclude the client area from Win32 hit testing.
Private hosting changes are fork-only. Compositor topic e8e5a083 provides v5 active scene grouping
with frame-ring render/input exclusion and disconnect recovery, plus v6 observed
physical focus acknowledgements and guarded frame/native restoration, plus v7
numbered configuration and native size/policy recovery. Driver fallback for old
servers remains. Owned wexhost now binds explicit SSD hosts through the v7
properties with UI-thread lifetime, region and focus-loop management; Explorer
adoption is now gated by WEX_WAYLAND_NATIVE_WINDOWS=1 for isolated tests; AUTO
remains disabled. The compositor also publishes replacement decoration policy
atomically, avoiding a false CSD notification during activation. It now observes
mapped xdg-toplevel parent changes with a lifetime-managed listener and publishes
them through the standard foreign-toplevel interface. Explorer detaches native
child proxies before destroying their owner frame, preserving native lifetimes.
These owner-lifetime fixes add no further Wine fork changes. See WAYLAND_L2.md
for 32/64 verification and worktree paths; L2 remains separate from production.

L2 dd18609 adds a fork-only in-process uxtheme/bridge region contract. A borrowed
outer HRGN and a scoped mask-update flag preserve automatic theme-region ownership
while wexhost excludes the native client. The HRGN is read only on the owning UI
thread; neither property is serialized to the driver/compositor. Existing explicit
application regions keep precedence. Owned code retains compatibility definitions
so master still builds at its production Wine pin. That pin does not gain this
runtime feature until the L2 topic is integrated. In 3fd4865, hosted minimized
frames retain normal content instead of uploading iconic-caption buffers; group
geometry ignores iconic dimensions, readiness can retry after a buffer-size
change, and activation clears the preceding epoch's focus observation. Restore
is explicit before requesting native focus. Compositor e7be9560 rejects focus
requests for minimized groups, preventing a delayed request from reopening them.
An active group's state is driven once through its Wine frame; wexhost's separate
control connection remains the fallback outside an active group. No new protocol
version, WEX_NATIVE_WINDOW field, Unix-call index or public Windows API is added.

The subsequent b971d96 synchronizes the v9 constraint XML. V9 adds a maximum-size
event to native observation, not native resize requests; the driver keeps binding
v8. Compositor 2e88585f includes maximum dimensions in shared size hints, reports
them only to v9 observers, and detects committed XDG constraint changes without
requiring a move/repaint. For wlroots 0.18's missing WM_NORMAL_HINTS notification,
XWayland uses the public XWM event hook and a per-view deferred notification after
wlroots parses the property; pending notifications are removed on destruction.
No X event is consumed or parsed independently, and existing window-size policy
is unchanged. Owned wexhost fills existing max_size fields, preserves state
transitions when coalescing geometry events, and wexshell applies independent
axis limits in WM_GETMINMAXINFO. See WAYLAND_L2.md for compatibility checks.

180d405 synchronizes the v10 private XML across Wine, wexhost and compositor.
Wine's driver still binds v8 and bridge observers bind v9; there is no new Wine
runtime behavior at this checkpoint. Compositor dd65ccef adds serial-specific
XDG hosted resize requests, constraint acceptance, configure acknowledgement,
native-buffer readiness and matching-frame publication. Public scene/buffer
APIs retain old root buffers until atomic handover; supersession and bounded
rollback retain the original geometry. Focus/capture loss starts rollback;
failed rollback restores ordinary native composition. Snapshot listeners and
references are released on either window's destruction. XWayland resize requests
are explicitly unavailable, pending a separate native completion path. The
production pins, WEX ABI, adoption gate and sizing affordances remain unchanged.
The following synchronization merges Gemini's production 71572805 into the L2
topic as c6dac92; it adds no further Wayland behavior. Master keeps the incoming
prefix-split S0/S1 merge and its new pin. Those installer/session/stock changes
are synchronized from their owner rather than edited by this L2 work.
Compositor e8e5a083 requires a content-bearing native commit after acknowledgement
(buffer, damage, viewport, scale or transform); committing only acknowledged xdg
state cannot incorrectly report native readiness. A deterministic before/after
probe covers that separation. b41b65f fixes shared-header dependencies in the
compositor test-client build so window-layout changes rebuild every client.

The L2 move checkpoint adds fork-only v8 frame pointer capture and its Wine
SetCapture hook, guarded by the current focus epoch and active HOST state.
Hosted Move uses Wine's loop only with v8; zone placement echoes are ignored
for these proxies because native observation owns reconciliation. Generic
Wine fixes retain keyboard/menu move loops before a button press, terminate
them on capture loss, and commit pure zone moves without waiting for repaint.
Those generic fixes can be extracted for upstream review; no MR is opened.
The compositor routes pointer warps through normal hit testing with xdg surface
geometry offsets. Explorer activates task-menu Move/Size targets and separates
requested geometry from older observations. Production 2e8bcfa carries only
the shared property definitions needed to compile the existing owned bridge;
the L2 topic merges that header fix. Wine's consumption of resize negotiation
remains pending; the compositor's XDG transaction is implemented separately.
Display Properties, wireless UI and media player replace stock stub modules
through explicit assembly entries, with pristine originals preserved.
Those implementations are project modules rather than source patches in
the retained fork. VM sync resolves the gitlink separately from owned source.

P3b project modules now include wexres (shared resources), themeui (shared theme
engine, used directly by desk.cpl) and msgina (Turn Off/Log Off, called directly
by logonui), plus stobject's registry-driven COM volume/power tray service.
Netshell now lives under src/dlls, owns network tray/status/properties/backend,
and exposes the shared UI to ncpa.cpl. Wireless selection lives in wzcdlg and
is loaded through a typed export by the shell/netshell. Wine's boot DLL registration list includes
stobject and netshell; the shell enumerates
ShellServiceObjectDelayLoad and tolerates missing optional DLLs/services.
Their implementation and interfaces live under src/. Configure
and header installation entries are retained on wineexplorer/11.19 through
d0a0af0681. Search now lives in optional wexsearch with an opaque UI interface;
its class/resource ownership, file search, cancellation and absent-module
checks pass. Browser windows now live in optional wexbrowse behind one
string/flag entry point; taskbar toolbars remain core. See refactor/P3B.md
for extraction acceptance. The complete repository/build/install acceptance
is in refactor/P4.md; P5 assessment keeps unsupported extension replacements
as X commits. The separately pushed P6 candidate passes full build/runtime
smoke without changing the production pin (REBASE.md).

## In place of stock behavior (deliberate)
| Area | Change | Gate |
|---|---|---|
| `dlls/winewayland.drv` xx-zones-v1 | optional shared per-socket coordinate zone; volatile registry token supports independently launched processes; map/move positioning and feedback with surface scaling and tracked xdg geometry offset | only when compositor advertises xx_zone_manager_v1; upstream/winewayland-zones aed8b9599c + 01607e166c; retained 853c5b02c7 + 1bab2dda87 on codex/wayland-integration; seven placements match X11 exactly at 100% and within 1 px at 150% |
| `dlls/winewayland.drv`; `include/wexwayland.h` | privileged layers, shared-handle cross-process relative stacking, native activation hook and minimized restore; generic scalar role/edge/size properties, no class checks | optional wex_window_manager_v1; ce019d67c7 + header correction 996f87f33f; activation/configure a83412b0ed; maximized work-area borders e3be0174be; shared atoms 97eb62958c; focus acknowledgement 89735ec1ef; background restore/logical stacking 624720e2b6; native focus loss with stale-event guard ea0654efdb; v1 same-connection fallback; v2 compositor 9a47d74 passes 26/26 |
| `dlls/win32u/message.c` | skip implicit click activation of a WS_EX_NOACTIVATE root | generic Win32 style fix 8f1717c8f3; verified private Wayland and X11; upstream baseline incorrectly activated it |
| `programs/explorer/desktop.c`, `explorer.c` | /shell without /desktop starts the plugin on the driver's desktop and skips display mode changes | explicit /shell only; d7d46937a3 included in accepted 05286f7c90; native Wayland shell passes |
| `programs/explorer/desktop.c`, `explorer.c`; `include/wexshell.h` | load the versioned wexshell.dll plugin for the Windows-style shell and route its browser/applet commands; implementation lives in project modules under `src/dlls` (wexshell, wexres, themeui, msgina), while the stock host retains desktop creation, clipboard, COM registration and its message loop | shell startup only when `EnableShell=1` (`HKCU\Software\Wine\Explorer\Desktops\<name>`), named `shell`, or requested with `/shell`; missing/incompatible plugin uses stock behavior |
| `programs/explorer/desktop.c` | `/shell` switch (before the program to run) enables the shell for that desktop; used by the Winlogon-style session script | only when given |
| `programs/explorer/explorer.c` | `/wlan`, `/netstatus`, `/netprops` helper switches; `::{CLSID}` paths and `/e` accepted | token-based, only when given as arguments |
| `dlls/desk.cpl` | theme/background/appearance/settings pages added in front of Wine's Settings page; installed theme discovery, isolated style previews, direct shared themeui Apply, host-supported modes and keep/revert confirmation (see DISPLAY.md); themeui now persists Classic colors before cross-process reload | always |
| `dlls/uxtheme` (`msstyles.c`, `msstyles_v4.c`, `draw.c`, `property.c`, `msstyles.h`) | Binary Vista-format VARIANT parsing, typed properties, WIC PNG decoding, DPI variants and atlas regions; ApplyTheme reads typed SysMetrics | always; reviewed fork through f0108dc; standard-only four-commit upstream candidate c16f0bda39..43c139255b, including fixes; full MIT notice, no unused generated tables; source-built tests pass both architectures |
| `dlls/uxtheme/system.c` | MUI/filename documentation and fabricated scheme display-name fallbacks for installed styles lacking localized strings | fork-only convenience behavior; excluded from the cleaned upstream series and its assertions |
| `dlls/uxtheme` (`system.c`, `msstyles.c`, `metric.c`) | implement private ordinal 16 `OpenThemeDataFromFile`; metrics follow the supplied theme handle; activating a preview applies metrics even when its classes are already parsed | always |
| `dlls/uxtheme/msstyles.c` | convert non-client theme font heights from points to 96-DPI pixels; trim token whitespace before a comma (`MSSTYLES_GetNextToken`) | always; token trimming is an independent upstream candidate |
| `dlls/uxtheme/system.c`, `uxthemedll.h` | watch ThemeManager registry changes and reload the active theme in other processes without reapplying global metrics | theme queries/opening theme data |
| `dlls/winex11.drv/window.c` | virtual desktop resize does not wait for fullscreen hint acknowledgements; standalone shell sessions have no host window manager to supply them | virtual desktop root window only |
| `programs/wmplayer` | Wine's stub `main.c` replaced by ReactOS mplay32 | always |
| `dlls/shdoclc` | icon 190 (Wine's `iexplore.ico`): `iexplore.inf` names it as the Internet Explorer desktop icon, stock Wine has none | always |
| `programs/calc`, `programs/mspaint` | new programs (ReactOS calc; own mspaint) | always |
| `dlls/ntdll/unix/system.c` | `SystemPowerCapabilities.SystemBatteriesPresent` uses Wine's existing `fill_battery_state` helper with zero-initialized state; `SystemBatteryState` and its sysfs implementation are already stock in wine-11.19 | always (replaces the hard-coded FALSE capability; no-battery state remains FALSE) |
| `server/fd.c` | recompute the DST-adjusted timezone bias when DST starts/ends (a long-running session never saw it before) | always (bug fix, upstreamable) |
| `dlls/ntdll/unix/system.c` | drop the cached zone info when `/etc/localtime` changes | always (bug fix) |
| `dlls/kernelbase/locale.c` | preserve a locale selected through Regional and Language Options while the remembered Unix locale remains unchanged; a changed Unix locale becomes the new default | always; upstream suitability requires review in P0 |
| `dlls/user32/cursoricon.c` | load system cursor schemes from `HKCU\Control Panel\Cursors` and cache the file-backed cursors, falling back to built-ins | system cursor resource loads; cache/refresh semantics require review in P0 |
| `dlls/win32u` (`window.c`, `input.c`, `defwnd.c`), `dlls/user32/misc.c` | `RegisterShellHookWindow`/`DeregisterShellHookWindow` implemented: the desktop's taskman window gets the registered `SHELLHOOK` message for window created/destroyed/activated/redraw/flash | always (no cost without a registered window except one server call per event) |
| `dlls/mmdevapi` (`audiovolume.c`, `unixlib.h`), `dlls/winepulse.drv` | `IAudioEndpointVolume`: master volume and mute of the default device through the driver (PulseAudio/PipeWire); two new unix calls `get/set_endpoint_volume`; other drivers (alsa, oss, coreaudio) return E_NOTIMPL; the wow64 table of winepulse has stubs, so 32-bit callers get E_NOTIMPL for now | always |
| `dlls/winex11.drv/window.c` (`sync_window_position`), `event.c` (`set_input_focus`) | in a virtual desktop, a window that is not the top one is restacked in X directly below the window it follows in the Win32 z-order (stock only ever raises, for the sake of window managers), and focusing a window no longer raises it in X; without it an activated window covers topmost ones such as the taskbar | only in a virtual desktop, unmanaged windows |
| `dlls/winex11.drv/event.c` (`X11DRV_ActivateWindow`) | in a virtual desktop the window that becomes active is raised to the top of the Win32 z-order, as Windows does on activation (Wine leaves it to the X window manager, and the row above removed the X raise); the X stacking follows, below topmost windows. Before, clicking a window or its taskbar button activated it without bringing it to the front | only in a virtual desktop, unmanaged windows |
| `dlls/win32u/sysparams.c` (`SPI_SETWORKAREA`/`SPI_GETWORKAREA`) | the work area is the monitor's and every process sees it, like on Windows and ReactOS (stock kept it per process, so windows maximized in other processes covered the taskbar): it is written to the monitor's registry key, the monitor serial is bumped so other processes reload, and a volatile copy in `HKLM\Software\Wine\WorkArea` (by monitor path) is applied again whenever the monitors are rebuilt from the driver, which every process entering a virtual desktop does; it is dropped once it no longer fits the monitor. `SPI_GETWORKAREA` reads the primary monitor every time. As on Windows, the rectangle has to lie on one monitor, and a non-zero uiParam (the taskbar passes TRUE) fits the maximized windows of that monitor to the new work area | always (only matters once something sets a work area) |
| `dlls/shell32` (`shell32.rc`, `shresdef.h`, `shell32.rgs`) | Windows XP names in the namespace: string 8964 "Recycle Bin" (was "Trash", with the delete prompts worded like XP), new string 9227 "My Documents" as the `LocalizedString` of CLSID_MyDocuments (string 46 stays the folder name "Documents") | always |
| `dlls/shell32/shell32.rc` | icon 32 (`IDI_SHELL_EMPTY_RECYCLE_BIN`, Wine's own `trash_file.ico`): stock Wine defines the id and returns it for `SIID_RECYCLER`, but has no such icon, so an empty Recycle Bin was drawn blank | always (bug fix, upstreamable) |
| `dlls/shell32/iconcache.c`, `shellord.c` (`FileIconInit`) | shell32's own icons can be replaced through `HKLM\...\Explorer\Shell Icons` like on Windows and ReactOS (stock Wine only did it for overlays): value `n` replaces icon index n, an icon asked for by resource id stands for index id - 1 for the first 54 icons (no gaps there), and beyond that a value named after the negative id (`-137`) is used, which Windows has no use for. A replacement that cannot be loaded falls back to shell32's icon. `FileIconInit(TRUE)` (was a stub) reloads the cached shell32 icons in place so a theme change shows without new image indexes. Explorer writes the values from the theme's icon pack | always (only matters when Shell Icons has values) |
| `server/winstation.c` (`remove_desktop_user`), `include/ntuser.h` | new Wine desktop flag `DF_WINE_SHELL_DESKTOP`: a desktop with it is not timed out when only its owner is left (stock wineserver unlinks the desktop's name and posts `WM_CLOSE` then; with a shell that happened whenever the shell was alone and one of its threads ended, and new programs could no longer open the desktop). Explorer sets it in shell mode | only on desktops that have the flag |
| `dlls/shell32/folders.c` (`IExtractIconW::GetIconLocation`) | a shortcut without an icon of its own shows the icon of its target, as on Windows and ReactOS (stock Wine showed the blank file icon, e.g. for the Start menu shortcuts in search results); shortcut targets use the default fallback to prevent recursive icon lookup through cycles | always (bug fix, upstreamable) |
| `dlls/comctl32/propsheet.c` (`PROPSHEET_SetWizButtons`) | when the focused wizard button ends up disabled or hidden (Next swapped for Finish), the focus moves to the default button; before, nothing had the focus on a wizard's last page and Enter did nothing | always (bug fix, upstreamable) |
| `dlls/comctl32/imagelist.c` (`add_dib_bits`) | an image without alpha clears the item's alpha flag; before, replacing a 32-bit image with one without alpha (`ImageList_ReplaceIcon`) kept the flag and the new image drew fully transparent | always (bug fix, upstreamable) |
| `dlls/shell32/shellord.c` (`SHFindFiles`) | opens the Search Results folder (`explorer.exe ::{E17D4FC0-...}`, whose window shows the Search Companion) like Windows and ReactOS, honoring `REST_NOFIND`; was a stub returning FALSE. The folder to look in and saved searches are not passed yet | always |
| `dlls/user32/user32.rc` (`SYSMENU`) | the window menu ends with Close, as on Windows: Wine's "About Wine" item and its separator are removed (the `SC_ABOUTWINE` handler stays, unused) | always |
| `dlls/shell32` (`shlview_cmenu.c`, `new_menu.c`, `shlfileop.c`, `shlview.c`, `shell32.rc`) | Windows XP folder background menu (View, Arrange Icons By, Refresh, Paste, Paste Shortcut, New, Properties); New lists Folder and the ShellNew file types and starts renaming the new item; `FOF_RENAMEONCOLLISION` names copies "Copy of X"; Create Shortcut / Paste Shortcut implemented; items added by path notifications are read again for sizes and dates; notifications for items outside the folder (they are recursive) are ignored and a move in or out shows as an add or a removal; the background menu takes the "pastelink" verb | always |
| `dlls/shell32` (`shlfolder.c` `SHELL32_BindToChild`, `folders.c`) | folder shortcuts work as on Windows: a folder whose desktop.ini names `CLSID_FolderShortcut` opens the folder its `target.lnk` points to; a desktop.ini `IconFile` is used (it was read and dropped) | always |
| `dlls/shell32` (`shfldr_fs.c` display names, `shlview_cmenu.c` Send To) | a folder is shown by its desktop.ini `LocalizedResourceName` (not for parsing or editing), as Windows shows "Documents" by another name; the item menu has Send To (Desktop (create shortcut), My Documents, SendTo-folder shortcuts, removable drives) | always |
| `dlls/shell32` (`brsfolder.c`) | SHBrowseForFolder returns the target of a folder shortcut unless `BIF_NOTRANSLATETARGETS`, as Windows does | always |
| `dlls/shell32` (`shlview.c` keys, `shlfileop.c`, `shell32.rc` confirmations) | Ctrl+A, Ctrl+C, Ctrl+X and Ctrl+V in the shell view; the delete confirmations use the XP captions (Confirm File Delete, Confirm Folder Delete, Confirm Multiple File Delete, Confirm File Replace) and name the item by its display name | always |
| `dlls/shell32` (`shfldr_mycomp.c`, `shell32.rc`) | drives without a label are named "Local Disk (C:)", "CD Drive (D:)" and so on, with that Type; columns "Total Size" and "Free Space"; `HideMyComputerIcons` (set for Control Panel in `wine.inf`) hides an item from the My Computer window but not from folder trees | always |
| `dlls/shell32` (`pidl.c`, `shell32_main.c`) | file system folders have the type "File Folder" (Windows wording) | always |
| `dlls/shell32` (`shell32.rc`, `shresdef.h`) | string 21785 "Shared Documents" (the Windows XP id), the LocalizedResourceName Explorer gives Public\Documents | always |
| `dlls/shell32` (`shell32.rc`, `shresdef.h`) | strings 0x7143-0x7145 "Shared Music", "Shared Video", "Shared Pictures" (XP ids), the names Explorer gives Public Music, Videos and Pictures | always |
| `dlls/shell32` (`undo.c`, new; `shlfileop.c`, `shfldr_fs.c`, `recyclebin.c`, `shlview.c`, `shlview_cmenu.c`) | Undo as on XP: SHFileOperation with FOF_ALLOWUNDO and renames in the view record their items; the background menu shows "Undo Rename/Move/Copy/Delete" (verb "undo", Ctrl+Z in the view); undoing a delete restores the item from the trash. The view finds notified items by name, resets edited labels and appends notified items at the end | always |
| `dlls/comctl32` (`listview.c`, also built as comctl32_v6) | LV_VIEW_TILE (v6 only): LVM_SET/GETTILEVIEWINFO (tile size, lines), LVM_SET/GETTILEINFO (per item columns); a tile has the normal icon at the left and the name with its detail lines (gray) next to it, laid out like the small icon view | always |
| `dlls/comctl32` (`listview.c`) | groups (v6) in the icon, small icon and tile views: LVM_ENABLEGROUPVIEW, LVM_INSERTGROUP, LVM_REMOVE(ALL)GROUP(S), LVM_HASGROUP, LVM_SET/GETGROUPINFO, LVM_MOVEITEMTOGROUP, LVIF_GROUPID; each group is laid out in rows under an XP header (blue bold name over a fading line). Not in the details view | always |
| `dlls/comctl32` (`listview.c`) | type-ahead: a space continues a search in progress ("my p" finds My Pictures) | always |
| `dlls/comctl32` (`listview.c`) | LVM_SETBKIMAGE offsets: the image stays at its place in the window (Explorer's folder watermarks in the bottom right corner) and the list draws it again when it scrolls or is resized | always |
| `dlls/comctl32` (`listview.c`) | a selected item keeps its highlight box when the text background is CLR_NONE, as on Windows | always |
| `dlls/shell32` (`shlview.c`, `shell32.rc`, `shell32.rgs`, `shfldr_fs.c`, `shfldr_mycomp.c`, `shellpath.c`, `shell32_classes.idl`) | My Computer lists XP's Files Stored on This Computer (Shared Documents, a new namespace folder with XP's CLSID {59031A47-...}, and My Documents named "<user>'s Documents"), then its drives in XP's groups: Hard Disk Drives, Devices with Removable Storage, Network Drives, Other; drives and folders have no detail lines in Tiles | always |
| `dlls/shell32` (`shlview_cmenu.c`, `shell32.rc`), `dlls/comctl32` (`comctl32.rc`) | file Properties as XP: "test Properties" title (comctl32's PSH_PROPTITLE caption "%s Properties"), Type of file, Opens with (registered program name) and Change... (Open With, always), Size and Size on disk with exact bytes, XP date format, Read-only; the page keeps its icons | always |
| `dlls/shell32` (`shfldr_fs.c`, `changenotify.c`) | drag and drop: a folder item is its own drop target (files take none); Drop moves, copies or makes shortcuts (CF_HDROP from the shell or other programs) with XP's default effect (move on the same drive, else copy; Ctrl copies, Shift moves, Ctrl+Shift links), undoable; change notifications of a rename or move reach the watchers of either item's folder | always |
| `dlls/shell32` (`shlview.c`) | the view watches its folder on disk (FindFirstChangeNotification) and takes the folder's items again when other programs change it: new items at the end, gone ones removed, the selection kept | always |
| `dlls/shell32` (`new_menu.c`), `loader/wine.inf.in` | New > Shortcut: ShellNew Command handlers (HKCR\.lnk\ShellNew "rundll32.exe appwiz.cpl,NewLinkHere %1") come after Folder, before the separator | always |
| `dlls/appwiz.cpl` (`createlink.c`, new; `appwiz.rc`, `appwiz.cpl.spec`, `shortcut.bmp`) | NewLinkHere(A/W): Windows XP's Create Shortcut wizard (location with Browse..., then the name; .lnk for items, .url for Internet addresses); the picture is XP's from the resource pack, else ReactOS' | always |
| `dlls/shell32` (`shlview.c`, `shell32.rc`) | View > Tiles: 48 pixel icons (SHIL_EXTRALARGE) with the type and size of files (none for folders) under the name; icons and tiles are laid out in rows (no LVS_ALIGNLEFT), as on XP | always |
| `dlls/shell32` (`shlview.c`, `shell32.rc`) | View > Thumbnails: 96 pixel framed pictures of images (GDI+), the 48 pixel icon for other items, folders as XP's large folder (the Shell Icons folder icon) holding up to four of their pictures; the list is laid out again after it is filled; a selected thumbnail keeps its picture and gets XP's thick frame of the selection color | always |
| `dlls/shell32` (`shlview.c`, `shell32.rc`) | View > Filmstrip (FVM_THUMBSTRIP) in picture folders: the selected picture large above one row of thumbnails, with previous/next and rotate buttons; rotating saves the picture in its own format (GDI+) | always |
| `dlls/shell32` (`openwith.c`, new; `dialogs.c`, `shlexec.c` stubs removed) | Open With dialog (`SHOpenWithDialog`, `OpenAs_RunDLL`), ReactOS-based: programs from `HKCR\Applications` (FriendlyAppName or version FileDescription), recommended from the type's open command, OpenWithList and OpenWithProgids; "Always use" sets the type's default. The item menu of one file has Open With ▸ (or Open With... when nothing else is recommended) | always |
| `loader/wine.inf.in` (file types) | `Folder\ShellNew`, Bitmap Image (`.bmp` → `Paint.Picture`, opened by mspaint) and Rich Text ShellNew entries; `HideFileExt=1` (no-clobber) as on Windows XP | prefix install/update |
| `programs/iexplore/iexplore.inf` | CLSID_Internet gets its display name "Internet Explorer" (Windows' default value); Wine had none | always |
| `loader/wine.inf.in` | file-type associations (.txt/.bat/.cmd/.log, folder open command); `HKCR\Applications` Notepad, Paint, WordPad and OpenWithList entries for .txt/.bmp/.rtf; the Windows icons of text, configuration, setup and batch files (shell32 -152, -151, -153) | always |
| `programs/control/control.c` | `printers` opens the Printers folder; no argument opens the Control Panel folder in Explorer like Windows (Wine's own Control Panel window only if that fails) | always |
| `programs/shell32`-visible: none | Run dialog placement is done by explorer (CBT hook), not by shell32 | - |
| `loader/wine.inf.in` | `.msc` files open with `mmc.exe` (`MSCFile`); new `[MscFiles]` copies `devmgmt.msc` (a data file of `programs/mmc`) into system32 | always |
| `dlls/ntdll` (`unixlib.h`, `loader.c`, `ntdll.spec`, `unix/unix_private.h`, `unix/loader.c`, `unix/file.c`) | `wine_create_symlink` internal unix call with target canonicalization (`realpath`) to support fake DLL symlinks; client-side copy-on-write symlink break (`check_cow_break_symlink`) in `NtCreateFile` gated on Wine DLL directories (`is_in_wine_dll_dirs`) for data writes, truncate, overwrite, or supersede | internal call / writes to built-in DLL symlinks |
| `dlls/setupapi` (`fakedll.c`) | symlinked fake DLL generation replacing copied fake DLLs with symlinks to Wine's installation tree while preserving native DLLs and WinSxS assemblies; WOW64 path redirection in `get_real_link_dest()` | `WINELINKFAKEDLLS=1` env var or `HKLM\Software\Wine\LinkFakeDlls` (DWORD 1) |
| `server/fd.c` | leaf symlink preservation in `dup_fd_name` / `open_fd()`: ensures `fd->unix_name` retains the leaf symbolic link when opening a symlink, so closing with `FILE_DELETE_ON_CLOSE` unlinks the link rather than the package target | symlinks opened with delete-on-close |

## D2 compositor effects and capture

- `dlls/winex11.drv/window.c`, `x11drv.h`: a private `_WEX_DROPSHADOW` CARDINAL on class-shadow windows carries
  `SPI_GETDROPSHADOW` to the compositor at creation/show. Explorer's out-of-context hook cannot reliably read another
  process's XID. The hint is only written when `_NET_WM_CM_S<screen>` has an owner; no input/shape/rendering changes
  without one. The shared internal `X11DRV_has_compositor` queries ownership on the actual display's default screen.
- `dlls/user32/win.c`: `AnimateWindow(AW_BLEND)` steps temporary `WS_EX_LAYERED` uniform alpha at roughly 16 ms
  intervals for `dwTime`, then restores the layered bit/attributes and the requested visibility. As allowed by the
  D2 spec this path runs without an X11 selection query in user32; intermediate alpha is visible only with a
  compositor (Wine's X11 driver only publishes opacity otherwise). Other animation flags retain the partial-stub
  behavior. Existing per-pixel layered windows fall back to immediate show/hide to preserve their bitmap; uniform
  alpha and color-key layered windows retain their attributes. Automatic popup menu fades are deferred: win32u's
  `show_popup` cannot call user32's `AnimateWindow`, and a shared fade/compositor gate would broaden the stock change.
- `dlls/winex11.drv/bitblt.c`: direct `X11DRV_StretchBlt` source copies and `X11DRV_GetImage` readback substitute the
  real X root for a virtual-desktop source drawable and add its root offset to the source DC rectangle. Gate:
  source equals Wine's `root_window`, that window differs from `DefaultRootWindow`, and `_NET_WM_CM_S<screen>` has
  an owner at read time. The DC is copied locally; destination rendering/GCs and ordinary window reads stay as before.
  `dlls/winex11.drv/xrender.c`: under the same gate, scaling/format conversions fall back to that core path (and GDI
  image readback if needed), avoiding a cached desktop picture that lacks redirected children. Ownership is queried
  per read, so already-open screen DCs recover immediately after composition is disabled.

## Backported from upstream Wine (drop when rebasing past them)

| Upstream | Change |
|---|---|
| MR !12231 (merged 2026-10-08) | `dlls/user32/lstr.c`: `CharToOemBuffW` wrote more bytes than asked for with a multi-byte OEM code page (UTF-8); untranslatable characters become `_` |
| MR !7746 (open upstream when taken) | `dlls/shell32/shfldr_fs.c`, `shfldr_desktop.c`: sizes of files of 4 GiB and more (the pidl keeps only 32 bits); checked in Explorer's Details view |

## Reverted (were changed earlier, now stock again)
- `dlls/comctl32/monthcal.c` (today-circle removal): timedate.cpl now creates its calendar with
  `MCS_NOTODAY | MCS_NOTODAYCIRCLE`, which is what the XP applet looks like.
- `include/commctrl.h`, `include/prsht.h` (`SNDMSG` redefinition).
- `dlls/ntdll/unix/sync.c` (`NtSetSystemTime` running `sudo date`): clock changes go through the applet's elevation
  prompt only.
- `dlls/shell32/dialogs.c` (Run dialog placement).

## License headers

`wexhost` now also provides `WexAppsList` / `WexAppsLaunch` through runtime-loaded GIO. `apps.c` handles freedesktop discovery and launching with the shell's Wine variables removed; both Start menu styles expose the entries. The X11 WM checks its running state under the connection lock so requests arriving during shutdown cannot use the closed connection. These are project additions, not changes to Proton or a shared prefix.

Native focus adds `WexWmFocus`, current-focus validation for queued X events, and foreground-event synchronization. Explorer attaches the old foreground thread to its own input queue so Wine's detach operation does not recreate Explorer's queue and discard its active window/input time. Alt+Tab defers completion until after the keyboard hook. These fixes stay in Explorer and `wexhost`; the diagnostic X11-driver experiments were removed.

Rounded captions add `user_api_hook.pSetWindowRgn`, with the default user32 callback forwarding to `NtUserSetWindowRgn` and uxtheme tracking explicit application regions. The theme renderer combines the caption's transparent background region with its body, avoids recursive region repainting, and clears only its own region when styles are disabled. Start menu bitmap callbacks and its theme-derived panel shape stay in Explorer; ordinary application menu bitmaps retain upstream Wine behavior. See [THEMES.md](THEMES.md) for verification and references.
The project is GPL-2.0-or-later as a whole (see `LICENSE.md`). New project files carry the GPL-2.0-or-later header (`scripts/add-license.py` adds it to anything missing it); older project files keep their LGPL-2.1-or-later header, which is compatible.
Modified Wine files keep their original header. ReactOS-derived programs keep theirs:
- `programs/calc` - ReactOS Calc, LGPL-2.1+ (header in each file); `copying.txt` is the GPL text shipped with it.
- `programs/wmplayer` - ReactOS mplay32 (ReactOS license: GPL-2.0-or-later).
- `programs/explorer/alttab.c` - ReactOS-derived Alt+Tab implementation; retains its GPL-2.0-or-later header and attribution.
- `dlls/ntoskrnl.exe` - stock Wine.

## Themed non-client frame (visual phase)
- `include/ntuser.h`, `include/winuser.h`: new `DrawNonClientFrame` user32 callback, `struct draw_non_client_frame_params`,
  `user_api_hook.pNonClientFrameDraw` and `NCFRAME_*` flags.
- `dlls/win32u/defwnd.c`: `nc_paint` and the caption-button redraws ask the hook first; if it draws, only the rectangle is advanced.
- `dlls/user32/{user_main.c,hook.c,nonclient.c,user_private.h}`, `dlls/wow64win/user.c`: callback plumbing and default (declines).
- `dlls/uxtheme/{window.c,system.c,uxthemedll.h}`: `UXTHEME_NonClientFrameDraw` draws `Window` caption, frame and buttons from the active msstyles.
- `dlls/uxtheme/{window.c,system.c,uxthemedll.h}`: the themed frame draws classic when the application cleared `STAP_ALLOW_NONCLIENT` (`SetThemeAppProperties`) and opens its theme through `UXTHEME_OpenNonClientTheme`; `OpenThemeData` returns no theme when `STAP_ALLOW_CONTROLS` is cleared, as on Windows (stock Wine ignored both; the `OpenThemeData` part is upstreamable). Used by GUI-mode setup for XP's classic look.

## Explorer window (file browser)
- `dlls/shell32/shell32.rc`: shell view column titles "File"/"Modified" are now "Name"/"Date Modified" (Windows wording).
- `programs/explorer/explorer.c`: window class is `CabinetWClass`; the window chrome moved to `browser_ui.c` (menu bar, Standard
  Buttons, Address bar with Go, activity logo, status bar) and `browser_taskpane.c` (File and Folder Tasks / Other Places /
  Details, drawn with the ExplorerBar class).  Toolbar and logo pictures come from ReactOS browseui (`res_browser/`).

## Menu accelerator underlines (Windows XP behavior)

* `dlls/user32/text.c`: `DrawText` honors `DT_HIDEPREFIX` (the `&` is removed, the letter is not underlined).
* `dlls/win32u/menu.c`: menus hide the accelerator underlines until a key is used while the menu is open (or
  `SPI_GETKEYBOARDCUES` is set), like Windows XP; menu items with a 32 pixel picture are exactly as high as the
  picture (the classic Start menu).

| `dlls/wexhost` (`unixlib.c`), `include/wexhost.h` | WEX_POWER_LOGOFF (login1 TerminateSession of our session) and WEX_POWER_SWITCHUSER (GDM CreateTransientDisplay) | always |
