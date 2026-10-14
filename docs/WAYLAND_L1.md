<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Wayland L1 Wine integration

Status: complete, 2026-10-14. Accepted integration is merged and pushed.
Scope: WAYLAND_INTEGRATION_WORKORDER.md; shipping X11 stays unchanged.

## Environment

Compositor development uses ProtonExplorer-VM (SSH 2223). Source/build is
~/wexcomp-build, candidate codex/wex-window-handles at 9a47d74, from
wexcomp ad962566bd169c3c27fafdaac9780a599fb7624a.
Debian wlroots 0.18.2 development packages installed; meson/ninja build passes.
No dev runtime or ISO defaults are changed. Gemini's WSL wexcomp/wexbuild
distributions are untouched. Wine edits use the separate host fork worktree
build/wayland/wine, branch codex/wayland-integration, now 05286f7c90. It includes
the concurrent production theme commits through 70e6378514 and the verified
Wayland integration through ea0654efdb. wineexplorer/11.19 is fast-forwarded
to 05286f7c90; compositor wexcomp to 9a47d74f. Owned source and the Wine pin
are merged into project master through 2853f509. Master still defaults to X11.
Final WoW64 build is /srv/protonexplorer/wex-wayland-l1/wow64-source/build/wine64,
configured with --enable-archs=i386,x86_64. Historical win64-only evidence below
predates the new mandatory build rule. Final WoW64 results are below.

## Final WoW64 acceptance

All tests use public resources and isolated prefixes. Recorded results on the
ea0654efdb runtime, followed by a complete combined 05286f7c90 WoW64 build
and click, native focus, composition, 32-bit Notepad and shell capture rechecks:

| Requirement | Result / evidence |
|---|---|
| Full WoW64 build, both MinGW C++17 compilers | FINAL_WOW64_BUILD_PASSED; staged install and runtime package complete |
| Shared coordinates, independently launched second process | Seven placements exactly match X11 at 100%; within 1 px at 150% |
| Cross-process stacking, mixed bitness | WIN32_SHARED_STACKING_PASSED; 64-bit A/C and 32-bit B, relative orders, topmost and no focus theft |
| Native `/shell`, desktop bottom, work area | 1024x768 desktop, taskbar at y=738, SPI_GETWORKAREA=(0,0)-(1024,738); no display-mode call |
| Click activation and covered-window input | CLICK_RAISES_AND_DESKTOP_BOTTOM_PASSED; NoActivate preserves foreground; restored Notepad receives a covered-area click |
| Taskbar minimize/restore, menus, 32-bit program | NOTEPAD32_TASKBAR_AND_MAXIMIZE_PASSED; real taskbar clicks, Alt+F, maximized visible frame stops at y=738; menu-wow64.png/maximized-wow64.png |
| Topmost and shell layer | Topmost probe above maximized Notepad and below taskbar; topmost-wow64.png; Start menu opens in start-wow64.png |
| Alpha, color key and requested rectangles | COMPOSE_PLACEMENT_AND_ALPHA_PASSED; five rectangles within 1 px; per-pixel ramp, 50% blue and key hole match X11 within one color value; compose-*-wow64.png |
| Pointer warp | POINTER_PROTOCOL_AND_MOTION_PASSED; actual wp_pointer_warp_v1 request and motion at (950,600) |
| Native Wayland keyboard focus roundtrip | NATIVE_FOCUS_RETURN_PASSED; Wine receives a/c, foot receives b; queued deactivation cannot clear newer activation |
| XWayland alongside Wine | XWAYLAND_AND_WINE_FOCUS_RETURN_PASSED; mixed-xwayland-wow64.png |
| Stock labwc / private v1 fallback | STOCK_CAPTURED / V1_FALLBACK_PASSED; menu screenshots, v1 minimize/restore without v2 handles |
| Compositor protocol regression | 26/26, including three independent v2 clients, stale/self/cross-layer isolation and non-raising inactive restore |
| X11 on build and dev VMs | Focus/NoActivate/tray bounds pass; eight 800x600 native-dialog cases pass after readiness polling |
| X11 D1/D2 on build and dev VMs | Five D1 cycles pass; D2 blending and restored styles/attributes pass; d2-effects-wow64.png |

