# Refactor continuation — 2026-10-12

October 13 P3b shared resources accepted: the build VM has a separate source/build under
`/srv/protonexplorer/wex-refactor-p3b`. The full build, runtime and screenshot
checks pass; see P3B.md. Wine fork build integration is pushed as bd86fda234.
No installed runtime/session was changed. Themeui is also accepted (P3B.md),
including direct Display Properties Apply and actual Classic palette rendering.
Msgina is accepted with standalone, real Start menu and logonui tests (all
cancel-only). Stobject is accepted with COM lifecycle, slider/menus and
optional-service startup checks. Four full desktop/taskbar/tray comparisons
masking only the clock have zero differing pixels. The last source guard
stops service enumeration on errors other than an oversized entry; its module
rebuild passes. Netshell now passes host-status, COM/menu/dialog and optional
service checks, with all four full screenshots identical except the masked
clock. Its implementation has moved to src/dlls/netshell; the prior assembled
public directory is preserved in p3b/pre-split-netshell. ncpa.cpl now calls
the shared DLL directly; its real applet entry-point Cancel check passes in
the same caller process. The extra module/full build also passes. Wine fork
build/header/registration integration is pushed through 41f35202f8.
Wzcdlg is accepted: its standalone/cpl/missing-DLL checks pass and its entire
dialog capture equals P3. The temporary wlan_dialog_show shell export/fallback
was removed. Fork build/header integration is pushed through 51b8e6ed5b.
Wexsearch is accepted: module/full builds, standalone real-file/cancel/focus
operations and actual shell requests with the DLL present/disabled pass.
All four standard captures plus Search Companion match except the masked clock.
Its configure/header integration is pushed as 1743400e44. Installed runtimes
are unchanged. Wexbrowse is also accepted: full build, three independent folder
cases (including icons/search/teardown), optional availability/host fallback
and Search Companion checks pass. All five captures match except the clock.
Fork configure/header integration is d0a0af0681; the final header cleanup's
module rebuild passes. No P3b runtime is installed. Next: P4 repository
restructure, P5 evaluation and P6. The API presence/absence
probe passes with zero failures in both runs.

P4 has started: 324 remaining module/header/theme paths moved to src. New
assembly replacement guards pass disposable safety tests. P4.md records the
incomplete checkpoint. Its prototype root is /srv/protonexplorer/wex-refactor-p4.
Do not rerun package-p3b.py/build-p3b.sh against the changed main layout: old
wine/ project paths no longer exist. The accepted P3b VM tree is intact.
Use package-p4.py/build-p4-owned.sh for this prototype. The fork tag wine-11.19
was pushed unchanged. The submodule and installer acceptance are still pending.

Failed commands during this prototype:

- P4 preparation then failed at checkout with fatal: unable to read tree
  (d0a0af068194c306bb36659b35bb2d4e0eff43c1). The fetched commit was present
  in the VM's source repository only through FETCH_HEAD. The shallow local
  clone did not contain it and had no shared alternates file. Promoted the
  fetched ref to origin/wineexplorer/11.19 and fetched that explicit ref into
  the clone before retrying checkout. The helper now validates object presence
  and does this fallback explicitly. Its no-checkout status read printed many
  staged deletions; no source had been checked out or deleted there.

- The P4 fork fetch produced no output for almost two minutes. A bounded curl
  to github.com returned HTTP 200. Prepared a 403299-byte offline Git bundle
  as a fallback. A targeted TERM of the previously observed git-remote-https
  PID 1725241 returned No such process: that helper had handed off to index-pack.
  Inspection showed the fetch still active, indexing 1341937 objects.
  No process was terminated; the bundle was not imported. A subsequent ls of
  p4/scripts failed because extraction follows the still-running fetch.

- The first browser runtime suite passed its three standalone folder/UI cases
  but failed the unavailable-browser command assertion. The startup probe is
  a separate process: GetModuleHandle(wexshell.dll) cannot see the desktop's
  loaded DLL. LoadLibrary in the probe before resolving WexShellCommand; this
  exercises the same core export in the probe process. No product fix needed.
  Reading scripts/fe.sh also failed: that path does not exist in this checkout.

- The first standalone Search Companion image comparison failed:
  compare-search.py asserted after differences at (4,30)-(640,480).
  Five-second captures outside a named desktop showed differing window positions
  and a partially initialized new browser. Changed both capture runs to the same
  named 800x600 shell desktop and waited 15 seconds for the browser. Added actual
  results-view visibility to the shell request probe. This is not accepted as a
  visual pass until the corrected captures are compared.
  The corrected captures differed only at (765,581)-(770,589), the taskbar
  clock. The initial whole-image assertion therefore also failed; after
  applying the same clock-only mask used for standard captures, comparison
  passes with zero differences.
  A second JavaScript patch bundle hit the same Markdown backtick parse error;
  separated the long workorder status-line replacement into a saved script.

