<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Installed Wayland session

In progress, 2026-10-16. This follows `WAYLAND_SESSION_WORKORDER.md`.
Parts A and B lead to review point 1; tray integration and final regressions
follow that review. No acceptance is claimed yet.

### Installed-program and packaging checkpoint

Integration candidate: production Wine and the main gitlink now advance to
0f25ee1; compositor is 2bde47ee. The isolated assembled Wine source was audited
again (all 12,462 stock files match 0f25ee1), current owned master source was
synced, and its combined build passes (`session-iso-candidate-wow64.log`).
The preceding old-production d92d7e4 compatibility build also passes. The ISO
build uses `/srv/protonexplorer/wayland-session-iso-build`, with its output
and packaging temporaries on the large /srv/protonexplorer volume, not the
nearly full root disk. Fresh installation is still pending; this is not B
acceptance. The earlier source-pin and disabled-controls notes below are history.
The Wayland installer test uses a separate blank 40 GiB disk and 30,000 MB
partition so the subsequent real application installations fit. The default
X11 installer test keeps its 20 GiB disk and 15,000 MB partition.
Development-overlay shutdown exceeded the explicit 50-second wait while
systemd waited for session-c5.scope's 90-second stop deadline. No disk inspection
was attempted; wait for QEMU to exit before mounting its disk.
It subsequently exited normally; the QMP socket is gone and Debian WSL has no
remaining QEMU process. The stopped overlay is preserved for reproduction.
The fresh-install runner refreshes apt metadata before installing its real foot
client, and the existing shared-app check closes its earlier identical-caption
Test32 fixture before launching Alice's common app. These prevent dependence on
the development VM's populated package cache or an old test window. The fresh
ISO run remains the next verification of those setup paths.
The development Steam check used `-no-cef-sandbox` in this disposable VM; normal
launch without that override is still to be checked on the fresh installation.

The real installed-session action runner now passes on the development overlay
with full-window dragging **off** (the actual SPI value is logged): foot input,
move, edge shrink, corner grow, exact work-area maximize/restore, taskbar
minimize/restore, Notepad input, native Alt+Tab and closing both programs.
Evidence: `installed-actions.log` and `installed-action-steps/` under the ignored
directory. The test only accepts completed gestures (no pointer capture), not
an intermediate preview. Its first OCR check failed on inserted spaces around
terminal hyphens; normalizing punctuation/spacing and moving the cursor off
the text fixes that reading, without accepting incorrect letters.

That test exposed a real outline-resize failure. Foot rounded the penultimate
requested width 641 to 636 and supplied acknowledged content. The final pointer
position requested 636 again; with no new content needed, foot only acknowledged
it. The outstanding transaction timed out and rolled the entire gesture back.
The host now coalesces a new intent already satisfied by its current ready
frame, retaining the existing acknowledged-content transaction. This changes
neither compositor readiness nor state-only publication rules. The real full
action sequence passes with this fix. Temporary sizing traces established the
exact request/response sequence; those verbose traces were removed.

Control Panel's one-line sentinel correction is verified live and committed
as Wine topic 0f25ee1. `control-panel-fixed.png` shows all 13 visible entries.
Both architectures build with current master-owned code, and the real production
Wine d92d7e4 was rebuilt before the next main push. The compositor package was
also reproduced at the new 2bde47ee pin. Source scp preserved Windows CRLF and
made its first exact Git comparison fail; an EOL-insensitive comparison confirmed
only line endings differed, then the two audited files were restored from the
pinned Git archive before packaging.

Steam's actual XWayland sign-in CSD window has one hosted taskbar item and passes
input, moving, taskbar minimize and restore. No credentials/sign-in were
submitted. Evidence: `steam-controls.log`, `steam-input.png`, `steam-moved.png`,
`steam-minimized.png`, `steam-restored.png`. The initial exact drag delta assertion
ignored the CSD drag threshold (actual +92,+55 for a +100,+60 pointer drag);
the bounds check now verifies that movement occurred. Clicking elsewhere too
soon after fast typing truncated the visible field; allowing input to settle
before the focus click produces the expected text. The first OCR also misread
the insertion cursor as a letter. Steam's bootstrap logs mmap allocation errors,
but its fully rendered sign-in client and window actions run successfully.
Steam remains a test-only installation, absent from the ISO.

