<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Wayland L1 Wine integration

Status: in progress, 2026-10-13. Acceptance is not yet claimed.
Scope: WAYLAND_INTEGRATION_WORKORDER.md; shipping X11 stays unchanged.

## Environment

Compositor development uses ProtonExplorer-VM (SSH 2223). Source/build is
~/wexcomp-build, candidate codex/wex-window-handles at 192ab62b, from
wexcomp ad962566bd169c3c27fafdaac9780a599fb7624a.
Debian wlroots 0.18.2 development packages installed; meson/ninja build passes.
No dev runtime or ISO defaults are changed. Gemini's WSL wexcomp/wexbuild
distributions are untouched. Wine edits use the separate host fork worktree
build/wayland/wine, branch codex/wayland-integration, now 3bb585c4ee. It includes
the concurrent production f5016cf theme fix. Owned source is on the experimental
project branch codex/wayland-l1 at 1e94efb9; master still ships X11.
Final WoW64 build is /srv/protonexplorer/wex-wayland-l1/wow64-source/build/wine64,
configured with --enable-archs=i386,x86_64. Historical win64-only evidence below
predates the new mandatory build rule; final WoW64 acceptance remains pending.

## Integration finding

wex-window-v1 v1's sibling arguments are connection-local objects. Sharing the
privileged socket does not permit a Wine process to reference another process's
Wayland window object. Relative cross-process stacking needs a protocol extension
with shared window handles. The user approved Codex implementing the compositor
extension on 2026-10-13. Candidate branch: codex/wex-window-handles in the Proton
VM clone. No Gemini WSL or shipping desktop changes.

## Command failures and corrections

- Reading docs/WAYLAND_L1.md failed because it did not exist yet; this report
  creates the required acceptance record.
- Initial pkg-config check failed for wlroots-0.18; install libwlroots-0.18-dev
  and compositor dependencies. Meson/ninja then pass.
- git worktree add --no-checkout plus sparse-checkout set did not populate the
  source files yet, so reads of the new window.c/wayland_surface.c paths failed.
  Explicit checkout codex/wayland-integration populated them.
- Reading src/dlls/wexshell/taskbar.c failed; the taskbar implementation is
  systray.c. Searches subsequently use the module directory.
- First compositor acceptance run failed with ModuleNotFoundError: PIL in
  tests/wex/verify.py. Install Debian python3-pil and rerun. The runner stopped
  its disposable headless compositor normally; no shipping session was touched.
- The next compositor run reached the shell-layer pixel check but raised
  IndexError: the output was still at the backend's default size because
  wlr-randr was missing and the runner suppresses that command's failure.
  Installed wlr-randr; the complete existing suite now passes 25/25.
- Initial Wine driver compile rejected Unix swprintf and L-string arguments:
  host wchar_t is 32-bit, Wine WCHAR is 16-bit. Use ASCII formatting and UTF-16
  u literals. Linking then rejected RtlOpenCurrentUser, which is not exported
  by Unix ntdll; use native token/SID lookup and NtOpenKey instead. Driver builds.
- Reading src/dlls/wexshell/plugin.c failed; exports are in desktop.c.
- The first placement capture still showed centered windows. Move config
  initialization before toplevel creation, send the requested zone position
  before its initial commit, and ignore the initial add-item position event.
  Placement acceptance remains pending the corrected capture.
- The stationary two-process comparison then found the first process centered
  while the second placed correctly. NtCreateMutant returns the successful
  informational STATUS_OBJECT_NAME_EXISTS when another process holds the
  creation guard; the initial nonzero-status check wrongly disabled zones.
  Accept all nonnegative NTSTATUS values, including abandoned-wait recovery,
  and rerun. Minimized sentinel rectangles are excluded from placement requests.
- Corrected placement passes: six stationary windows exactly match X11 pixels;
  a second process shares the same opaque handle and lands at (740,50).
  Upstream zone branch aed8b9599c is pushed; retained integration commit
  853c5b02c7 is local on codex/wayland-integration. Production pin is unchanged.