- Search extraction reads failed before editing: the PowerShell command using
  src/dlls/wexshell/{search_companion.c,search_results.c,instant_search.c,explorer.c}
  raised Missing argument in parameter list (Bash brace syntax). Used directory
  searches with -g. Positional scripts/*probe.c and build/refactor/*.txt
  rg searches raised Windows OS error 123; use directory plus -g instead.
  An unnecessary rg of build/refactor/nonexistent.log raised OS error 2.
  Reads of scripts/shell-menu-probe.c, browser_ui.h and browser_taskpane.h
  failed because those files do not exist; rg --files found shell-plugin-probe.c
  and browser.h. None of these read failures mutated files.

- The first search module link failed with undefined IID_IContextMenu,
  IID_IImageList, IID_IShellFolder, theme functions and AlphaBlend. Those had
  been supplied by the old shell compilation unit/imports. Define GUIDs in
  the new module.c via initguid/shlobj/commoncontrols and delay-import uxtheme
  and msimg32. Its module/full rebuild passes.

- The first search runtime runner stopped at probe compilation:
  FindResourceW's RT_BITMAP was the ANSI macro in this non-UNICODE test.
  Used MAKEINTRESOURCEW(2) explicitly. No runtime checks had executed;
  restarted the runner with the corrected probe.

- The initial patch recording these failures itself failed to parse:
  JavaScript String.raw contained Markdown backticks. Nothing executed.
  Removed the Markdown backticks from the patch and reapplied it.

- A functions.exec bundle failed to parse with `SyntaxError: Missing } in
  template expression`: its JavaScript String.raw template contained Bash's
  `${1:?p3 or p3b}`. Nothing in that bundle executed. Used a dollar placeholder
  in the patch string and replaced it before apply_patch.

- The first wzcdlg configure failed because netshell/nettray.c still included
  the now-removed private wlan_dialog.h. Updated it to the shared wexwlan.h;
  other consumers were already updated. Rebuilding the isolated tree.

- `extract-wzcdlg.py` stopped at its directory-absence assertion because the
  preceding patch had already created the new Makefile/spec directory. No
  files moved. Changed the guard to require absence of wlan_dialog.c and allow
  the existing scaffolding directory, then reran.

- The staged netshell diff check found a new blank line at netshell.rc EOF
  after resource extraction. Removed that extra blank line; no code behavior
  changed and the staged diff check passes.

- The first netshell resource build failed at netshell.rc:159:58 with a syntax
  error: moved LAN Properties templates use LVS_* constants, but the old
  netshell.rc did not include commctrl.h. Added that include. The prototype
  also preserved its old public module directory as pre-split-netshell after
  exact comparison with the untouched P3 baseline; no stock module was replaced
  (upstream has no netshell directory).

- The first service acceptance probe failed `registered service activates`.
  A full-taskbar comparison (masking only the clock) also showed missing
  applet pixels at 692..728,578..593; the upper-desktop comparison alone would
  have missed this. `WINEDEBUG=+regsvr32,+atl` showed the registrar rejecting
  the `.rgs` SPDX C comment as a root key named `/*`. Wine's ATL parser does
  not support comments in this input. Moved its GPL/SPDX notice to the REUSE
  `.rgs.license` sidecar and kept the registry script comment-free; rebuilding
  and rerunning registration, COM, optional-service and full-taskbar checks.

- The second stobject build failed compiling systray.c: implicit declarations
  of shellservices_start/stop. The old applet declarations came transitively
  from explorer_private.h, so replacing direct includes in systray.c did not
  add the new declaration. Added shellservices.h to that private header and
  rebuilt. No installed runtime was touched.

- The first stobject configure failed with `module.c:16: error:
  wine/library.h: No such file or directory`. Searches/reads of that header
  also failed: this Wine release declares its resource registration helpers
  in rpcproxy.h, with no HINSTANCE argument. Switched to that header and the
  existing Wine DLL registration signatures; rebuilding before acceptance.
- ReactOS fetch of `dll/shellext/stobject/res/stobject.rgs` was unavailable.
  The local public reference has `resources/rgs/systray.rgs`; it confirms
  CLSID {35CEC8A3-2BE6-11D2-8773-92E220524153}. The service NEW/SAVE contract
  was checked against public csystray.cpp and explorer/shellservice.cpp.

- The first power UI runner recorded POWER_CHECK_FAILED after successful
  standalone and greeter reports, without a Start menu report. Launching the
  controller after opening a modal dialog stalled; moved it before activation.
  The second attempt reported `FAIL consumer opens power dialog`: starting
  the controller after opening the menu and using the `u` key did not activate
  the menu item. Start the controller before opening the menu and click the
  Turn Off row at 80,550, observed in the fixed 800x600 Classic menu capture.
  The final runner passed all three reports and POWER_CHECK_PASSED.
- `ls .../checks/power/start-menu.txt` failed during the incomplete first
  runner because that report had not been created. It now exists and passes.
- ReactOS reference fetch of `dll/shellext/stobject/stobject.rgs` returned
  HTTP 404; use its actual registration resource/source inventory when needed.

- A work-order status apply_patch failed because its context ended midway
  through the actual long line. No change was applied. Reapplied with the
  complete line; the accepted themeui status update succeeded.

- The first post-persistence theme runner recorded THEME_CHECK_FAILED despite
  a zero-failure probe and a saved screenshot. Its outer 180-second timeout is
  a possible cause, but the log did not preserve the exit reason; do not claim
  that run passed. A separate `bash -x check-theme.sh` rerun exited normally
  and wrote THEME_CHECK_PASSED. The captured desktop pixel is RGB(12,34,56).
  All four final baseline screenshots have zero changed pixels above the clock.

- Two Windows OpenSSH reads of the build VM stalled during public-key
  authentication despite IPQoS=none. Cancelled the read-only sessions with
  Ctrl+C; no guest command ran in those sessions. A verbose third connection
  eventually authenticated after the stall. Git's bundled
  `C:/Program Files/Git/usr/bin/ssh.exe` connects immediately and is the current
  fallback (same key/port, no guest SSH changes). `Get-Process VirtualBoxVM`
  failed because these VMs run as VBoxHeadless; queried those actual processes.
  All four VMs remain running; only isolated prefixes are used by tests.

- After a successful theme rebuild, the capture runner ended with
  `capture-shell.sh: line 53: unexpected EOF while looking for matching '"'`.
  I had copied an updated capture helper over the running script, changing
  its offsets while Bash was reading it. `bash -n` passes on both local/remote
  complete files. Restarted only the isolated capture/theme tests after the
  first runner exited. Do not overwrite a script while it is executing.

- The first theme UI probe reported failure creating its .theme fixture and
  consequently could not discover it. Its standalone MinGW `swprintf` used
  `%s` for a WCHAR string; changed it to standard `%ls` and placed the fixture
  under the isolated user's APPDATA theme directory. The corrected fixture is
  discovered and the Themes page Apply/list refresh work.
- The probe's assertion that its windowless process immediately observes another
  process's SetSysColors failed. Wine caches colors per process (the existing
  engine has theme_sync_colors for this reason). Check the persisted color and
  the actual shell's rendered desktop instead; do not change product color
  behavior to satisfy that windowless probe.
- The corrected registry assertion exposed a real theme-engine gap:
  apply_registry_sections imported cursors/sounds/icons but skipped Colors;
  SetSysColors alone uses temporary values. The shell's existing
  theme_sync_colors subsequently reloaded the previous persistent palette.
  Persist the theme's Colors section alongside its other registry sections so
  Classic colors survive the required cross-process reload. Validate actual
  desktop rendering with the fixture after this fix.
- The first theme screenshot run stalled in wineboot with `unimplemented
  function themeui.dll.theme_init`, although explicit LoadLibrary/GetProcAddress
  in the fresh theme prefix found working exports. Switched this required engine
  to ordinary imports in wexshell and desk.cpl (its own shell32 dependencies
  remain delayed); rebuilding and retesting before acceptance. Prior visual
  status files were stale, so the capture helper now clears images and marks
  running/failure explicitly before every run.

- `python build/refactor/extract-theme.py` stopped at its no-shared-state
  assertion after moving the two source/header files: the byte replacement
  expected LF but the Windows working file used CRLF. No C transformation was
  written before the assertion. Made the initial moves restartable and
  normalized input bytes before the replacement, then reran successfully.
- The first theme configure failed: `explorer_private.h:24: error:
  theme_engine.h: No such file or directory`. The extraction had updated C
  includes but missed the private header. Changed it to wextheme.h; configure,
  themeui, shell, desk.cpl and the full build pass.
- `git diff --check` found trailing whitespace at appearance_page.c:150 after
  removing its obsolete COPYDATASTRUCT declaration. Removed the trailing space.

- `rg -n 'resource.h|shellres|shellfolder' wine/dlls/desk.cpl/*.c` and
  `rg -n 'WexResModule' src/dlls/wexshell/*.c` reported Windows filename syntax
  error 123. Use `rg -g '*.c'` with a directory instead of a positional glob.
- The first `make -j8 dlls/wexres/all dlls/wexshell/all programs/explorer/all`
  failed linking wexres: undefined `IID_IShellLinkW`, `CLSID_ShellLink`, and
  `IID_IPersistFile`. Added initguid.h before windows.h/shlobj.h in its module
  translation unit. The next build repeated the same linker error because
  `sync-archive.py` uses tar entries with epoch timestamps and `tar -xf` kept
  them, making the corrected source older than the object. Changed this
  prototype's extraction to `tar -xmf`; both DLLs and the full build then passed.
  The first failure log is preserved as `/srv/protonexplorer/p3b-guid-failed.log`.
- Reading `wine/dlls/themeui/Makefile.in` and `themeui.spec` failed because
  upstream has no themeui module in this tree. Its new module can be assembled
  without replacing an existing stock directory.
- `capture-shell.sh p3b` reached the menu probe but exited with one menu failure,
  without creating screenshots. The startup API probe then reported two
  failures (no taskbar/controls). `WINEDEBUG=+explorer` showed failed graphics
  initialization and `nodrv_CreateWindow`. wexres had made the previously
  delayed shell32/shlwapi/ole32/comctl32/uxtheme dependencies eager imports.
  Restored DELAYIMPORTS; startup and all four screenshot comparisons then
  passed (zero changed pixels above the clock strip). The tray and eight native
  dialog placement checks also pass. The full build passes.
- Diagnostic `ls` of `build/dlls/winex11.drv/x86_64-unix/winex11.drv.so` and
  `x86_64-windows/winex11.drv.dll` failed: this Wine tree uses `winex11.so` in
  the module directory and `x86_64-windows/winex11.drv`. Both actual files exist;
  no missing driver build or dependency problem was found.
- `Get-Content scripts/fe.sh` failed because the old helper is no longer in
  this repository. Use the maintained `scripts/vm.sh` workflow.


Latest checkpoint (October 13): P0–P3 acceptance passes. P3b is next, then P4,
P5 evaluation and P6 rebase drill. See P3_TEST_REPORT.md. The older chronological
entries below describe their state at the time and do not override this.
The dev VM runs the installed plugin; build VM source/build is isolated under
/srv/protonexplorer/wex-refactor-p3. The public ISO install test passed and its
QEMU VM is stopped. Source/fork changes are committed regularly; preserve the
user's .gitignore edit and concurrent Wayland documentation/lab commits.

P0 acceptance passed; source is still in its original layout. P1 uses a separate
build checkout/prefix on the Proton build VM. Existing installed runtimes,
installer images and desktop sessions remain intact.
User's `.gitignore` edit is preserved. A pre-existing `.claude/` worktree is
untouched; `/.claude/` was added only to local `.git/info/exclude` after the app
started thousands of untracked-file diff subprocesses for it. That exclusion
is not a project policy change and is not committed.

Read `REFACTOR_MAP.md` and `PATHS.md` for current audit state. The audited source
is pinned to `c9f9440d`; later documentation/tool commits do not change its Wine
tree. Keep that pin until actual Wine source changes are intentionally audited.
The bare evidence repository is `build/refactor/wine.git`. The real release tag
came unchanged from wine-mirror because the user's fork has no tags.

Current map: 750 paths/1,876 hunks, zero unclassified paths. Ninety-seven stock
paths have explicit hash-pinned, per-hunk decisions in `REVIEWS.json`; eleven include U hunks
containing twelve independent fix candidates. P0's strict acceptance command passes.
P1 acceptance passed, with twelve validated/pushed branches: imagelist-alpha (985db3ef),
empty-recycle-bin-icon (00d5a25f), wizard-focus (94a5845c),
drawtext-hideprefix (72feeed7), folder-iconfile (d942c4f4), and
rename-source-notification (f7038cc7), power-capability-battery (aee72c04), and
shortcut-target-icon (794d2015, fix 23e9baea), ie-desktop-icon (1fab750b),
theme-token-whitespace (acfcdd57), timezone-cache (eaf8a9ed), and
server-dst-bias (5372d43d). P2 is in progress; P3 and later phases have not started.
These refactor phases precede the installer's layout update in P4.
VM is clean on upstream/server-dst-bias. No runner is active; switch master and
rebuild before any further P1 work. All twelve topics are pushed, no MRs opened.
P0 has been refreshed after applying the tested battery initialization fix in
main (31f7896a). Only one reviewed ntdll hunk changed; strict coverage still
passes all 750 paths/1,876 hunks. P2's sparse checkout is build/refactor/fork,
branch wineexplorer/11.19, retaining the original 72 B commits and 85 subsequent
commits through 916ffefabc (all pushed). P2_SERIES.json records each commit/path;
P2_PROGRESS.json shows zero stock differences, with 436 M paths reserved for
assembly and twelve N differences.
U, 47 X groups, H integration and two additional B backports are retained.
Shared declarations were split into feature commits. The final shell files
were split into eleven helper/feature commits and match the pin exactly.
P1's additional shortcut cycle guard was reconciled in main c9f9440d and the
retained branch; only that reviewed hunk changed in the refreshed audit.
Assembly equality passes: tree bd2441a2b82d51a63b90ad059589e4f71a53dceb,
zero unexpected differences, twelve classified N differences.
The isolated assembled build is running (PID 1329961) at
/srv/protonexplorer/wex-refactor-p2. Logs /srv/protonexplorer/p2-build.log;
status wex-refactor-p2/build.status. Actual guest tree equality and patch hash
passed before autoconf/configure. No installation occurs. P1 source/build and
installed runtimes remain unchanged. See P2.md for next steps.
Runtime probe compilation passed without diagnostics. The smoke runner is
queued (PID 1425116, check-assembled.sh --wait-for-build); it waits for the
full build to pass, then uses its own prefix/Xvfb display. Status/results are
under wex-refactor-p2/checks, log /srv/protonexplorer/p2-check.log. Inspect
the resulting desktop.png visually. At the latest check, BUILD_RUNNING and
CHECK_WAITING_FOR_BUILD were reported; runtime acceptance is still pending.
See UPSTREAM.md for statuses, descriptions and exact validation scope.

The import's upstream snapshot is `eba89375` (72 commits after wine-11.19).
The 68 import differences contain only modes, missing ntoskrnl files, open art
and CRLF normalization. The search helper is local
`build/refactor/find-import.py`; its bounded candidate search completed.
`build/refactor/check-coverage.py` performs six negative checks, all passing.
`build/refactor/record-reviews.py` created the first reviewed stock decisions;
do not rerun it after adding more reviews, because it writes only those eleven.
`build/refactor/review-graphics.py` added the graphics, theming, shell-hook,
work-area and endpoint volume decisions. It reads the pinned evidence and
preserves existing review entries. Build VM connectivity was checked: Xvfb
exists and the 60 GiB root filesystem has 19 GiB available. No guest was written.
`build/refactor/review-shell.py` added shell32, Explorer boundaries, build inputs
and backport decisions. Local generators are scratch tooling; the committed
REVIEWS.json is authoritative. The fork master still resolves to 081042ede6.

P1 runtime: `~/wex-refactor/upstream` is a shallow fork clone, and
`~/wex-refactor/build` is a full clean 64-bit build (completed). No installation
was performed. `base-build.log` and `base-build.status` record completion.
The last runner was `test-topic.sh rename-source-notification shell32 shlfolder`,
PID 1316175 at relaunch; it finished with TESTS_PASSED and committed f7038cc7.
The VM checkout is clean on that branch. Switch master and rebuild before the
next topic. `rename-topic.log` and `results/rename-source-notification/` hold
eight expected baseline failures and zero fixed failures. All six topic suites
pass. Disk has 11 GiB free; account for isolated prefixes before further builds.
The last runner was `test-topic.sh ie-desktop-icon shell32 shelllink`, PID
1323418 at relaunch, and completed with TESTS_PASSED. No runner is active.
`ie-icon-topic.log` and `results/ie-desktop-icon/` hold three expected baseline
failures and zero fixed failures. Its topic is committed and pushed.
Its build-targets also rebuilds shdoclc; the new icon is exactly fork master's
programs/iexplore/iexplore.ico. The runner stages new files as well as edits.
Its update-prefix flag runs isolated wineboot -u before each test phase because
ExtractIconEx opens the physical prefix DLL rather than the loaded build DLL.
The shortcut self-referencing-link test passed the full shelllink suite and
was committed/pushed as 794d2015. VM is clean on upstream/shortcut-target-icon.
Cycle evidence: shortcut-cycle-build.log and shortcut-cycle-test.log. Host
battery, shortcut and ie-icon worktrees all match their validated VM commits.
Switch master and rebuild before the next topic. Disk has 8.5 GiB free.
Battery validation completed: full info suite in both states, one expected
failure before and none after. It uses scripts/refactor/power-fixture.sh and
test-power-topic.sh through test-topic.sh's optional input/test-wrapper.sh.
Both battery and no-battery sysfs views were verified inside a private mount
namespace. The VM provides passwordless sudo for namespace/mount setup;
runuser executes Wine and its cleanup as wineuser. Outside the namespace the
real AC-only view remains unchanged. The fixture directory is readable by that
user, and the isolated wineserver is killed and waited for before unmounting.
The topic initializes SYSTEM_BATTERY_STATE to zero: the existing project
patch does not, while the stock helper expects the caller to initialize it.
The audited main Wine source has not yet changed; carry that correction into
the retained patch/P2 tree and update the audit when changing main source.
SetPath loses the folder type in a simplified PIDL; the icon regression uses
fully parsed PIDLs so that independent issue does not mask the tested fix.
Topic results/prefixes are isolated under ~/wex-refactor. Old failed imagelist
runner 1281780 was stopped and its patches reversed exactly before rebuilding
master and rerunning; the successful imagelist run then confirmed six intended
failures before the fix and zero after. All current topic source is open Wine
code, with existing notices preserved.

Host sparse worktrees are `build/refactor/upstream` (validated imagelist),
`build/refactor/recyclebin` (validated resource fix), and
`build/refactor/wizard` and `build/refactor/drawtext` (validated), and
`build/refactor/folder-icon` and `build/refactor/rename` (validated).
Fetch VM commits through SSH into
refs/remotes/build/upstream/<topic>, verify the host worktree matches, then
update only that worktree's index/HEAD and push its approved upstream branch.
No WineHQ merge request was created. Git identity follows the user's configured
handle; real-name authorship and sign-off remain the user's manual step.

### P1 tooling failures and corrections

* During P2, `Get-Content build/refactor/fork/dlls/uxtheme/uxthemedll.h`
  failed Cannot find path because the header was outside the sparse checkout.
  Added both shell/theme headers to the sparse definition before editing.
* `python build/refactor/retain-h-series.py` stopped at git diff --check (exit 2)
  on the audited Explorer Makefile's added blank line at EOF. Reversed the exact
  h-00 patch, then reran using core.whitespace=-blank-at-eof for that inherited
  source whitespace; other whitespace errors still fail. All four H commits
  then completed. No patch/commit was skipped or staged after the first failure.
* Bare topic extraction/commit helpers are ignored scratch files under
  build/refactor; authoritative inputs are the committed hash-pinned map.
  scripts/refactor-series.py and refactor-progress.py are committed reusable
  tools. Three deliberate negative inputs were rejected without emitting a
  patch. Strict P0 coverage still passes; no VM build/install was run this turn.

* P2's first power-case comparison asserted full text equality with main and
  failed on main's reworded comment and blank line. Fork-to-tested-upstream
  equality had already passed; PowerCaps statements are unchanged. Normalize
  comments/whitespace for the main comparison; both executable cases pass.
  The PowerShell shell continued to commit/push after that assertion failure;
  verified the comparison and clean fork status afterward. No runtime installed.

* Initial DST run had the same failures before and after: the in-tree loader
  selects build_dir/server/wineserver before considering WINESERVER, so the
  proxy's preload library was never used. Winter initial-state assertions
  failed, correctly exposing that fixture error. Archived as
  server-dst-bias-uninstrumented-server-failed, reversed both patches and
  rebuilt master. Prestart the instrumented server with proxy -p; the loader
  then connects to that server. Rerun: both initial states pass, exactly two
  transition failures before, zero after. Each scenario kills/waits its server.
* `head -65 ~/wex-refactor/build/wine` read ELF bytes because build/wine is a
  binary, not a launcher script. Discarded that output; inspected the loader
  source's exec_wineserver precedence instead. No file changed.

* Initial theme fixture called OpenThemeDataFromFile, which is a stub on fork
  master (it is implemented only in our fork changes). Wine aborted at that
  export; runner rejected EXPECTED_REGRESSION_NOT_FOUND. Archived as
  theme-token-whitespace-file-api-stub-failed, reversed tests and rebuilt master.
  Used implemented ApplyTheme/OpenThemeData and restored the original theme.
  Also corrected fixture property TextFont to the actual Font property.
* Timezone baseline confirmed the stale-rule assertion; fixed build failed
  `implicit declaration of function stat`: the port omitted sys/stat.h. Added
  the header; archived as timezone-cache-stat-header-failed and reran both
  phases after exact patch reversal/master rebuild.
* Initial timezone fixture cleanup failed `rm: cannot remove
  '/tmp/wex-timezone.6T5NLxWS/WexDST': Device or resource busy`. Capture the
  real target before binding, use --no-canonicalize, and unmount every layer
  at that exact target. After the old namespace exited, inspected the single
  leftover file, removed it with sudo rm -f, then rmdir. Actual host timezone
  hash checked unchanged. runuser now resets its normal HOME automatically,
  avoiding the prior /root/.cache permission warning.

* After prefix refresh, IE extraction returned count 2 when both sizes were
  requested together, contrary to the draft's assertion of 1. Changed the
  regression to extract each size separately (each must return 1); baseline
  then failed both counts and both-handle presence, and the fix passed.
  Evidence archived as ie-desktop-icon-two-sizes-count-failed. A tail of
  after.log before prefix refresh completed reported No such file: the test
  log is created after refresh. Use status and optional file existence guards.

* IE test compile failed because rpcndr.h defines small as char; local variable
  `HICON large = NULL, small = NULL` expanded into invalid C. Renamed the two
  variables large_icon/small_icon. Archived the failed build in
  results/ie-desktop-icon-small-macro-failed, reversed tests, and reran. Runner
  now traps command errors as COMMAND_FAILED rather than leaving a stale
  BUILDING_REGRESSION status when no process remains.
* IE fixed resource run still failed ExtractIconEx's count assertion: the DLL
  copied into the prefix predated the new resource. wineboot -u in that prefix
  changed its shdoclc.dll SHA256 from f7833def... to the build's 81ee2b82...;
  installed runtimes were untouched. Added optional update-prefix before both
  baseline and fixed runs, archived results as
  ie-desktop-icon-stale-prefix-failed, reversed both patches, rebuilt master
  and restarted. Do not refresh only the fixed phase if reusing a prefix.
* `rg -n 'UINT WINAPI ExtractIconExW|ExtractIconExW' wine/dlls/shell32/iconcache.c wine/dlls/shell32/*.c`
  failed os error 123 because rg received the wildcard literally in PowerShell.
  Read the known file and used git grep for the other definition.

* Initial shortcut run FIX_TEST_FAILED: SetPath used a simple PIDL identifying
  the temporary folder as a file, so target icon extraction still returned the
  generic icon. An isolated WINEDEBUG=+shell run confirmed that tmp extension
  path. Evidence is archived in results/shortcut-target-icon-simple-pidl-failed,
  including diagnostic.log. Added SetIDList with SHParseDisplayName's full PIDL;
  the rerun showed the intended folder failure before and none after the fix.
* An IE test patch in String.raw again contained Bash ${target%...}, triggering
  SyntaxError: Missing } in template expression before any tool call. Split the
  C patch from the Bash patch; pass Bash via a normal quoted JS string.
* `git -C build/refactor/ie-icon add -N dlls/shdoclc/iexplore.ico` rejected a path
  outside sparse-checkout. `sparse-checkout add --no-cone ...` then rejected
  unknown option no-cone (add inherits the existing mode). Used
  `sparse-checkout add dlls/shdoclc/iexplore.ico`, then add -N. Exported patch
  now contains both the new binary and resource declaration. The initial
  resource-only input was replaced while BUILDING_REGRESSION, before the fix
  patch was applied; tests were unchanged.

* Relaunch 1318032 also rejected the lock: winedevice 1317313 survived SIGTERM.
  Rechecked ps/fuser and killed only that orphan with SIGKILL; lock probe passed.
* Run 1318095 reached a fixed battery suite with zero failures, but wrapper
  returned 1 before the no-battery suite. Its redundant outside-namespace
  wineserver -k failed because inside cleanup had already stopped the server.
  Removed that redundant call, archived evidence as
  results/power-capability-battery-cleanup-exit-failed, reversed both patches,
  rebuilt master and relaunched both states. No fix was committed from either
  rejected fixture run.

* The initial battery user-namespace run started Wine as mapped uid 0. Cleanup
  outside that namespace did not find its server, which survived into the next
  fixture. Fixed tests then reported server_init_process_done assertion errors
  and process-token access failures. Stopped only runner 1317075, timeout
  1317400 and isolated server 1317202; archived evidence under
  results/power-capability-battery-userns-failed. Reversed both exact patches
  and rebuilt master. Installed server 1434 and the live desktop were untouched.
* The attempted `unshare --user --map-current-user --mount --net` fixture failed
  `mount: /sys/class/power_supply: must be superuser to use mount.` Bash exec
  loses the setup capabilities with a nonzero uid. Replaced it with sudo -n
  unshare --mount and runuser; Wine keeps its normal uid/groups.
* `bash ~/wex-refactor/power-fixture.sh none ls /sys/class/power_supply`
  initially failed Permission denied: sudo's mktemp directory had mode 0700.
  Added chmod -R a+rX for the temporary fixture before mounting; both modes
  then listed correctly as wineuser.
* Relaunch 1317887 rejected with `Another topic runner holds the checkout/build
  lock.` Five orphaned Wine services from the failed user namespace inherited
  lock fd 9. fuser and ps identified 1317206,1317259,1317289,1317295,1317313;
  stopped only those verified processes. Runner now closes fd 9 for the test
  child while retaining its own lock. Relaunch 1318032 started normally.
* Status reads immediately after rejected relaunch failed with No such file
  or directory; its output was in battery-topic.log, before result creation.

* The initial rename draft changed only the destination branch, overlooking
  that the preceding condition also includes all two-item events. Baseline
  failed eight destination assertions; the runner rejected the incorrect
  source-only expected pattern with EXPECTED_REGRESSION_NOT_FOUND before
  applying any source fix. Removed two-item events from the preceding condition
  so the both-path check is reachable, changed the expected pattern to
  destination, reversed the exact tests-only patch, rebuilt master, and reran.
  The attempted scp update arrived after the runner had already rejected the
  pattern; no running test/code patch was modified. Tests are unchanged.

* `git --git-dir=build/refactor/wine.git diff master refs/audit/wineexplorer -- dlls/shell32/changenotify.c`
  produced a misleading deletion because the project ref stores that file under
  wine/. Discarded that output; compared the fork blob with the actual project
  file and used the reviewed hunk instead. No source was edited from that diff.

* Wizard baseline produced the eight expected failures, but the runner wrote
  EXPECTED_REGRESSION_NOT_FOUND: the Windows-written failure-pattern had CRLF,
  and command substitution retained CR. Exporter now writes LF bytes; the
  runner strips CR/LF before matching. Reversed the exact test-only patch,
  rebuilt master, and reran: eight failures before, zero after.
* `ssh -vv -i D:/VirtualBox_VMs/vm_key -p 2223 -o BatchMode=yes -o ConnectTimeout=5 wineuser@127.0.0.1 true`
  stalled after offering the public key. Adding `-o IPQoS=none` restored access;
  exact cause is undetermined. No guest reboot was performed. Use that option
  with SSH/scp. The old stalled connections are closed.
* `VBoxManage debugvm ProtonExplorer-VM info os-dmesg` failed VERR_FILE_NOT_FOUND;
  `VBoxManage debugvm ProtonExplorer-VM detectos` was an invalid subcommand;
  `osdetect` worked. `osdmesg --lines=24` failed VERR_NOT_FOUND. Temporary metrics
  were disabled afterward. The ignored console screenshot is not project art.
* `rg -l -i 'guestcontrol|passwordfile|credential' docs scripts/vm.sh fe.sh`
  failed because fe.sh does not exist. No guest credentials were changed.
* `bash -n script && nohup ... & echo $!` backgrounds the entire AND list,
  retaining the SSH channel until completion. Run syntax checking and launch
  separately. The wizard connection closed after successful completion.
* The first read command this continuation used Bash brace expansion
  `build/refactor/drawtext/dlls/user32/{text.c,tests/text.c}` in PowerShell:
  ParserError, Missing argument in parameter list. Repeated with explicit paths.
* DrawText launch printed literal `$!` because the PowerShell command included
  a Bash backslash before the escaped dollar. The runner launch is independent
  of PID printing; inspect its status/lock rather than launch a duplicate.

* `rg -n 'static void test.*alpha|static void test.*draw|create_test_bitmap|check.*pixel|START_TEST|test_ImageList' build/refactor/upstream/dlls/comctl32/tests/imagelist.c`
  failed with "The system cannot find the path specified" immediately after
  worktree creation with --no-checkout. Sparse patterns did not populate the
  empty index. `git -C build/refactor/upstream read-tree -mu HEAD` populated it.
* `git --git-dir=build/refactor/wine.git show master:dlls/shell32/tests/icon.c`
  failed: `fatal: path 'dlls/shell32/tests/icon.c' does not exist in 'master'`.
  Listed the test tree and searched it; stock-icon tests are in shelllink.c.
* The first attempt to write test-topic.sh used a JavaScript String.raw template
  containing `root="${WEX_REFACTOR_ROOT:-$HOME/wex-refactor}"`. It failed before
  any tool executed with `SyntaxError: Missing } in template expression`.
  Used an ordinary quoted JavaScript string for the patch instead.
* Initial imagelist baseline command failed with
  `wine: chdir to /home/wineuser/wex-refactor/prefixes/imagelist-alpha : No such file or directory`.
  Added the prefix-parent mkdir and explicit expected-assertion matching.
  The guard command
  `test "$(cat ~/wex-refactor/results/imagelist-alpha/status)" = WAITING_FOR_BASE_BUILD && kill 1281780`
  returned 1 because the runner had already advanced. A second launch rejected
  the dirty checkout. Stopped the original runner, killed only its Wine prefix's
  server, reversed the exact input patches, rebuilt master, and reran successfully.
  Added a checkout/build flock to prevent overlapping runners/status writes.
* `python build/refactor/export-topics.py` failed with
  `ValueError: Empty patch: dlls/comctl32/imagelist.c` after that topic had been
  committed. The following
  `scp -i D:/VirtualBox_VMs/vm_key -P 2223 -r build/refactor/topics/wizard-focus wineuser@127.0.0.1:wex-refactor/topics/`
  failed because the wizard directory had not been created. Exporter now diffs
  against fork master (including committed topic changes) and accepts a topic
  argument. `python build/refactor/export-topics.py wizard-focus` and scp passed.

## Commands that failed or were interrupted

P3 installer ISO built and the interactive QEMU/KVM test passed; ISO hash and
scope are in P3_TEST_REPORT.md. VM p3-plugin was shut down normally. WSL state
uses /var/tmp/wineexplorer-vmtest, not RAM /tmp; no other VM was stopped.
The dev VM repaired runtime was reinstalled and the session restarted; its
live shell-plugin-probe reports zero failures. An unnecessary copy of the
public p3-input.tar.gz to ~/build/p3-input-validation.tar.gz on the dev VM was
not unpacked; it contains no private resources and can be discarded later.

The first check-compose.sh run produced screenshots but no stdout because
Explorer launches children without inheriting its redirected handles. Its
log-count assertion rejected the run. Launch the console probe directly after
the shell, using the configured named desktop, and remove capture.bmp before
each capture; both P2/P3 modes then pass blend/capture/input checks. Failed
evidence is checks/composition-first. The initial check-compose-own.sh failed
before launching the probe: it searched for a desktop after a fixed two-second
sleep, and xwininfo showed zero children. Poll the observed Own desktop until
visible, then use its XID; five cycles and two toggles pass with zero API errors.

Pixel analysis `python -c "... print([(p,im.getpixel(p)) for p in
[(950,450),(913,420),(919,425),(915,470),(909,472),(800,477),(658,477)]]])"`
failed with an extra closing bracket. Assign the points list first, then print
the comprehension; the corrected read-only analysis succeeded. No image was
edited. Samples outside the popup show the background color, a baseline
shadow limitation to retain in the report.

The first grouped-folder capture opened My Computer three times; namespace
navigation reused one window, so that screenshot did not validate grouping.
Use three distinct real directories for the grouping check instead.

October 13 continuation: dev build repair passed and reports zero future-dated
files. Live Explorer PID 50228 maps /usr/local/lib/wine/x86_64-windows/wexshell.dll.
The public P3 runtime was staged under stage-probe/usr/local using make install
prefix=/usr/local DESTDIR=... (never inst/build); the relocated runtime passes
tray and all eight native-dialog checks. The first staged check stopped before
launch because the earlier module rebuild had reset build.status to
MODULE_BUILD_PASSED. Repeated full make successfully, then reran the check:
CHECK_PASSED. No stage prefix/session was created by the rejected first run.

Concurrent commits 091b73e1, 531aa984 and c419eb94 add a Wayland plan/L0 lab
and prior-art notes. They are preserved; this task continues the separate
REFACTOR_WORKORDER sequence. User renewed the instruction to continue all
workorder items after the token interruption, and to commit frequently.

Dev sync first reported only 53 files and Autoconf CRLF warnings such as
`AC_CHECK_HEADERS(\\\r): you should use literals`. The legacy helper's
`git diff --diff-filter=AM` excluded rename destinations and fix-eol.sh only
processed uncommitted changes, leaving committed Windows CRLF text intact.
Added --no-renames and sync-archive.py (LF text plus Git file modes), restored
the previous generated stamp 7c5f4628d50de2a8ffd516902c68fbb84a4e95c3, and
resynced: 128 public files and 28 local-only artwork files updated without
Autoconf warnings. Enabled pipefail so a failed archive cannot advance the
stamp. No private artwork was read into or committed to public Git.

Dev VM full build passed; vm.sh install and restart-shell completed. A live
Luna desktop screenshot is under build/refactor/p3-dev-first.png. Installation
reported `make: warning: Clock skew detected. Your build may be incomplete.`
Read-only inspection found 323 old generated build files dated November 8,
ahead of the guest's October 13 UTC clock; source has no future-dated files.
Repair those generated artifacts and repeat verification before trusting this
dev build. No system clock was changed. Existing user files were preserved.
`tail ~/.wineexplorer-session.log` failed because that log path does not exist;
use the session's actual configured log path. A later /proc/50073/maps read
failed because the restarted transient desktop process had exited; re-query
current PIDs rather than treating that stale PID as the shell.
`Get-Content docs/COMPOSITION.md` failed because that document does not exist;
the maintained composition instructions are in D2_TEST_REPORT.md.

P3 `check-p3-fallback.sh` first failed compiling shell-plugin-probe.c with
`x86_64-w64-mingw32-gcc -Wall -I "$root/source/include" ...`: this mixed Wine
and MinGW headers (`unknown type name WINOLEAPI` in propidl.h), and ARRAY_SIZE
was undefined. Copy only the shared wexshell.h into the probe directory, use
MinGW's own standard headers, and use sizeof for the array count. Compilation
and enabled/disabled/version/ordered-desktop-switch checks then passed.
Log retained as /srv/protonexplorer/p3-fallback-compile-failed.log.

An apply_patch adding assemble.sh also contained a nonexistent P3.md context
`build, then connect the src module for normal` and was rejected atomically.
Reapplied the standalone file addition; no partial edit remained.
The search command naming scripts/fe.sh reported a missing file: that helper
does not exist in this checkout. vm.sh is the maintained sync helper.

P3 full build, linked module build and all P2 smoke checks pass. The API
fallback probe passes with the plugin enabled and disabled. Visual captures
of both Start styles, My Computer and Turn Off match P2 pixel-for-pixel above
the taskbar. The first click-based Classic capture showed a closed menu in
both phases; rejected it and used a Start-button message/visibility probe.
Feature captures and logs remain under each phase's checks/visual; installed
VM desktops are unchanged. Main now has the minimal host and src move;
retained fork is pushed through 43c797e7e6. Dev/composition/installer checks
remain before P3 acceptance.

P3 prototype: `git diff --cached --check` initially rejected new-file
whitespace inherited from Makefile.in, explorer.rc, resource.h,
startmenu_view.c and theme_engine.c, plus one trailing-whitespace line introduced
by extracting startup. Removed trailing whitespace/blank EOF lines from text
copies; staged diff check passes. The original Wine files remain unchanged.
`git diff --no-index --numstat wine/programs/explorer/desktop.c
build/refactor/p3-host/programs/explorer/desktop.c` returned 1 as expected for
differing files, reporting 26 additions/163 removals versus the old modified
host; this is not a compile failure. Stock-hook size must be measured against
upstream, not that modified file.

P3 prototype build launched at /srv/protonexplorer/wex-refactor-p3 (PID 1461516).
P2 source/build are preserved. New source is solely copied from committed
public c9f9440d, with mechanical DLL entry/resource ownership adjustments.
Main's original Explorer is still in place; plugin acceptance is pending.

P2 build finished with BUILD_PASSED. The first `check-assembled.sh` run failed
with `FAIL: no visible shell taskbar`: it launched the probe on the default
desktop instead of RefactorP2. A second run via `explorer /desktop=RefactorP2`
reported CHECK_PASSED but copied the earlier FAIL report before the child
completed. That result was rejected. Added report removal, completion polling
and an explicit PASS assertion. The third run still failed because a separate
Explorer launcher did not reliably run the child before polling expired.
Corrected by passing the probe as the `/shell` startup program, launched after
shell initialization on the same desktop. The fourth run (PID 1461093) passed:
two tray icons, zero overlaps/out-of-bounds; all eight native-dialog cases pass.
Desktop screenshot copied to build/refactor/p2-desktop.png and inspected.
Failed harness evidence is in checks-first/second/third; installed desktops
were untouched. No refactor runner remains active. P2 acceptance now passes.

An apply_patch update to P2.md failed with `Failed to find expected lines`
for the context `installed VM runtimes are unchanged. Next: finish this build`
followed by `and runtime verification before P3.` The actual first line begins
`builds and all installed VM runtimes`. No edits were applied. Read the tail
and reapplied using the exact second line as context; the update succeeded.

During smoke-check preparation, `rg -n 'D1|D2|Xvfb|screenshot'
docs/HANDOFF.md docs/CODEX_HANDOFF.md docs/TESTING.md scripts/vmtest.py`
reported `docs/TESTING.md: The system cannot find the file specified` because
that document does not exist. Used the maintained probe table in HANDOFF.md.
`rg -n 'ManageLinuxWindows|Composition|WexWmStart|WexWmSetWorkArea'
wine/programs/explorer/*.c wine/dlls/wexhost/*.c scripts/vm.sh` reported invalid
filename syntax on Windows because rg does not expand those positional globs.
Corrected with `rg -n -g '*.c' ... wine/programs/explorer wine/dlls/wexhost`.
Both were read-only searches; no VM/session changes resulted.

P2 background launch command ended with `& echo \$!`; Bash therefore printed
the literal `$!` instead of the PID. The build itself launched successfully.
`pgrep -af build-assembled.sh` identified PID 1329961; no second runner was
launched. An immediate diagnostic `cat .../build.status` reported
`No such file or directory` while configure was still preparing; added a
BUILD_PREPARING status before preparation for future launches. The running
copy had already advanced to BUILD_RUNNING and was left running unchanged.

October 12 shell split: `python build/refactor/retain-shell-series.py` first
stopped at `AssertionError: Plan must cover each generic hunk once` before
changing the retained checkout. The initial plan omitted two generic shell
keyboard/lookup hunks and the Properties size-formatting hunk. Added explicit
groups for those hunks; the rerun committed all eleven groups and exact file
comparison passed. The retained branch is pushed through 3fb5308752, with zero
stock paths pending. No VM commands or builds ran during this step.

1. In PowerShell:

   ```text
   git --git-dir=build/refactor/wine.git rev-parse wine-11.19^{}
   ```

   The unquoted `{}` was parsed by PowerShell, producing the spurious
   `-encodedCommand` argument and `fatal: ambiguous argument '': unknown revision
   or path not in the working tree.` The following diff in that shell still ran;
   its exit code concealed the earlier failure. The audit now passes
   `wine-11.19^{commit}` as a Python subprocess argument, obtaining the correct
   release commit `455e3509b98a6919fd4ad1def4803e08c41c03b2`.

2. Interrupted this long history search with Ctrl+C (exit 1):

   ```text
   git --git-dir=build/refactor/wine.git log -5 --format='%h %cs %s' --find-object=3629c0f846 -- dlls/bcrypt/bcrypt_main.c
   ```

   It found the October 5 bcrypt commit, then continued searching the entire
   history for four more matches. Use bounded post-release raw history instead.

3. Interrupted the first provenance-enabled audit invocation with Ctrl+C
   (exit 1):

   ```text
   python scripts/refactor-audit.py --git-dir build/refactor/wine.git --output build/refactor/baseline
   ```

   Its internal `git rev-list --objects master ^wine-11.19` caused repeated
   lazy blob fetches in the partial clone. Replaced it with `git log --raw
   --no-abbrev --no-renames master ^wine-11.19`, which reads change metadata.
   The corrected audit and exact-equality verification pass.

The first tag fetch omitted `--no-tags` and auto-followed historical Wine tags.
It succeeded; only the local ignored bare clone was affected. Reproduction
commands now use `--no-tags` to fetch only the requested release tag.