Another tooling failure: running archive-pin-check.py with Linux Git against
a Windows absolute-path worktree failed “not a git repository”. Windows Python
and Git regenerated both pin/restore archives correctly. The accidental use of
the older archives was discarded as verification; the fresh archive run completed
the production-pin build. The source is restored to the topic afterward, while
its binaries are production until the next topic build. The Welcome selector's
first OCR check failed on its blue focused highlight although it visibly selected
Wayland; the test defocuses the combo before reading it.

Two unit-scale outputs now pass the real foot move/maximize check. The test
VM uses wlroots' DRM+headless backends, with Virtual-1 and HEADLESS-1 each
1280x800, laid out at (0,0) and (1280,0). This is compositor output simulation
on the installed system, not a synthetic client. `grim` captures both outputs;
QEMU's USB tablet spans their union (`VMTEST_INPUT_SIZE=2560x800`), while its
primary screendump remains 1280x800. The vmtest override affects input only.
Exact maximized Win32 frame bounds are (-4,-4)-(1284,774) on the primary,
and (1276,-4)-(2564,804) on the second, which has no taskbar reservation.
Screenshots `two-output-primary-max.png`, `two-output-moved.png`, and
`two-output-secondary-max.png` plus `two-output-check.log` show the real result.

The first run failed: Wine moved the proxy to x=1568, but the compositor
rejected placement outside its first-output zone, leaving foot visibly on
the primary. Compositor 2bde47ee uses the existing protocol's infinite zone
policy for private Wine desktop clients, still anchored at the preferred
output. Requests must intersect the actual output layout. Public native zones
retain their per-output policy. No new protocol or Wine hook was required.

The settled Control Panel still has blank contents. Read-only enumeration of
its actual list control found 13 items and item-zero bounds 0,0,65535,65535.
The shell view passes MAKELPARAM(-1,-1), which is unsigned 0xffffffff on
64-bit Wine, while comctl32 expects the pointer-sized -1 sentinel for default
icon spacing. A one-line topic fix uses (LPARAM)-1; live verification follows
the current build. This explains the blank view without assuming a compositor
repaint failure. The existing readback helper gained child/layout inspection
for this real listed-program failure, not another synthetic test.

Additional failed commands: the first compositor scp/build paths omitted
`wow64-source`; repeating with the actual assembled-root path builds cleanly.
The readback compile omitted -lgdi32 and failed to link CreateSolidBrush and
DeleteObject; the correct link succeeds. An image read raced the still-running
screenshot command; waiting for completion produced the image. Searches for
waylandhost.c under wexshell and explorer.c under wexshell failed: these belong
to wexhost and wexbrowse. The background grim apt process stopped on terminal
input during man-db triggers, so a simultaneous Steam install hit its dpkg
lock. Bringing that original job to the foreground completed it; the subsequent
Steam install ran with setsid and /dev/null stdin and completed. No package
manager lock was removed. Steam's GUI bootstrap is test-only, not an ISO asset.

The existing combined-WoW64 checks pass in Bob's actual Wayland session:
the existing Test32/Test64 programs and both syswow64/system32 Notepads draw
their distinct windows. `check-wow64.sh` now uses `session-run.py` to inherit
the named user's running Explorer environment, including its private Wayland
endpoint. It drops Wine's process-local startup FD/reservation variables.
The optional `--programs-only` explicitly excludes the S4 multi-user assertions
on this legacy-prefix development overlay; the default still requires them.
Evidence: `build/wayland-session/wow64-wayland.log` and `wow64-steps/`.

