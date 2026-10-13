# P3b subsystem extraction

Shared resources, themeui, msgina, stobject, netshell, wzcdlg, wexsearch and
wexbrowse accepted October 13, 2026. P3b is complete; P4 repository restructure
is next.

`src/dlls/wexres` owns the icon/bitmap packs, namespace lookup and resource
templates formerly in wexshell. `src/include/wexres.h` defines the exports.
The shell keeps its manifest and file-description metadata. Resource calls
use the shared DLL's immutable module identity; window class/code instances
remain owned by the shell. Start menu style crosses the boundary as a value,
through a shell-local adapter, rather than a pointer to taskbar settings.
Resource images exist once. Feature dialog templates will move to their
respective DLLs when those features are extracted.

Build entries and header installation are retained in the Wine fork. The
implementation and shared header are project-owned, linked by assemble.sh.
Asset generation, the public asset manifest and private overlay VM sync point
at wexres. The original audit pin and historical P2 assembly ledger are intact.

## Acceptance evidence

Build VM: `/srv/protonexplorer/wex-refactor-p3b`, separate from P3 and installed
runtimes. Both DLLs build alone; the complete 64-bit Wine build passes.
Logs are `/srv/protonexplorer/p3b-delayed-check.log` and `p3b-smoke.log`.

- Four Xvfb captures against accepted P3: Classic Start, two-column Start,
  Explorer, Turn Off. Each has zero changed pixels in the 800x570 comparison
  area (the changing taskbar clock is excluded).
- Tray layout probe: two visible applet icons, zero overlaps/out-of-bounds.
- Eight native-dialog placement checks pass, including oversized and
  offscreen-owner dialogs.
- Versioned shell API/startup probe: zero failures with the plugin present;
  plugin-disabled run retains the stock tray and passes.

Screenshots/logs stay in ignored build output. The live dev session still uses
the accepted P3 build; this isolated step has not installed a new runtime.
No new ISO/install test is claimed for this resource extraction. P4 requires
a fresh clone, complete build/install, ISO and interactive installer test.

## Extraction scope

Keep clock/taskband/Alt+Tab in wexshell. Every split must pass
its feature check and visual comparison; optional modules must not prevent
shell startup when absent. Record any seam that is too wide instead of
forcing it across DLLs.

## Theme engine acceptance

`src/dlls/themeui` owns theme_engine.c. Its single interface is
`src/include/wextheme.h`; callers receive copied settings, with explicit Start
menu style and wallpaper-refresh operations. Display Properties calls the same
engine directly for Themes/Appearance Apply. The existing shell IPC command
remains compatible. This required engine uses ordinary imports; its dependent
shell libraries remain delayed, preserving graphics startup order.

The split exposed a Classic palette persistence gap: SetSysColors changed
temporary values, but the shell's reload read the previous persistent colors.
The engine now imports the theme's Colors section into HKCU with its other
registry sections. This is the only intentional behavior correction in this
step.

The standalone themeui/shell/desk.cpl builds and complete Wine build pass.
Four screenshots again match accepted P3 with zero changed pixels above the
clock strip. `scripts/refactor/check-theme.sh` passes its UI/API probe with
zero failures: state snapshots cannot mutate DLL globals; missing files are
rejected; the Themes page discovers a new fixture, applies it, and refreshes
its list after a rename. The real shell background pixel is the fixture's
RGB(12,34,56), confirming cross-process rendering. Logs:
`/srv/protonexplorer/p3b-theme-final2.log`, `p3b-theme-trace.log`, and
`wex-refactor-p3b/checks/theme/result.txt`.

## Power dialog acceptance

`src/dlls/msgina` owns power_mgmt.c and its four fallback bitmaps. Its interface
is `src/include/wexpower.h`: Turn Off, Log Off and action dispatch. Resource-pack
lookup remains shared; fallback artwork loads from the feature DLL. The shell
calls the same three functions. logonui loads this DLL directly instead of
spawning another explorer process. Asset generation and private overlay sync
route the power artwork to msgina; there is no image duplication.