- Private patch transfer failed because directly SCP'd driver files contained
  mixed CRLF/LF, so Git's LF patch context did not match. No partial patch was
  applied. Transfer the complete changed text inputs with LF-normalizing tar
  instead. The following shell build then stopped in makedep: wexwayland.h
  existed but was not listed in include/Makefile.in. Register the header;
  driver, wexshell and stock Explorer host then build successfully together.
- git sparse-checkout add --no-cone rejected the unknown option; add uses the
  existing mode. The following read/patch for include/Makefile.in failed since
  it had not been populated. sparse-checkout add include/Makefile.in works.
- First native /shell session exited with "Failed to create PIDL for Z:\\shell".
  The updated host executable/prefix hashes match, but wexshell's command
  dispatcher intercepts commands before host parsing and still treats /shell
  as a browser path. Exclude the /shell token just as /desktop is excluded.
  Its compositor and nested Xvfb stopped normally. A screenshot attempt after
  exit failed to create display, then SCP/view failed because no PNG existed.
- The candidate fork commit staged include/Makefile.in but Git refused the new
  include/wexwayland.h outside the sparse patterns. The shell/driver commit
  ce019d67c7 and host commit d7d46937a3 were followed immediately by explicit
  git add --sparse and header commit 996f87f33f. Candidate head is complete and
  pushed; production pin is unchanged. Never use the intermediate candidate
  without its header commit.
- Native /shell now starts successfully at 1024x768. The public-resource desktop
  and taskbar are visible at the bottom; candidate driver and wexshell loaded.
  Session /srv/protonexplorer/wex-wayland-l1/checks/sessions/session.4h50U8
  is running nested on Xvfb :20 for acceptance; shell/activation checks ongoing.
- Archive generation later failed on include/wexwayland.h after a sparse pattern
  update hid the newly tracked header. PowerShell continued with a partial tar;
  only the isolated lab source was affected. The generator now reads tracked
  sparse-hidden content from HEAD, and a complete tar/rebuild was repeated.
- The first action sequence failed its focus assertion: commands read state
  immediately and the new probe launched another Notepad while an earlier one
  remained. Reuse an existing Notepad, poll foreground state after real clicks,
  and wait only for the finite topmost client rather than the long-lived probe.
  Activation now passes, including WS_EX_NOACTIVATE after the two-line generic
  win32u click-style check. Windows behavior reference:
  https://devblogs.microsoft.com/oldnewthing/20240919-00/?p=110283
  That fix and X11 fallback validation are still candidate work.
- Failed reads of warp/maximized reports and SCP/view of topmost.png occurred
  after the failed assertion stopped their generation; no image acceptance was
  claimed. A small patch using the wrong ${wine_bin} text failed to match the
  fixture; corrected to its actual $wine_bin spelling.
- A later docs/DWM* and docs/COMPOSITION* rg call failed because PowerShell
  does not expand those file globs for rg. Use a directory with -g instead.
- Successful checks: work area (0,0)-(1024,738), taskbar (0,738)-(1024,768);
  physical clicks select red/blue, green NoActivate preserves red;
  topmost yellow overlays maximized Notepad but not the taskbar; pointer warp
  returns (950,600). Restore/map activation is now deferred until a buffer commit,
  with a Win32 foreground check to discard stale activation requests. Retesting.
- Taskbar-click minimize initially restored Notepad immediately. Trace shows the
  driver's preexisting heuristic treats any configure while minimized as a
  compositor restore. For private wex views, acknowledge such configures without
  SC_RESTORE: this compositor restores only on explicit activation. Actual taskbar
  clicks now pass minimize (IsIconic=1) and restore (IsIconic=0, foreground Notepad);
  keyboard Alt+F opens its File menu. Status TASKBAR_CHECK_PASSED.
- The first X11 comparison failed its NoActivate assertion, then the adjusted run
  failed the tray probe. The named /desktop=X11Check applies only to Explorer;
  terminal probes were joining a different default desktop. Set the prefix's
  Explorer Desktop/Desktops registry entries before launching every process;
  repeat the old/new X11 comparison and the original 800x600 smoke fixture.
- The corrected X11 comparison passes normal activation and tray bounds. Old
  runtime activates NoActivate green; the corrected core preserves red. The
  800x600 smoke fixture first timed out during fresh-prefix wineboot (no tray
  report yet); repeat on the initialized prefix passes tray bounds and all eight
  native placement cases (checks/status CHECK_PASSED, x11-smoke-final.log).