Winecfg opens its Applications page. The real 32-bit 7-Zip 26.04 installer
reports completion and its installed 7zFM.exe opens a file manager. It is an
ignored, test-only download, never an ISO/repository resource (SHA256
39bf65045153fc26c42fa9fe47afdaf8cd1b11671c7f11d031e738c9a13960c009).
The automated destination assertion failed: the text had only reached
`C:\users\bobsmith\7-Zip-S` when Install was clicked, instead of the requested
`7-Zip-Session-Test`. The actual installed executable at the shorter path was
then launched and visually verified. The later OCR wait used “7-Zip File
Manager”, while its actual caption is “7-Zip”; screenshot and window readback
verify the file manager. Do not claim the original automation passed. Our
Control Panel opens, but its initial list screenshot was blank; the settled
content check was interrupted and remains to be repeated.

The reproducible compositor package at b99b5090 builds successfully, including
the source-commit record, calculated dependencies and libsfdo notice. Wine's
assembled stock files were audited against 055abb3 (12,462 files match), then
normalized to that Git revision while retaining the three owned-module
symlinks and generated configure. This supersedes the historical 3bb585c4
metadata noted below. The normalization restored old archive mtimes; generated
private protocol headers from the production-pin build were newer and caused
`wex_window_manager_v1_listener` incomplete-type / add_listener errors. Touching
the actual current XML regenerates the headers before rebuilding. Never use
these failed build outputs for packaging.

Tooling failures in this checkpoint: a JavaScript patch template interpreted
Bash substitution syntax and failed with “Missing }” before editing; the
replacement patch avoids template interpolation. Debian WSL initially lacked
MinGW compilers; installing both architectures enabled the existing checks.
A nested pgrep pattern containing pipes was parsed as shell pipelines; stage
scripts instead. The first session launcher reused /proc's stale
WINESERVERSOCKET=14 and Wine reported “Bad server socket 14: Bad file
descriptor”; removing that variable, WINELOADERNOEXEC and WINEPRELOADRESERVE
fixes the real launches. An accidental py_compile cache under installer/tests
was removed; use AST parsing for local syntax checks. `file`, WSL `wget`, and
scripts/build-wine.sh were absent; the successful alternatives were window
readback, PowerShell download, and scripts/assemble.sh. The first source audit
falsely flagged four stock CRLF data files; raw-byte hashing before normalized
comparison resolves that.

On resumption the old tool process IDs were gone (`Unknown process id 28611`),
`vmtest.py status` was rejected (no such subcommand), and screendump returned
ConnectionRefusedError on the stale QMP socket. QEMU was stopped; the existing
installed overlay was restarted successfully. No pending GUI check is inferred
to have passed from that interruption. The permitted Debian WSL staging HTTP
server was also restarted; Gemini's distributions remain untouched.
A documentation patch initially used a partial line as context and did not
apply; the replacement uses the full existing line.

## Part A

Qt/browser checkpoint: actual QTerminal at 150% keeps its Wine caption and
accepts `echo Qt150checked`, moves, resizes with its minimum constraints,
grows a corner (client 897x630 to 957x651), maximizes exactly above the taskbar,
and minimizes/restores through its single taskbar item. Firefox ESR at 150%
keeps its own decorations and one hosted button, minimizes/restores, and opens
the local `about:support` page by actual Ctrl+L typing (`Application Basics`
is visible). Evidence: `qt150-controls.log`, `qt150-typed.png`,
`qt150-corner.png`, `firefox150-min.png`, `firefox150-restored.png`,
`firefox150-support.png` and `firefox-qt150-controls.log` in the ignored directory.

QTerminal's Exit confirmation exposed native transient stacking after raising
its parent through the taskbar. Compositor b99b5090 raises native transient
children through their own hosting roots, preserving labwc's existing ordering.
The real close/switch-away/taskbar-return/Yes sequence passes at 100%
(`qt-transient-controls.log`). Taskbar activation also preserves maximized
placement and chooses the last active visible popup before restoring its owner,
instead of activating an already visible parent with SW_SHOWNORMAL first.

Still open: at 150%, the Exit dialog stays above its parent, but its Yes/No
buttons can disappear briefly after switching back. The bounded five-second
OCR check fails (`qt-transient150-controls.log`); later screenshots show the
buttons again, and a click at Yes closes the application. A real-client
WAYLAND_DEBUG trace shows configure acknowledgements, fresh partial-damage
buffers and frame callbacks; do not claim this case passing. Continue other
acceptance rather than expanding into synthetic cases. The 100% fix remains
verified. Current VM: Bob at 150%, foot plus QTerminal's Exit dialog, with
test-only compositor `-d` and QTerminal WAYLAND_DEBUG logging enabled.