Module/greeter builds and the complete Wine build pass. Four baseline captures
remain pixel-identical above the clock strip. `scripts/refactor/check-power.sh`
passes: standalone Turn Off and Log Off show within the display, cancel and
remove their dimming overlay without loading wexshell in the caller; the real
Start menu opens/cancels Turn Off; logonui's button opens/cancels it in logonui's
own process. A test-only inert LightDM pipe fixture supplies connection setup,
with no authentication or session-start messages. No power action is requested.
Reports are `wex-refactor-p3b/checks/power/{standalone,greeter,start-menu}.txt`;
all have zero failures. Runner log: `/srv/protonexplorer/p3b-power-final.log`.

## Tray service acceptance

`src/dlls/stobject` owns volume/power applets, their audio/power backends and
the volume popup template. Shared icons stay in wexres; no image duplication.
The boundary is standard IOleCommandTarget, using CGID_ShellServiceObject with
NEW/SAVE. The shell enumerates HKLM ShellServiceObjectDelayLoad, retaining each
successful service until shutdown and skipping malformed/unavailable entries.
No hard-wired volume/power calls or settings globals cross the DLL boundary.
Wine's boot registration list adds stobject; its registration script supplies
the class and service entry. The ATL parser rejects comments, so its GPL/SPDX
notice uses the `.rgs.license` sidecar.

The module and full Wine build pass. All four captures match the accepted P3
desktop **including taskbar and tray**, masking only the changing clock
rectangle (734,570)-(800,600): zero differing pixels. This stronger comparison
caught the initial registration failure that an upper-desktop crop missed.

`scripts/refactor/check-tray-services.sh` passes the COM lifecycle, resource
ownership, slider/menu and unload checks. Repeated NEW is idempotent; another
active client keeps applets alive; final release/SAVE cleans their windows;
restart works. Startup passes with the DLL present, disabled via DLL override,
removed from the registry, then restored. All reports have zero failures.
The live installed VM runtime is unchanged. Evidence:
`/srv/protonexplorer/p3b-services-final.log` and
`wex-refactor-p3b/checks/services/{com,present,absent,disabled,restored}.txt`.

