# Search: Windows XP, ReactOS, Wine and later Windows

Analysis of 2026-10-08 for the Start menu's **Search**, the Explorer **Search** button, F3 / Win+F and
it. ReactOS code (LGPL) may be ported.

## 1. Windows XP / Server 2003 (from the source, by role)

| Piece | Where | What it does |
|---|---|---|
| Entry points | `SHFindFiles(pidlFolder, pidlSavedSearch)`, `SHFindComputer`, F3/Win+F, Start > Search, toolbar Search, "Search..." verb on folders | open the search results folder with the search pane, optionally scoped to a folder or reopening a saved search (`.fnd`) |
| Two engines | `CFindFilter` | without an index it walks the namespace (`CNamespaceEnum`, matching each item); when the Indexing Service catalog covers the scope and is running it turns the criteria into an Indexing Service query (OLE DB rowset that also reports later changes) |

## 2. ReactOS

- `shell32` `SHFindFiles` opens `explorer.exe ::{E17D4FC0-...}` (parameters ignored, honours `REST_NOFIND`).
  `SHFindComputer` invokes `CLSID_ShellSearchExt`. The taskbar uses `SHFindFiles` for Win+F and the Start menu.
- `browseui/shellfind` (about 2,500 lines):
  - `CFindFolder`: the results folder (`IShellFolder2`, view callback, `IPersistFolder2`), keeps the real item and
    shows Name / In Folder / relevance columns;
  - `CSearchBar`: an Explorer bar band (`IDeskBand`) with "All or part of the file name", "A word or phrase in the
    file", "Look in", "Search hidden files", Search/Stop and a progress bar;
  - engine: one worker thread, recursive `FindFirstFile`, case-insensitive name match, content match by mapping the
    file and looking for the text as ANSI and UTF-16; results are posted to the folder window.
- Missing compared with XP: date, size and type criteria, the other advanced options, saved searches, any index,
  the task-based Search Companion.

## 3. Wine and WineExplorer today

- Wine: `SHFindFiles` is a stub (FIXME, returns FALSE); there is no `CLSID_FindFolder`; `query.dll` (Indexing
  Service client) is stubs only; no `structuredquery`, `tquery` or Windows Search API.
- WineExplorer: Start > Search launches `explorer.exe ::{E17D4FC0-...}`, which Wine resolves to the **Desktop**
  folder (seen in the VM on 2026-10-08). The browser's Search button does nothing (`browser_ui.c`).
- The Debian VM has GNOME LocalSearch 3.8 (Tracker 3 indexer with full-text extraction, queried over D-Bus with
  SPARQL) installed; it is not running in the WineExplorer session.

## 4. What later Windows versions added (Microsoft Learn)

- **Windows Desktop Search 2.x** (XP add-on) replaced the Indexing Service, which Microsoft calls obsolete as of XP.
- **Windows Search 3.0+** (built into Vista; redistributable for XP SP2 / Server 2003 SP1):
  - the `WSearch` service (`SearchIndexer.exe`, LocalSystem) keeps one shared index (*SystemIndex*) and throttles
    itself on CPU, I/O, memory, battery and disk space;
  - extensible with protocol handlers (`ISearchProtocol`), `IFilter` content filters and property handlers on the
    property system (`.propdesc` schemas);
  - managed through `ISearchManager` / `ISearchCatalogManager` and the Crawl Scope Manager
    (`ISearchCrawlScopeManager`: search roots and scope rules);
  - queried through the OLE DB provider in Windows Search SQL
    (`SELECT System.ItemName FROM SystemIndex WHERE SCOPE='file:...'`), with `ISearchQueryHelper` turning Advanced
    Query Syntax (AQS) into SQL, and the structured query API (`IQueryParser` -> `ICondition` trees);
  - in the Shell: `ISearchFolderItemFactory` builds search folders over indexed or non-indexed locations (USB, FTP,
    MTP through their Shell data sources), the `search-ms:` and `search:` protocols, saved searches (`.search-ms` in
    `%USERPROFILE%\Searches`, FOLDERID_SavedSearches), search connectors (`.searchConnector-ms`, Windows 7 federated
    search), libraries;
  - remote queries: the Windows Search Protocol [MS-WSP].