- Maximized Notepad initially left a four-pixel gap. Wayland configure sizes
  describe xdg window geometry, not Win32's full rectangle. The first correction
  used visible/window differences, which are zero here; using WS_THICKFRAME
  alone then produced a two-pixel deficit. Include WS_BORDER, preserve the
  maximized resize borders, and track xdg geometry offsets in zone coordinates.
  The final correction sets the private maximized rectangle from SPI work area
  plus the resize borders and clears SWP_NOMOVE. Raw Notepad rectangle is now
  (-4,-4)-(1028,742); its visible frame exactly meets (0,0)-(1024,738).

## Additional verification and continuation

- Full isolated Wine build completed: full.status FULL_BUILD_PASSED. The new
  private version 2 driver also compiles successfully; runtime checks pending.
- Current 100% positions match X11 exactly. At 150%, six windows and a second
  process match within one pixel. The fractional output is 1280x799 versus the
  X11 1280x800 baseline; the checker now allows the requested one-pixel tolerance
  for image dimensions as well as rectangles.
- Native shell checks pass click activation, NoActivate, actual taskbar minimize
  and restore, menu input, taskbar-over-topmost, alpha blending, and work area.
  A pointer-entered probe reports SetCursorPos success and subsequent screen
  motion (950,600). Stock labwc fallback opens Notepad and its File menu.
- Mixed foot and XWayland windows render alongside maximized Notepad with the
  taskbar visible. X11 smoke passes all eight native placement cases and tray
  bounds. D1 own-compositor enable/disable passes five cycles. D2 records click,
  requested shadow, opacity/color-key rendering and completed 900 ms blend
  (elapsed 921 ms, styles/attributes restored); shadow pixels were not asserted.
- Version 2 compositor builds; all existing v1 clients pass 25/25 checks.
  Three independent version 2 clients now exercise relative ordering, self,
  stale/unknown handles and cross-layer no-ops. All 26 checks pass, including
  restore without keyboard focus. Compositor candidate heads ebdbf66/192ab62.

Additional command failures/corrections:

- git apply from the Wine worktree could not open the relative host patch path;
  use the absolute D:/DeepThought/Projects/WineExplorer/build/wayland path.
- The fractional checker initially rejected 1280x799 output before examining
  window rectangles. Allow image-size differences within --tolerance as above;
  the repeated position comparison passes. Pillow warned that getdata will be
  deprecated; use get_flattened_data when available with an older-version fallback.
- Restoring owned source files left stale Git modification metadata but no diff.
  git add of those exact four files refreshed the index; no source change staged.
- Cached GetCursorPos alone did not establish actual pointer motion. Added an
  entered-window warp probe and observed its motion event. The first alpha
  capture was after its 30-second client exited; repeat while alive shows blend.
- pgrep found no XWayland process because it starts lazily. ss identified the
  compositor's X1 socket; launching xmessage on DISPLAY=:1 started it correctly.