CS_DROPSHADOW pixels under Wayland remain the known L0 gap (D3 work), and are
not claimed here. This is an opt-in test session; no ISO or shipping session
default changes. Existing screenshots without a WoW64 suffix are historical;
the table distinguishes them from final WoW64 evidence.

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
- The first mixed-bitness stacking run started while fresh-prefix wineboot was
  still finishing: initial screenshot was black and cleanup of StackB timed out.
  Wait for a visible Shell_TrayWnd before testing, and make timed-out cleanup
  continue through all clients. Warm retry passes with StackB reporting bits=32.
- A stricter pointer assertion found no warp trace because it enabled only
  waylanddrv; wayland_pointer.c uses the cursor channel. Enable +cursor and set
  the probe's class cursor explicitly. Confirm WARP_BEFORE (800,500), the actual
  wp_pointer_warp_v1 request (local 250,200; screen 950,600), and motion (950,600).
- The final WoW64 build, including both C++17 compilers, passes at
  wow64-final.status WOW64_COMPLETE_BUILD_PASSED. Do not use the first-pass
  configure that omitted the x86_64 C++ modules.
- A native foot focus roundtrip passed its initial 'a' but failed to deliver 'c'
  after clicking an already-active Wine root window again. Keyboard leave left
  its owning Win32 input queue active. A private-only queued deactivation now
  checks the current keyboard/foreground state and an activation epoch before
  clearing the owning queue; old leave events cannot undo a newer Wine activation.
- That driver build first failed on implicit declaration of the existing static
  wayland_keyboard_get_focused_hwnd helper. Export it internally and declare it
  in waylanddrv.h; targeted build then passes. The first wrapper lacked set -e
  and started the old lab binary after the failed make; stop that isolated prefix
  and use a strict wrapper before the corrected build/session.
- Native foot roundtrip and repeated click/covered-window checks now pass on
  WoW64 with ea0654efdb. The root probe receives 'a' before foot takes focus and
  'c' after clicking back; 'b' stays in foot. The private activation epoch prevents
  queued focus-loss messages from clearing a newer activation.

## Replaying the maintained probes

### Restart recovery and final fixture corrections — 2026-10-14

- The restarted host has both VMs running. The opt-in WoW64 Wayland session
  uses Xvfb :30 and checks/sessions-wow64/session.XcERjE; production X11 is untouched.
- Composition comparison first failed because Notepad covered the background,
  while the X11 reference used the desktop. Minimize Notepad before capture.
  The next capture preceded painting despite valid rectangles; wait for
  `probe ready` and two seconds. Corrected placement/alpha comparison passes.
- The first 32-bit Notepad taskbar fixture left stale composition windows and
  failed its minimize assertion. Stop only the isolated prefix and restart.
  An immediate repeat still failed; the warm fresh-Notepad repeat passes actual
  taskbar minimize/restore, foreground return, Alt+F and maximized work-area bounds.
- Staged make install builds extra SDK import libraries after make all; it
  completed successfully. The public WoW64 runtime archive is about 543 MB.
- Dev-VM native 800x600 dialog test initially failed `LibreOffice-sized dialog:
  not hosted`; its six-second sleep did not establish shell readiness. Poll the
  visible Shell_TrayWnd before creating dialogs. All eight cases then pass.
- After restart, an attempted /srv/protonexplorer/wex-wayland-l1/wow64-phase.sh
  invocation failed because the ignored helper was not stored there. Upload the
  required helper explicitly to /tmp. Reading traytasks.c failed because this
  shell's taskband implementation is taskband.c; subsequent reads use it.
- Final WoW64 XWayland xmessage and 32-bit Notepad render together; clicking
  back restores foreground and Alt+F input. XWAYLAND_AND_WINE_FOCUS_RETURN_PASSED.