Test tooling: vmtest now waits for QMP's scheduled key release before sending
the next chord/text. Its old fixed 50ms delay was shorter than the default
80ms key hold, overlapping modifiers in fast program checks. Firefox's actual
Ctrl+L/about:support navigation passes after the change. QTerminal's first
test launch inherited /root and logged `CRASHED! result: -1`; `-w
/home/bobsmith` runs its shell correctly. A wait for `Shell No. 1` then failed
because its running shell changes the title to `bobsmith@WEX-PC: ~`; geometry
readback and the typed shell command establish that it is running. An
immediate post-close process query preceded QTerminal's exit; the test now
allows two seconds for orderly exit. Read-only discovery also tried nonexistent
`wexcomp/src/foreign-toplevel.c`; the sources are under `src/foreign-toplevel/`.

ISO preparation discovery: live-build is installed on the Proton build VM.
The actual build volume is `/srv/protonexplorer` (25 GiB free), not `/srv`
(on the nearly full root disk). The assembled source root has no .git directory;
git queries there failed. Its Wine checkout still has historical Git HEAD
3bb585c4 with our explicit source overlays and owned-module symlinks. Package
version is fixed rather than Git-derived; nevertheless, normalize and verify
that source against the integration revision before the ISO. Do not reset or
discard the owned-module symlinks. The final production-pin build after the
taskbar activation change passes both architectures.

CSD checkpoint: Wine topic 055abb3 fixes a real gedit 150% startup failure.
It first maps at logical 900x747, then shrinks to 853x513. The proxy has no
nonclient ring, hence no GDI damage to publish its new extent. Compositor debug
reported `frame ... size=900x747 native ... size=853x513 client=0,0 900x747`;
the retry limit subsequently removed the proxy, leaving gedit without a taskbar
button. Reusing Wine's existing transparent dummy-content helper for visible,
noniconic all-client hosts updates that extent; SSD frame painting is unchanged.
At 150% gedit now has exactly one hosted taskbar item, its own header bar,
successful `GTK150checked` input, and taskbar minimize/restore. Evidence:
`gedit150-controls.log`, `gedit150-min.png`, `gedit150-restored.png`.
The full combined WoW64 build passes (`session-csd-dummy-full-wow64.log`),
as does the repeated production-pin build including the owned diagnostics.

The existing window-control readback gained a read-only inventory mode to
diagnose this actual gedit failure. It prints native app IDs, logical geometry,
flags and states; it does not bind, resize or control clients. Compile it with
`-Isrc/include`; the existing `wayland-l1-check.sh` compile line is updated.
Both MinGW architectures compile with `-Wall`. An initial `%I64u` format emitted
a MinGW format warning; the new inventory output uses portable `%llu` instead.
No synthetic application or new protocol test has been added.

For this diagnosis only, the installed launcher selects a small
`/usr/local/bin/wexcomp-debug` wrapper passing labwc's standard `-d` option,
and Wine logs `-all,+wexhost,+explorer`. These are test-VM overrides, not ISO
defaults. The debug wrapper and staging helpers are under the ignored build
directory. Bob currently has gedit open at 150% with unsaved test text.

Scale checkpoint: Wine topic 4fb0940, compositor 0456b718 and the owned
`linux_windows.c` reconciliation now distinguish logical native observations
from Win32 pixel geometry using the driver's existing preferred surface scale.
The driver publishes a scalar read-only scale and posts an owning-thread
notification, following the other private properties; no new hook or protocol.
The shell retains the raw native snapshot for scale reconciliation. This
covers the requested one-output 150% case and unit-scale output layouts;
mixed-scale monitor coordinate transforms have not been tested.

