<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Installed Wayland session

In progress, 2026-10-16. This follows `WAYLAND_SESSION_WORKORDER.md`.
Parts A and B lead to review point 1; tray integration and final regressions
follow that review. No acceptance is claimed yet.

## Part A

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