Public protocol references:
[ReactOS service loader](https://github.com/reactos/reactos/blob/master/base/shell/explorer/shellservice.cpp)
and [tray service](https://github.com/reactos/reactos/blob/master/dll/shellext/stobject/csystray.cpp).

## Network module acceptance

The existing project-owned netshell module now lives in src/dlls/netshell,
with nettray/net_props/net_backend/wlan_status. Its shared interface is
src/include/wexnet.h (copied status/scan records and operations, plus the two
network dialogs). The Connection Tray COM object uses the same service
lifecycle; Wine boot registers it and the shell no longer calls nettray
directly. Resource templates and class instances belong to netshell; common
icons remain in wexres. Packet directionality code is unchanged.

To extract one DLL at a time, wireless selection still uses its original shell
implementation through a typed export, preserving process and owner. The
dispatcher prefers wzcdlg; the next step moves that implementation and removes
the temporary shell export. No private settings/global pointers cross DLLs.
This describes the netshell checkpoint; wzcdlg now replaces that temporary
bridge as recorded below.
Existing netshell entry points invoke the same dialogs directly.
ncpa.cpl calls StartNCW in this DLL; its cancel-only entry-point probe confirms
the dialog stays in the caller process instead of spawning explorer.

Module/full builds pass; four full captures including taskbar/tray match P3
exactly when masking only the clock. check-network.sh passes its read-only
host-status check, Connection Tray activation/menu/stop, Status and Properties
dialogs (own resources, within the display, Cancel), and startup with service
present, DLL absent, registry entry disabled, then restored. All reports have
zero failures: wex-refactor-p3b/checks/network/*.txt;
/srv/protonexplorer/p3b-network-final.log and p3b-network-cpl.log. The additional
ncpa build and full build pass in p3b-ncpa-check.log.

The prototype preserves the old assembled public netshell tree as
pre-split-netshell after exact comparison. Fresh Wine fork clones contain no
stock netshell directory. assemble.sh accepts empty directories left by tracked
file deletions, while still rejecting nonempty collisions. Public/private
asset routing follows the move. Installed dev/build VM runtimes are untouched.

## Wireless selection acceptance

src/dlls/wzcdlg owns wlan_dialog.c and its template; src/include/wexwlan.h
contains its single entry point. Backend/status calls use wexnet.h, and common
icons use wexres. The shell and netshell dispatch through a typed DLL export;
missing wzcdlg simply leaves wireless selection unavailable. The temporary
shell export and fallback were removed. There are no shared globals across
these modules or new helper processes.

The module and full build pass. The standalone wireless dialog has its own
resource instance, fits the display and cancels. ncpa's actual applet entry
still shows it in the caller process. The full network suite passes, including
shell startup with wzcdlg disabled. The wireless dialog capture against P3 is
pixel-identical across the entire 800x600 image; all four standard desktop/
taskbar/tray captures also match except the masked clock.
Evidence: p3b-wzcdlg-final.log, p3b-wzcdlg-optional.log,
p3b-wireless-visual.log and checks/network/{wireless,wireless_absent,cpl}.txt.

## Search module

Search Companion, results, engine, instant search and agent animation now live
in src/dlls/wexsearch. Its public wexsearch.h exposes an opaque UI handle,
layout/visibility operations and instant-search operations. Window classes
and the Rover bitmap belong to the DLL; common icons remain in wexres.
The shell retains its optional loader for process lifetime, so live window
procedures and asynchronous workers cannot be unloaded between calls.
Missing search disables the taskbar edit and consumes a Search Results request
without opening an empty window.

Module and full builds pass. check-search.sh passes standalone ownership,
one real file result, pane reopen, cancellation during destruction, instant
navigation/Escape and hide/cancel checks. Search runs independently without
loading wexshell. Shell startup and actual Search Results commands pass with
the DLL present and disabled; present results-view visibility is checked.
Four standard full desktop/taskbar/tray captures and the actual Search
Companion capture match P3 exactly except the masked taskbar clock.
Evidence: p3b-search-full.log, p3b-search-visible.log,
p3b-search-visual-final.log and checks/search/{standalone,present,absent}.txt.
Fork configure/header integration is pushed as 1743400e44.

## Browser module

src/dlls/wexbrowse owns explorer.c, browser_ui, browser_taskpane, browser.h
and slideshow. WexBrowseOpen takes folder/selection strings and flags; all
browser structs, COM callbacks and window procedures remain private.
The core retains command parsing and applet dispatch, and holds the optional
browser DLL while its classes/callbacks can be live. If loading fails, its
command export returns FALSE so stock explorer handles the command.
Browser images remain in shared wexres; class instances belong to wexbrowse.

The optional search client adapter has one source copy in wexsearch.
Wine's PARENTSRC compiles it into both callers, each with its own private
load-once state. This shares implementation without cross-DLL globals or
a runtime dependency on wexshell. The browser standalone probe confirms
that it never loads wexshell.

Scope refinement: toolbars.c is exclusively taskbar toolbar code (ordering,
resizing, settings, search edit and task button space), rather than browser
chrome. Moving it would expose taskbar state across the boundary. It stays
with the taskband in wexshell. Browser-specific bars are in browser_ui and
move together. Slideshow has only browser consumers, so it moves with them.

Module/full builds pass. Three standalone cases (filesystem folder,
My Computer and My Documents) pass class ownership, display bounds, bars,
task pane, taskband icon, Search Companion open/close and window teardown.
Optional startup passes with the DLL present and disabled. The actual core
command export returns FALSE when the browser is unavailable and leaves no
empty XP window, preserving host fallback. Search tests still pass after
the move. Four full desktop/taskbar/tray captures plus Search Companion match
P3 exactly after masking only the changing taskbar clock.
Evidence: p3b-browser-build.log, p3b-browser-final.log,
p3b-browser-header.log and checks/browser/{standalone,present,absent}.txt.
The header-only cleanup rebuild passes. Fork integration is d0a0af0681.