- Fetching Wine's branch did not create an origin/wineexplorer/11.19 tracking
  ref in the sparse worktree, so two log/diff commands failed. Inspect FETCH_HEAD.
  Concurrent visual-style commits through 70e6378514 merge cleanly; combined
  build acceptance is pending. A compositor ls-remote using origin failed because
  its bare cache has no origin; use the explicit public repository URL.
- First combined-build transfer used TarInfo's default 1970 timestamp; make
  skipped the changed sources and printed success without compiling the new
  msstyles_v4 file. That status is rejected. Set current archive timestamps,
  re-extract and rebuild; Makefile contains both architectures' new module and
  CURRENT_THEME_WOW64_BUILD_PASSED follows actual compilation. Tar warned that
  host timestamps were 2-3 seconds ahead of the VM; make completed correctly.
- First combined-session click run reached the readiness deadline before the
  shell was visible; its log was empty and later phases were not run. Warm repeat
  passes click/covered input, native focus and composition comparison. An attempted
  explorer.log read failed; the launcher's file is shell.log. Combined-build
  32-bit Notepad taskbar/menu/maximize and Start/topmost captures also pass.
- SHA-256 source manifest matches all 25 integration/theme files to 05286f7c90;
  the assembled checkout's old Git version string is not its effective source
  identity. checks/source-candidate.txt records the verified candidate. Do not
  reset the assembled tree. Final nested session.Y9Dz8L/Xvfb :30 is stopped;
  isolated Wine/compositor exited, both real VM desktops remain running.

The helper scripts are test tools, not session/ISO defaults. Compile dependencies:
both MinGW GCC architectures; capture/input dependencies: grim, ImageMagick,
xdotool, foot and Python Pillow. Use only public source/resources and an isolated
prefix. Do not run these in the user's real desktop prefix.

Start scripts/wayland-session.sh with WEX_WINE_BIN, WEX_WINESERVER_BIN and
WEX_WAYLAND_PREFIX selecting the WoW64 lab runtime/prefix. Its log prints the
session directory. In a second terminal set:

```bash
export WINEPREFIX=/srv/protonexplorer/wex-wayland-l1/prefix-shell-wow64
export WEX_WINE_BIN=/srv/protonexplorer/wex-wayland-l1/wow64-source/build/wine64/loader/wine
export WEX_WINESERVER_BIN=/srv/protonexplorer/wex-wayland-l1/wow64-source/build/wine64/server/wineserver
export XDG_RUNTIME_DIR="$session/runtime" WAYLAND_DISPLAY=wex-shell-0
export WEX_L1_XDISPLAY=:30 # nested Xvfb backing this particular lab session
unset DISPLAY
bash scripts/wayland-l1-check.sh stack /tmp/l1-stack
bash scripts/wayland-l1-check.sh click /tmp/l1-click
bash scripts/wayland-l1-check.sh compose /tmp/l1-compose
bash scripts/wayland-l1-check.sh pointer /tmp/l1-pointer
bash scripts/wayland-l1-check.sh mixed /tmp/l1-mixed
```

Set session to the exact directory printed by the launcher first. Stack uses two
64-bit processes and one 32-bit process and logs their bitness. Click checks
actual pixels, foreground, NoActivate, desktop-bottom behavior and a second click
in an area covered by restored Notepad. Pointer checks an entered, visible-cursor
window, the cursor debug channel's real protocol request and resulting motion.
Mixed checks input returning from a normal-socket foot client to an already-active
Wine root window. All these fixtures use the lab's 1024x768 output at scale 1.

For the X11 comparison, launch the same --stationary composition probe in an
isolated 1024x768 named X11 desktop, with all processes sharing its Desktop and
Desktops registry entries. Capture with the compose phase (DISPLAY set,
WAYLAND_DISPLAY unset). Compare its compose.png/compose-rects.txt with the Wayland
outputs using scripts/check-compose-placement.py. Position/150% comparisons use
scripts/wayland-position-probe.c and scripts/check-wayland-positions.py; the latter
checks a second process at (740,50) as well as six primary windows.