Real foot at 150% now retains its Wine frame and input: an edge shrink changes
the pixel client from 1050 to 930 wide, maximize reaches frame
(-4,-4)-(1284,774), restore returns to client (114,41) 930x721, and
`echo Scale-150-passed` prints. The taskbar is visibly at the bottom, above the
window, with scale120=180 and pixel rect (0,770)-(1280,800). Evidence:
`scale150-controls.log`, `scale150-max.png`, `scale150-restored.png`, and
`scale150-taskbar.png` in ignored `build/wayland-session`.

The successive live-scale failures were: unconverted native inventory,
independent compositor placement of Wine shell/frames, and the offscreen old
taskbar edge never receiving preferred scale. The last is fixed by notifying
the Wine view's assigned output scale before it repositions through its zone.
Normal windows can retain partially offscreen positions after a live scale
change, consistent with labwc's midpoint visibility policy. Switching scale
while foot is already maximized still needs reconciliation; static 150%
maximize/restore passes. Keep that remaining real case explicit.

Latest incremental checkpoint: Wine topic ac2cc1e and compositor 4346271c.
Foot caption maximize/restore now works by pointer after the theme renderer's
bitmap-based slots were aligned with Wine's metric hit slots. XWayland xterm
now completes the same v10 resize transaction using a content commit at the
requested hint-constrained dimensions, without requiring an xdg ack. Real
edge sizing changes its client from 484x316 to 604x316; corner sizing reaches
664x381, maximize fills the 1280x770 work area, and restore returns to 664x381.
No synthetic clients were added.

Alt+Tab from native xterm now uses the associated Wine frame's ordinary
wl_keyboard focus path. Wine restores held Alt/Shift from enter before Tab;
Explorer's existing hook owns switching. The held switcher is visibly above
the terminals, releasing Alt switches to foot, and `echo After-Alt-Tab` prints
there. The Windows key opens Start from native foot. Initially the switcher
existed but was invisible as a subsurface clipped into the host's client hole;
private-shell nonactivating topmost overlays now get independent surfaces.
An earlier xterm typing attempt after the unhandled shortcut contained control
input and failed as a shell command; clean post-switch typing passes afterward.

Evidence in ignored `build/wayland-session`: `foot-caption-passed.png`,
`foot-caption-restored.png`, `xterm-maximized.png`, `xterm-controls.png`,
`native-held-readback.png`, `native-switcher-complete.png`, `native-start-key.png`.
Logs: `session-caption-full-wow64.log`, `session-xwayland-compositor.log`,
`session-shortcuts-full-wow64.log`, `session-shortcuts-compositor.log`,
`session-switcher-overlay-full-wow64.log` in the Proton VM's isolated checks
directory. All listed builds pass; the full production d92d7e44 build was
repeated after these topic changes and passes too.

The first live switch to 150% detached the native groups and exposed labwc
frames while Wine's taskbar stayed at its physical pixel size. This is a real
acceptance failure under investigation; 150% is not claimed passing. Bob is
currently at 150%, with foot and xterm open. Firefox ESR, qt6-wayland and
wlr-randr are installed for the remaining real-program checks.

Linux desktop-file launches now copy the compositor's normal Wayland socket
and XWayland display into their GIO launch context. They remove both `WINE*`
and shell `WEX_*` variables. The endpoints come from the session inventory,
so launching the first native application does not require an existing window.
X11 launches retain their original display environment. Both PE architectures
and the Unix wexhost library compile in the combined WoW64 build.
The complete combined WoW64 build also passes with master's exact production
Wine pin, d92d7e44, including these changes. The launch change does not require
any property from the unmerged L2 Wine topic.

The installed overlay now runs Bob's actual LightDM Wayland session, selected
through the Welcome screen's new session list. X11 is the initial choice.
Gedit keeps its header bar and has one taskbar button; physical typing and
taskbar minimize work. Foot physical edge grow/shrink and corner grow work,
followed by successful typing. This uses Wine L2 topic 0607ece plus the pending
captured-motion fix, compositor eca5e619, and the current owned source. These
are incremental checks, not the complete part A acceptance.

The native-input failure was configuration: suppressing labwc defaults also
removed client click-to-focus. Explicit Client Focus/Raise bindings restore
normal native activation without enabling labwc's shell shortcuts. Foot's
outward sizing failure was Wine clamping captured motion to the old frame;
the pending Wine fix skips that clamp only during explicit compositor capture.
The existing window-control tool's z-order readback now includes geometry and
hosting/focus properties to diagnose this real foot failure; no synthetic
client cases were added.