- **Windows 10 / 11**:
  - the index database moved from `Windows.edb` (ESE) to `Windows.db` on Windows 11;
  - Classic and Enhanced indexing, settings under Privacy & security > Searching Windows, the `AllowWindowsIndexer`
    policy; Windows 11 Enhanced mode also reads text in images;
  - Copilot+ PCs on 24H2 add semantic indexing ("improved Windows search", natural language,
    `ISearchCatalogManager3` reports support, WinRT `QueryOptions.IncludeLocalSemanticIndex` and
    `IncludeCloudProviders`);
  - 2026 Insider builds bring a new WinUI 3 search box with inline actions.

## 5. Proposed plan for WineExplorer

**Phase 1: XP behavior without an index**, everything in explorer plus a small, upstreamable shell32 change.
1. A "Search Results" folder (`CLSID_FindFolder`) and the Search pane in our Explorer windows, on the model of ReactOS
   `CFindFolder`/`CSearchBar`, ported to C, with XP's criteria (name, containing text, look in, date, size, type,
   advanced options) and XP's columns.
2. `SHFindFiles`/`SHFindComputer` in Wine's shell32 open that folder, as ReactOS does; then the Start menu, F3/Win+F,
   the toolbar Search button and "Search..." on folders all go through `SHFindFiles`.
3. The XP Search Companion layout (task list, then the criteria page), drawn natively from XP VM screenshots. The
   Agent characters need Microsoft Agent and stay out at first.

Status: done. `search_companion.c` (pages, criteria, preferences, character chooser), `search_engine.c` (worker
thread: names, wildcards, text in files, dates, sizes, types), `search_results.c` (results list, watermark), and
`agent_char.c`, which plays the Agent characters of `%WINDIR%\srchasst\chars` itself (written from the published
descriptions of the .acs format: frames, branches, exit branches, return animations, idle states; no Microsoft Agent
needed). Rover shows, searches, is pleased or embarrassed by the result and reacts to clicks; "With a different
character" previews Courtney, Earl and any other .acs file with its greeting.

**Also: instant search** (`instant_search.c`), the taskbar search box of Windows 10 and 11 as a taskbar toolbar
(Toolbars > Search). Typing there, or typing while the Start menu is open, lists the best match, Start menu programs,
Control Panel items, and folders and files of your own folders (found as you type by the same search engine), with the
selected item described on the right ("Open", "Open file location"); the last row opens the Search Companion.

**Phase 2: an index**, the counterpart of XP's "Use Indexing Service". See section 6.

**Phase 3, only if programs need it**: the Vista+ APIs (`ISearchFolderItemFactory`, `search-ms:`, structured query,
the Windows Search OLE DB provider). Wine has none of them; this is a large separate project.

## 6. Index backend: Linux indexers compared (references/linux-search, gitignored)

| Source | License | Model | Fit |
|---|---|---|---|
| **LocalSearch 3** (`localsearch`, formerly tracker-miners; Debian 13 ships 3.8.2) | GPL-2.0+ (program) | per-user daemon: crawls configured folders, live updates (inotify/fanotify), extractor subprocess for properties and full text (txt, Office through libgsf, PDF, ODF, images, audio ...), SQLite store with FTS; queried in SPARQL over D-Bus | names **and** contents, live, already installed: the best match for XP's Indexing Service |
| **TinySPARQL 3** (`tinysparql`, formerly tracker) | LGPL-2.1+ (library) | the store/query library and D-Bus endpoint client (`tracker_sparql_connection_bus_new`, cursors) | what `wexhost`'s Unix side would load to query LocalSearch |
| **FSearch** | GPL-2.0+ | Everything-like: compact in-memory tree of entries with parent pointers, presorted arrays per sort key, Everything-style query language (wildcards, regex, `ext:`, `size:`, `dm:`, `path:`, `case:` ...), inotify/fanotify monitors, saved database file | names only, instant as you type; ideas for a later "instant" name search |
| **plocate** | GPL-2.0 | `updatedb` snapshot (daily timer, system-wide, root), trigram posting lists over zstd-compressed path blocks | names only, not live, whole system: not a fit; the trigram index is a good idea for huge name sets |

GPL code is used out of process only (LocalSearch as a service over D-Bus); FSearch and plocate are design
references, nothing is copied into the LGPL Wine tree.

**Separation from the Linux desktop: proven with the stock binary (2026-10-08, VM).** Run Debian's
`/usr/libexec/localsearch-3` with:
- its own private D-Bus bus (`dbus-daemon --session --address=unix:path=...`), so it never meets a GNOME LocalSearch
  of the same user on the session bus;