- rg is absent on the Debian build VM; use grep there. The exploratory test*.c
  read also found no such filenames: actual clients are tests/wex/*-client.c.
- cat full.status returned missing while the full build was still running; it
  now contains FULL_BUILD_PASSED. Only the opt-in nested lab shell remains running
  on Xvfb :20; shipping VM and build VM's existing X11 desktop are unchanged.

## Later fixes and host restart

- GitHub push from the guest failed with no Username credential. Export its
  commits in a bundle and push from the host. The first fetch into an empty
  host bare cache failed on missing parent 737325aa: the guest clone is shallow.
  Fetch the public wexcomp base history first, then import and push the bundle.
- A new composition comparator initially expected pure blue, but compose-probe
  paints RGB(0,80,255). Correct that fixture color; rectangles and alpha regions
  now match X11. A separate comparison hung bootstrapping an X11 prefix because
  its wrapper omitted WINELOADER; export the build loader explicitly, stop that
  isolated prefix/server/Xvfb :24, and repeat under a new script filename.
- Reading host restored.txt/maximized.txt failed because those guest reports
  had not been copied; read the guest checks directory instead.
- Extra click/pixel checks exposed intermittent blank clients and stale focus.
  Queued WM_WAYLAND_SET_FOREGROUND acknowledgements echoed Wine's own private
  activation requests, repeatedly alternating old windows. Candidate 89735ec1ef
  ignores those acknowledgements only when the private manager is present.
- Initial GDB PID selection matched the pre-exec loader command and missed the
  renamed PE process; use its actual executable path. GDB then reported no
  shared libraries due to Wine's preloader. Load symbols at their /proc/maps
  bases; native backtraces show the repeated foreground/nonclient operations.
  GDB's libthread_db auto-load warning was left alone; no global debug policy changed.
- Background ShowWindow restore clears Win32 minimization but foreground-lock
  rules can deny SetForegroundWindow from the controller. Version 2 now has
  restore() without activation, tested with native clients. Pending activation
  also flushes against an existing committed buffer after restore, without
  depending on a new paint. Additional click/hit-test ordering checks continue.
- Desktop activation bypassed WM_WINDOWPOSCHANGING using SWP_NOSENDCHANGING,
  leaving Wine hit testing inconsistent with the compositor's bottom band.
  The shell now reasserts HWND_BOTTOM in WM_WINDOWPOSCHANGED. Experimental
  project checkpoint ab4a1691 preserves this alongside the role changes.
- A three-second startup delay found no probe/Notepad in a later run. Poll all
  four window titles before issuing clicks. Failed captures/assertions were not
  counted as acceptance. A covered-window click now also checks that compositor
  activation and Wine's logical hit-test order remain consistent.
- On the dev VM, transferring the Linux probe through Windows lost its execute
  bit: Permission denied. chmod +x fixes it. The 800x600-native fixture was also
  wrongly run at 1024x768; its hard-coded 800x570 limits correctly reject that
  capture. Separate the native 800x600 run from 1024x768 focus/tray checks.
- The first D1 dev check could not find its desktop after three seconds. Poll
  visible D1Cycles/Wine desktop titles. Re-running wineboot -u on an already
  initialized X11 prefix also hit BadWindow; bootstrap only missing system.reg.
  Dev isolated X11/native checks, D1 five cycles and D2 capture then pass.
- A staged public runtime was created from this lab build (not ~/inst/build)
  using DESTDIR under /srv/protonexplorer/wex-wayland-l1/staging, then unpacked
  into ~/wex-wayland-l1/runtime on the dev VM. Its installed /usr/local runtime,
  real prefix and visible desktop were never replaced or restarted.
- The Windows host restarted. Both dev/build VMs are running again; old nested
  test sessions stopped. Uncommitted driver/probe edits and saved screenshots
  survived. Main advanced concurrently to a1210c5d, with Wine f5016cf and the
  mandatory WoW64-build rule. The next build must incorporate that theme fix and
  use --enable-archs=i386,x86_64; previous win64-only evidence is historical.
- The restart-time rg docs/CODEX_HANDOFF* read failed because PowerShell does not
  expand positional globs; use rg docs -g 'CODEX_HANDOFF*'. Guest i686 MinGW was
  absent; install gcc-mingw-w64-i686 for the required WoW64 build.
- The first current-project archive named nonexistent configs/include paths;
  Git rejected configs. Archive the complete tracked candidate instead. Reading
  scripts/refactor/assemble.py also failed: the current wrapper is assemble.sh
  calling scripts/assemble.py. The fresh WoW64 source was cloned separately,
  assembled with that wrapper and never reset over the older assembled tree.
- WoW64 configure warned that x86_64 C++17 was unavailable. Install the matching
  g++-mingw-w64-x86-64 package alongside i686 GCC/G++; reconfigure after the
  current build stops, then compile the newly enabled modules too.
- A multi-file report patch matched the helper but failed the report's combined
  paragraph context. Read the actual first lines and patch the report separately.
- Post-restart click/covered-window checks pass with 624720e2b6: background restore
  is visible without focus theft, real clicks raise it, and clicking covered
  pixels stays in Notepad. Private activation now synchronizes Wine's logical
  z-order with the compositor raise. The f5016cf merge is 3bb585c4ee.