Production Wine remains d92d7e44. Sizing controls are guarded at that pin;
maximize remains disabled in both builds pending its integration. Remaining:
Start launch acceptance, CSD restore/maximize, hosted maximize, the other real
program classes, output-scale and two-monitor checks.

## Part B

The separate Wayland launcher shares an environment helper with X11, retains
PAM's runtime directory, and starts the private-socket shell without a virtual
desktop. Its Wine driver list permits both backends, so the next X11 login is
not left with a Wayland-only prefix. It lets Wine perform its normal prefix
update rather than forcing a full reinstall on every login.

The new `wexcomp` submodule pins eca5e619. `installer/package-wexcomp.sh` builds
that source in its own Meson directory, stages only the compositor executable,
records its source revision and license notices, and calculates Debian shared
library dependencies. Its initial combined source build and package generation
passed on the Proton build VM. ISO integration and a fresh install test remain
pending; the overlay uses staged packages/files for development.

Screenshots currently in the ignored `build/wayland-session` directory:
`greeter-choice.png`, `bob-wayland.png`, `input.png` (gedit header bar/taskbar),
`foot-resized.png`. Review point 1 requires final evidence from the new ISO,
not just these development captures.

## Hosted maximize/restore checkpoint

Wine topic 6b3435c and compositor 73ea3a44 extend the existing v10 path without
a new protocol version or generic Wine hook. The bridge defers the normal
Win32 maximize/restore command until native-ready, requests physical frame
focus/capture, and publishes the matching frame. Two scalar driver requests
cover capture outside GUI_INMOVESIZE and the captured maximize state preceding
resize on the same Wayland connection. The compositor mirrors that state while
leaving geometry to Wine, and respects explicit size limits while suspending
floating character-cell increments for maximized clients.

Real installed-VM foot now completes maximize at client `(0,26) 1280x744`:
the client bottom is exactly the taskbar/work-area boundary y=770. Win32 frame
`(-4,-4)-(1284,774)` includes the usual invisible maximized resize borders.
Taskbar minimize/restore preserves the maximized window. The taskbar's Restore
menu command returns exactly to `(288,121)-(992,649)`, client `(292,151) 696x494`.
Typing `echo restored-foot` succeeds afterward. In each finished operation the
group remains active, native focus is restored, and capture is cleared. The
frame screenshot is `build/wayland-session/foot-max-current.png` (development
overlay; final ISO evidence still required). Caption double-click and taskbar
menu paths are verified; a direct caption-button click still needs checking.

The successive real-program failures explained the implementation:

- Applying Win32's maximized style immediately changed the client hole before
  native-ready and detached the group. Deferring the system command fixes this.
- SetCapture outside GUI_INMOVESIZE never requested compositor capture. The
  explicit private request still requires the compositor's physical frame
  focus/capture checks; normal capture keeps its existing policy.
- A state-only xdg configure carried the old frame dimensions and used
  NOSENDCHANGING, bypassing the bridge and shrinking the already painted frame
  to 712x536. Hosted controlled views now acknowledge that state without
  applying the obsolete geometry, flushing their pending contents afterward.
- Foot initially selected floating cell dimensions 1278x741, leaving small
  work-area gaps. Sending maximized state before resize on the same connection
  gives it the correct pixel dimensions on the first completed transaction.

Start-menu launching is also checked with actual QTerminal. Its process has
`HOME=/home/bobsmith`, `WAYLAND_DISPLAY=wayland-0`, `DISPLAY=:0`, and no `WINE*`
or `WEX_*` variables. X11 remains the default Welcome-screen selection.

These results are at 100%. The remaining program classes, 150%, two outputs,
caption buttons, fresh ISO install, and review point 1 remain outstanding.
Master's production Wine pin is still d92d7e44, so these sizing/maximize controls
remain compiled out there until the integration pin is advanced.

## Test environment and command failures

