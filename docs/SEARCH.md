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

**Phase 2: an index**, the counterpart of XP's "Use Indexing Service".
- LocalSearch (Tracker 3) over D-Bus as the backend for name and content queries, matching docs/DBUS_PLAN.md, with
  the walking engine as fallback. Needs Windows path <-> Unix path mapping and the service running in the session.

**Phase 3, only if programs need it**: the Vista+ APIs (`ISearchFolderItemFactory`, `search-ms:`, structured query,
the Windows Search OLE DB provider). Wine has none of them; this is a large separate project.

Open decisions:
- Search Companion first, or the plainer Windows 2000 style pane first?
- LocalSearch as the index (adds a dependency on the session running it)?