- `XDG_CACHE_HOME` / `XDG_CONFIG_HOME` / `XDG_DATA_HOME` / `XDG_RUNTIME_DIR` of its own and
  `TRACKER_USE_CONFIG_FILES=1`, so it reads `tracker/localsearch-3.cfg` (`[General]` `index-recursive-directories`,
  `ignored-directories`, `ignored-files`, `max-bytes`, `index-on-battery`, `throttle` ...) instead of GNOME's
  GSettings, and keeps its database apart.

Configured for `drive_c/users/<user>` only, it indexed the profile in about 20 s, found `notes.txt` by name and by
content ("Hello"), and a file created afterwards was found by its content within 6 s. No custom build is needed.
(LocalSearch also has a build-time "dedicated service" mode, `-Ddomain_prefix=...`, made for sandboxing: own bus
name and cache, exits with its owner. Keep it as the fallback if the runtime separation ever stops working.)

**Drive letters stay on our side.** LocalSearch only knows `file:///home/...` URIs. The shell keeps the Windows
view:
- scopes ("Look in C:\Documents and Settings", "Local Hard Drives") become Unix roots through Wine's own mapping
  (`wine_get_unix_file_name`), and results come back to Windows paths through the drive table (`dosdevices`)
  plus the known folders (a profile folder that is a symlink to an XDG folder maps back to its `C:\...` name, not
  `Z:\home\...`);
- the index covers what the user calls Windows locations (default: the Windows profile and the Start Menu, like
  Windows Search; XP's Indexing Service indexed local drives), never the whole Linux tree behind `Z:`;
- Windows attributes stay Windows: hidden/system come from Wine (`GetFileAttributes`) when results are shown, since
  LocalSearch only knows dot files.

**Shape, as on Windows:** a Windows service "Indexing Service" (`CiSvc`, start/stop in services.msc) owns the
private bus and the LocalSearch process; the catalog lives in `C:\System Volume Information\catalog.wci` (hidden and
excluded from indexing, like XP); `wexhost` queries it with TinySPARQL; the search engine uses the index when the
scope is indexed and walks the folders otherwise (as XP did). Search Companion's "Change preferences > With Indexing
Service" switches it on or off.

Open decision:
- Search Companion first, or the plainer Windows 2000 style pane first?

## Taskbar band resizing (2026-10-09)

The first visible band stays at the fixed offset from Start, matching the XP VM.
Its left gripper can reorder it, but cannot introduce a blank leading space.
Other grippers move the boundary between neighboring bands: dragging right grows
the left band and shrinks the right band; dragging left does the reverse. The task
band absorbs remaining width. Its gripper, drawn in the tray rather than in the
task-button window, now handles dragging too, so a toolbar immediately before it
can be resized from the right. This applies to any toolbar order, including Search
before or after the task buttons. The existing crossing thresholds still reorder
bands. Locked taskbars hide and disable these grippers.

Dragging starts from the actual displayed widths, including compression on small
screens, and uses the coordinates supplied by the mouse event. Search and Address
keep enough width for their embedded controls. The sizing strip above the taskbar
is drawn with `TaskBar.SizingBarBottom`; locking hides it without shifting the
Start button or stretching the main taskbar background. The XP reference has a
four-pixel strip in its unlocked Luna taskbar.

Validation: the Explorer target builds in the Debian VM. Live window measurements
confirmed the first band stays at x=103 in unlocked Luna, a toolbar after the task
buttons can move its left edge while keeping its right edge fixed, and a shared
Search/Quick Launch boundary transfers 20 pixels in either direction. The task
band's gripper also resizes Search on its left. Reordering the task band was
exercised in the VM. Locked/unlocked screenshots matched the XP reference's
four-pixel sizing-strip colors and kept the background below it at the same
screen coordinates. Checks were performed with the XP resources installed.

## Search focus and desktop typing (2026-10-09)

The desktop view is now a focusable tool window kept at the bottom of the window
stack. Wine rejects keyboard focus on a child directly beneath its special
Desktop window; this had left the taskbar search edit receiving characters after
a desktop click. The desktop supports incremental name selection and repeated
initial-letter cycling. Leaving search closes the results, cancels the search
job, and clears its text, matching the requested Windows 11 behavior. Interaction
with the search results retains the query.

Live validation: enter a query, click the exposed desktop, and type `m`.
The search field returns to its placeholder and My Documents is selected.