Further read-only path failures in this continuation: there is no
`wexcomp/src/xwayland/` directory (use `src/xwayland.c`) or
`winewayland.drv/wayland_zone.c` (use `wayland_zones.c`). A literal
`docs/WAYLAND_L4*` rg argument also failed with Windows error 123; file
inventory found the plan in `WAYLAND_PLAN.md`. A build-VM cd initially omitted
`wow64-source` before `wexcomp`. The shallow clone's fetch updates FETCH_HEAD,
not origin/codex/wayland-session; use FETCH_HEAD there. Its existing view.c
diff against 73ea3a44 was indentation only, so the first exact-diff check
correctly prevented the reset; verified with `git diff -w` and advanced that
isolated checkout with a mixed reset, preserving working files. CRLF staging
still needs normalization before making the compositor package reproducible.
The xdg decoration source is `wexcomp/src/decorations/xdg-deco.c`; read-only
searches for `src/xdg-decoration.c` and `src/decorations.c` failed.
A STOCK_CHANGES documentation patch initially expected a whole sentence on
one line, but the source wrapped it across lines. The patch failed without
changing that file; re-read its exact context and applied the corrected patch.
An accidental patch briefly removed resize_transaction's serial field; it
was restored immediately, before any build or transfer.

After prefix-split S4 merged on master, the earlier incremental owned-source
archive was insufficient to establish that the complete merged tree built.
Re-synced every tracked `src`, `scripts` and `installer` file from master
2a2634fe to the isolated assembled build source, then ran the full combined
WoW64 build at production Wine d92d7e44 again. It exits zero (`Wine build
complete.`). The topic files are restored afterward; the root Wine pin is
unchanged. Both launchers now share S4's prefix preparation in the sourced
helper; Bash syntax checks pass. Fresh split-prefix Wayland login is still
part of the pending new-ISO acceptance.

The real foot caption maximize click at (944,136) reproducibly minimizes.
Readback returns HTMINBUTTON at that drawn maximize glyph; the normal
WM_SYSCOMMAND path receives SC_MINIMIZE. This is a mismatch between themed
caption drawing and Wine's metric-based hit rectangles, rather than a resize
transport failure. Caption double-click and taskbar Restore remain successful.

This continuation's read-only path mistakes: literal PowerShell globs
`wexcomp/include/wex*` and `wexcomp/src/protocols/wex*` fail with error 123;
the protocol header is `include/protocols/wex-window.h`, not
`include/wex-window.h` or `include/wex-scene.h`. `linux_windows.c` belongs to
`src/dlls/wexshell`, not wexhost. Wine's nonclient code is `win32u/defwnd.c`,
not `win32u/nonclient.c`; there is no `src/dlls/wextheme`. File inventory and
`rg -g` resolve these. The generated helper extraction left whitespace on
blank lines; `git diff --check` detected it and the patch removed it.

The permitted Debian WSL distribution runs our QEMU/KVM tests. The stopped
`p4-submodule` installed disk is the read-only backing file for a new
`/var/tmp/wineexplorer-vmtest/wayland-session/disk.qcow2` overlay. Gemini's
`wexbuild` and `wexcomp` distributions are untouched. The Proton build VM
continues to supply the combined WoW64 build and compositor build.

Read-only discovery failures: `rg ... src/programs/explorer` and reading
`src/dlls/wexhost/process.c` failed because the shell is in `src/dlls/wexshell`
and desktop-file launching is in `apps.c`. Reading
`installer/scripts/package-wine.sh` likewise failed; it is
`installer/package-wine.sh`. PowerShell's literal `src/wex*` argument to rg
failed with Windows error 123; use `rg -g` instead.

The Proton build VM has neither `/dev/kvm` nor QEMU; Debian WSL has both.
`command -v live-build` and `command -v lb` found no ISO builder in Debian WSL.
Serial login on the older installed overlay returned no command output;
the image predates the current installer test marker and needs its serial
getty checked. The test password matches the prior P4 runner's saved file.
`vmtest.py shutdown wayland-session --timeout 50` reported
`guest did not shut down cleanly; disk inspection must not proceed` while
systemd was still waiting for the greeter's session-c1 scope. Do not inspect
or mount its disk until QEMU has actually exited.
QEMU subsequently exited normally; a screenshot attempted after its exit failed
with `FileNotFoundError` for `qmp.sock`, confirming that the socket was gone.

The first NBD mount ran before `/dev/nbd7p1` appeared and failed with
`Can't lookup blockdev`; dependent discovery commands also failed because it
was unmounted. The NBD device was disconnected, then reconnected with
`udevadm settle` and `blockdev --rereadpt`. The overlay's serial getty was
enabled offline. Its disk and ext4 partition were expanded to 40 GiB while
stopped (`sfdisk`, `e2fsck -pf`, `resize2fs`); the backing disk is unchanged.
The subsequent serial session works. A redundant poweroff command raced guest
shutdown and returned `BrokenPipeError`; no disk was mounted while running.

The first package command failed because its explicit TMPDIR did not exist;
creating `/srv/protonexplorer/tmp` fixed it. A staging tar named `COPYING`, but
the compositor's license is `LICENSE`; the production package uses `LICENSE`
and retains libsfdo's notice when linked from the Meson fallback.
`dpkg-shlibdeps` initially warned that the executable was outside its staged
package; it now analyzes the staged executable. A first HTTP server background
launch did not persist; the active foreground server serves the ignored staging
directory on Debian WSL loopback port 18765 (guest address 10.0.2.2).

The first foot launch inherited `/root` as its working directory and failed;
setting HOME and `--working-directory=/home/bobsmith` fixed it. Gedit warns about
missing optional Peas typelibs under `--no-install-recommends`; its editor still
runs. The test VM has no sound device, producing ALSA warnings. The compositor
reports missing accelerated EGL for its standard VGA; its fallback draws the
desktop. Missing xdg_toplevel_icon_manager_v1 is logged by Wine.

One foot typing wait failed before the Client focus bindings were restored;
the same real typing check passes afterward. A config reload recentered the
taskbar at y=370; a fresh session puts it back at y=770. Output-change behavior
still needs examination in the required scale/multi-output checks.

`make dlls/winewayland.drv/winewayland.drv.so` failed because the Unix target is
`winewayland.so`; rebuilding the correct target passed. The following scp of
the nonexistent output also failed and was repeated after the successful build.
`bash -n scripts/vmtest.py` was an interpreter mistake and reported Python syntax
as a shell error; `python3 -m py_compile` passes. Other read-only path mistakes:
`src/programs/wexgreeter`, `src/programs/welcomelogon`,
`src/dlls/wexshell/taskbar.c`, `src/dlls/wexshell/traywnd.c`, and
`wexcomp/src/session.c` do not exist. The actual files are `programs/logonui`,
`wexshell/systray.c`, and `wexcomp/src/config/session.c`. A repeated literal
`winewayland.drv/*.c` rg argument produced Windows error 123; `-g '*.c'` works.

State: Debian WSL and our `wayland-session` QEMU overlay are running. Bob is in
Wayland, with foot open. NBD7 is disconnected. The loopback HTTP staging server
is running. The Proton build VM's assembled source contains current owned edits
and the Wine L2 topic plus captured-motion fix; its compositor packaging uses a
separate clean source checkout. No source or guest session in Gemini's two WSL
distributions was used.

Further tooling notes: inline quoted `/proc` environment scans left the serial
shell without its expected prompt, so subsequent readbacks returned no output.
Sending Ctrl-C restored it; staging `native-environment.sh` over the test HTTP
server avoided the nested PowerShell/WSL quoting. An initial `restart-max.py`
wait failed to recognize the white Welcome-screen text; using vmtest's white
OCR fallback fixes that. The package still emits dpkg-shlibdeps' staging-layout
warning, but builds and includes the calculated dependencies and libsfdo notice.
Read-only discovery also used nonexistent `wexcomp/include/output.h` (the output
structure is in `include/labwc.h`). The official 7-Zip download page was read
to select a 32-bit installer; the web tool's first click used an invalid page
reference and the corrected GitHub download was restricted by that fetcher.
No installer binary has been added to the repository.
