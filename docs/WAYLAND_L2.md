<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# L2 implementation and acceptance

Status: in progress; no acceptance claimed.

Scope and agreed decisions: WAYLAND_L2_WORKORDER.md. Project source is on master;
Wine and compositor work use codex/wayland-l2, initially f0108dc and 9a47d74f.
Host compositor worktree: build/wayland-l2/wexcomp. VM compositor checkout remains
~/wexcomp-build, used only for isolated nested tests. No default/session deployment.
Current L2 topics: Wine 786643b (host worktree build/wayland-l2/wine), compositor
efb40ac9 (build/wayland-l2/wexcomp). The ordinary wine/ submodule checkout remains
detached at its production f0108dc gitlink; neither production pin was advanced.

## Planning inspection corrections

- rg rejected docs/DWM* positional glob and src/programs/wexhost, which is absent.
  Use the docs directory with -g; host services are src/dlls/wexhost.
- VM rg is unavailable; use host Git snapshots or grep on the VM. Initial reads
  assumed src/wex and src/foreign.c; actual compositor code is src/protocols and
  src/foreign-toplevel, decorations are src/decorations.
- The raw GitLab wlr protocol URL was unavailable through the web tool. Existing
  installed wlroots headers, labwc source and protocol documentation establish
  the available interface and public resource lists.

## Backend-dispatch checkpoint

The existing X11 manager now implements a backend table. New StartEx and
capability APIs preserve the legacy call indexes; BindHost moves the X11 parent
lookup out of Explorer's binding path. AUTO selects X11 for an X11 desktop and
reserves the no-X-desktop path for Wayland. Wayland still returns not supported
at this checkpoint; native Wayland integration is not yet implemented.

The WoW64 build exposed the missing wexhost WoW64 Unix-call export. Both tables
now use the same fixed-width bridge records, including ULONG64 pointer slots.
The 32-bit and 64-bit backend probes both passed with zero failures, including
invalid selection, capabilities, duplicate start, invalid host and repeated
start/stop. The X11 800x600 smoke passed: two tray icons, zero overlaps or
out-of-bounds windows, plus all eight native dialog placement cases.

Build: Proton VM, /srv/protonexplorer/wex-wayland-l1/wow64-source/build/wine64,
configured --enable-archs=i386,x86_64. Module log: checks/l2-dispatch-build.log.
Probe logs: checks/l2-backend{32,64}.log. Prefix: prefixes/smoke-l2. Reproduce
with scripts/check-wm-backend.sh, setting WEX_CHECK_PREFIX to an isolated prefix.
No installed runtime or real desktop changes.

Corrections during this checkpoint:

- The first multi-file patch was rejected for a mismatched wm_thread context;
  no files were changed by that attempt. Reapplied with the actual context.
- wm.c initially included winbase.h without its prerequisite NT types, causing
  RTL_CRITICAL_SECTION errors. Use winerror.h and the shared header's NT types.
- The backend table was exposed to PE compilation, where unixlib_entry_t is
  absent. Guard the table with WINE_UNIX_LIB. Both module architectures now build.
- Linking the probe initially used stale import libraries: the module 'all'
  target does not regenerate them. Explicitly build both libwexhost.a targets.
- Explorer launches detached applications, so redirected launcher stdout did
  not contain the probe report. Give the probe an explicit output file and poll
  for completion; do not use Explorer's exit as application completion.
- The first 32-bit probe returned ERROR_NOT_SUPPORTED because wexhost lacked
  __wine_unix_call_wow64_funcs. Added the fixed-width ABI table and reran both
  architectures successfully. Failed logs were superseded by successful reruns.

## Compositor observation checkpoint

Private manager version 3 adds get_foreign against the same connection's
standard wlr foreign-toplevel inventory. The supplemental object reports a
view-lifetime token, logical client geometry, decoration policy and labwc size
hints. It does not adopt windows or change focus/decorations. Native view tokens
remain identical across privileged observers; Wine-controlled views are
ineligible. Standard metadata and ordinary controls remain on the wlr protocol.
Live geometry/decoration updates use view_moved/view_set_ssd_mode. Native
destruction closes the object and leaves refresh inert; disconnecting one
observer leaves other observers and the native view intact.

Meson build passed on the Proton VM. tests/wex/run.sh passed 27/27, including
all 26 previous checks and the new foreign-client scenario: SSD/CSD, existing
and late inventory, shared/distinct tokens, geometry/minimum sizes, standard
title/minimize/restore, independent observer disconnection, Wine-frame exclusion
and stale-object lifetime. Log: ~/wexcomp-build/l2-acceptance.log. Native/frame
binding and shell Wayland adoption remain unimplemented; L2 acceptance is open.
Verified source is compositor commit bc3ee676 on codex/wayland-l2, pushed to
zcomputerwiz/labwc. Production wexcomp remains at 9a47d74f. The guest branch has
the tested patch applied over that base; no production branch was advanced.

Corrections:

- A read assumed tests/wex/run-acceptance.sh; the actual harness is run.sh.
- Supplemental callback was initially named foreign_toplevel_destroy, colliding
  with the existing public API. Renamed before compilation.
- Initial compositor linking failed because wlroots does not export the standard
  foreign-toplevel protocol interface symbols. Vendor the unmodified upstream
  wlr XML with its original permission notice and generate private interface
  definitions in Meson and test clients. Build and full suite then passed.
- A guest fast-forward preflight failed because the transport tar assigned
  mode 0644 to four existing executable files. No fast-forward occurred. Preserve
  Git's tracked modes in subsequent tar transfers and restore these modes;
  the source contents already match bc3ee676. Unrelated guest logs remain intact.

## Additional bridge and metadata checks

The maintained backend probe now launches a real native X11 client after the
bridge is ready. Both 32-bit and 64-bit probes passed map-event delivery,
UTF-16 title retrieval, binding the native window to a Wine HWND, and releasing
it again, with zero failures. This exercises the added WoW64 table beyond error
returns. Changes are independent of the pending Wayland frame bridge.

Compositor 202b9089 adds coverage for live logical placement at 317,229 and
SSD/CSD changes. A decoration request must notify observers even when effective
SSD geometry remains unchanged or the window is shaded/fullscreen. The full
suite passed 27/27 again; log: ~/wexcomp-build/l2-live-acceptance.log.

The next protocol increment publishes this compositor's normal Wayland socket
and its own XWayland endpoint to privileged clients. This avoids guessing
wayland-0 or inheriting the outer session's DISPLAY for native launches.
Initial build failed because HAVE_XWAYLAND was tested before labwc.h supplied
the configuration definitions, skipping the required XWayland type header.
Move that conditional include after labwc.h, then rerun the suite.
The rerun passed 27/27; log: ~/wexcomp-build/l2-environment-acceptance.log.
A separate -Dxwayland=disabled build also passed, followed by the complete
foreign-client scenario against that compositor, explicitly asserting an empty
X11 endpoint. Build log: ~/wexcomp-build/l2-no-xwayland-build.log. Runtime logs:
/srv/protonexplorer/wex-wayland-l2/no-x-session.KDd0v9. The test compositor exited.

An inspection used scripts/build-assembled.sh, which is absent; the maintained
script is scripts/refactor/build-assembled.sh. A PowerShell positional wildcard
for session scripts was rejected; use rg on scripts with -g instead.

## Resume point

Completed foundations: backend-neutral PE dispatch/binding, working 32/64-bit
Unix-call tables, privileged supplemental native observation and trusted native
launch endpoint discovery. All are committed and pushed. Main runtime Wine pin
and production compositor branch remain unchanged; only the user's .gitignore
edit is pending in the main checkout. Wine's codex/wayland-l2 has no edits yet.

Next implementation is the wexhost Wayland client and native/frame association:
consume standard inventory plus supplemental snapshots, publish the native
lifetime token through a generic HWND property, map owned proxy dialogs as
independent toplevels, and have the compositor bind/group the frame and native
view. Keep AUTO Wayland unsupported until binding/rendering/input can safely
adopt a window. Distinguish observed state from requests and logical foreground
from physical native keyboard focus. Then wire native launch context and the
test session's private D-Bus activation environment to the discovered endpoints.
This is a foundation checkpoint, not complete L2 or deployment acceptance.

## Wine-side native inventory and control

wexhost now has an explicit Wayland backend. It connects using the caller's
WAYLAND_DISPLAY, requires private manager v3 and standard wlr inventory v3,
and reports native map/withdraw/title/icon metadata plus WEX_WM_OBSERVED full
state snapshots. Observations are separate from Win32 control requests. The
new GetNativeInfo ABI returns the native token, UTF-8 app ID and this compositor's
normal native endpoints; its argument records work in both PE architectures.
Identifiers increase across backend restart and are never reused. Closed
resources are collected once both protocols have closed them. Repeated pending
metadata/state snapshots are coalesced rather than accumulating resize events.

The reader follows Wayland prepare/read/cancel rules, holds no bridge lock while
polling, and releases all reader/queue resources on stop. Standard minimize,
restore, activate and graceful close are wired. Frame binding, geometry control,
topmost and custom native icons remain unsupported. AUTO Wayland remains
disabled, so Explorer does not yet adopt native Wayland windows. Coordinates
are compositor logical client coordinates until frame/DPI conversion is added.

Verified in the combined --enable-archs=i386,x86_64 build, with real foot clients:
inventory, positive client geometry, native identity/endpoints, UTF-16 title,
minimize/restore and activation acknowledgements, graceful close, withdrawal,
stale identifiers, stop/restart and compositor transport loss. Both PE probes
pass with zero failures. A private test D-Bus session prevents activation from
escaping to the real desktop. Reproduce with scripts/check-wayland-backend.sh
and an isolated WEX_CHECK_PREFIX. Logs: checks/l2-waylandwm-validation.log and
checks/wayland-backend.9uY8YV under /srv/protonexplorer/wex-wayland-l1.
Both real-X11 binding probes and the 800x600 tray/eight-dialog smoke pass again.

The module uses Wine's normal scanner-generated protocol code and Wayland
client library flags. The three XML sources retain their original notices
(private wex MIT, unmodified wlr permission notice and Wine's xdg-shell copy).
libwayland-client0 is already an explicit runtime package dependency in
installer/package-wine.sh. This checkpoint requires the project's existing
Wayland-enabled build configuration/development dependencies; compilation with
Wayland disabled is not yet supported or claimed.

Corrections in this checkpoint:

- The first runner stopped after a successful 64-bit probe because closing foot
  sends SIGHUP to its child and foot returns that status. Assert graceful-close
  withdrawal in the probe, then reap the terminal without treating child SIGHUP
  as a bridge failure. Both architectures pass afterward.
- Some SSH checks stalled when IPQoS=none was omitted. Restoring the documented
  option returned promptly; uptime/load confirmed a running, idle guest. One
  test run timed out before reaching its compositor, during prefix setup. Its
  rerun passed; no VM restart or real desktop changes were made.
- An X11 probe overlapped another test using the same prefix and was interrupted.
  Add a shared check.lock to both maintained backend runners, close the lock FD
  in child sessions, and rerun Wayland, X11 ABI and smoke sequentially. All pass.

Next remains native/frame binding in the compositor and Wine driver, followed
by Explorer adoption, physical/logical focus separation and native launch wiring.
The Wine submodule and compositor production branch have not been advanced.

Transport recovery follow-up: after a backend restart, the probe maps a second
native client and checks that its identifier differs from the first. The harness
then terminates only its own headless compositor. The reader emits withdrawals
for every live announced native entry before STOP, allowing the future shell
bridge to remove its proxies. Both architectures verify withdrawal delivery,
ERROR_NOT_READY after transport loss and clean stop with zero failures. Logs:
checks/l2-waylandwm-recovery.log and checks/wayland-backend.jK7iA5. No test
compositors or test Wine servers remain running. Main source checkpoint is
e5adf5bb plus this recovery follow-up; production runtime pins remain unchanged.

## Native/frame reservation and Wine forwarding

Compositor v4 adds controller-specific lifetime handles and associate_foreign,
while retaining the existing v1/v2/v3 contracts. A native view token identifies
the view; a controller handle identifies the live bridge observer. A frame can
reserve exactly one eligible native view and one live controller across separate
Wayland connections. Stale, mismatched and competing handles are rejected;
repeating a pair is idempotent. Explicit release, native/controller/frame
destruction, controller disconnect and switching the frame to a shell/desktop
role clear the reservation. PREPARED changes no native scene, focus, geometry or
decorations, and is not an acknowledgement of hosted content. Atomic rendering
and input preparation remain required before adoption.

Wine 925b10c forwards native/controller global atom properties through this
protocol, reports asynchronous status as another scalar HWND property, and
serializes association requests to suppress outdated responses when properties
change. The bridge owns atom references; no process-local pointers cross the
boundary. An owned proxy with a native token maps as an independent Wayland
toplevel under private v4 while retaining its Win32 owner. Ordinary owned
windows keep their existing subsurface path. Native endpoint events are consumed
without changing Wine's privileged graphics environment. These are fork-only
changes recorded in STOCK_CHANGES.md.

Verification:

- Compositor full protocol suite passes 28/28 (including v3 observation and new
  reservation lifecycle checks); log ~/wexcomp-build/l2-association-acceptance.log.
- Compositor without XWayland builds and passes both foreign observation and
  reservation scenarios; log ~/wexcomp-build/l2-association-no-x-test.log,
  runtime /srv/protonexplorer/wex-wayland-l2/no-x-session.PlmsRk.
- Combined WoW64 driver build passes. Real Wayland Wine probes pass 32/64-bit
  owned independent proxy, preserved Win32 owner, invalid-controller rejection,
  valid-pair restoration and controller expiration. No hosted rendering claim.
- Mixed 32/64-bit L1 cross-process stacking passes all screenshot/order/foreground
  assertions with v4. Driver logs: checks/l2-association-driver-build.log and
  checks/l2-driver-association.log under /srv/protonexplorer/wex-wayland-l1.

Reproduce the Wine checks using scripts/check-wayland-association.sh with an
isolated WEX_CHECK_PREFIX, the patched combined build and compositor L2 checkout.
The runner compiles both PE probes, uses a private D-Bus session and its own
headless compositor, then cleans up. Its SDK include path uses -idirafter so
MinGW's standard Windows headers are not mixed with Wine's incomplete generated
SDK headers. Test scripts take check.lock to serialize prefix use.

Corrections:

- The initial probe compile used -I for Wine's include directory and mixed Wine
  and MinGW COM headers, failing on WINBOOL/IActivationFilter. Use -idirafter;
  both compilers and the actual driver probes then pass.
- The initial stacking screenshot showed blue instead of green because a fixed
  one-second delay did not ensure all processes had visible, handle-published
  windows. Add a read-only ready command and poll it before taking screenshots.
  The original color/order/foreground assertions remain and pass.
- All stacking assertions passed but the test's ten-second Wine shutdown limit
  expired. Increase that limit to 45 seconds and always terminate the owned
  compositor in a nested finally block, even if server shutdown fails. The one
  leftover test compositor was identified by its recorded PID/config path and
  stopped. The complete rerun passes and cleans up.

Next: add the compositor's atomic native/frame scene group and client-area
render/input exclusion, then physical/native focus acknowledgements and frame
menu focus handling. Only after those checks can wexhost BindHost claim hosting
and Explorer AUTO adopt windows. Native launch/D-Bus activation routing follows.

## Active scene grouping checkpoint

The compositor L2 topic c16220d9 now offers private protocol v5 commit_foreign and a
separate ACTIVE/DETACHED/NOT_READY group acknowledgement. PREPARED remains a
reservation only. Activation of a group requires mapped buffers and matching
native client dimensions. Invalid rectangles preserve the existing reservation
and group. Wine still binds v4, and wexhost still binds v3; this checkpoint is
exercised by synthetic clients, not enabled Explorer adoption.

The native view's complete scene tree is reparented under the frame's outer view
tree. Four ordinary wlroots scene-surface nodes draw only the frame ring, with
source/destination crops and translated input coordinates. The original frame
root buffer is disabled while its subsurfaces remain above the native content.
Native buffers, transparency and input regions stay under wlroots control;
there is no capture or pixel-copy path. Movement and minimize/restore operate
on the group, and raising native content raises the frame root. Private
activation directs physical keyboard focus to the native view. Full Wine
logical activation and menu focus acknowledgements are still required.

Controller loss restores native decorations and removes the obsolete proxy.
Surface and scene-buffer destruction listeners handle abrupt frame/native
connection loss. A lost root buffer detaches rather than leaving a half-hosted
view. CSD/borderless clients require a full-root cutout, but their complete
acceptance tests, viewport/transform cases and 150% scaling remain pending.
This stage supports a fixed client rectangle; resize negotiation, decoration
policy changes, workspace/fullscreen transitions and focus handshake must be
completed before any bridge hosting capability or AUTO adoption is enabled.

Verification on the Proton VM:

- Full compositor suite passes 37/37 with XWayland and 37/37 without XWayland.
  It checks frame-ring and native input routing, input holes, alpha blending,
  grouped movement, minimize/restore, Wine menu layering, controller recovery
  and abrupt frame/native connection loss, alongside the existing L1 checks.
- Logs: ~/wexcomp-build/l2-scene-acceptance.log and
  ~/wexcomp-build/l2-scene-no-xwayland.log. Screenshots are under
  ~/wexcomp-build/tests/wex/output and
  /srv/protonexplorer/wex-wayland-l2/no-x-scene-output.
- tests/wex/run.sh accepts WEX_CHECK_COMP_BUILD to exercise the same suite
  against the no-XWayland build. Set WEX_EXPECT_NO_XWAYLAND=1 for that variant.
- Existing Wine v4 probes pass 32/64 association and all mixed-process L1
  stacking/foreground assertions against v5. Log:
  /srv/protonexplorer/wex-wayland-l1/checks/l2-scene-driver-regression-retry.log;
  state checks/association.aey1fhz7. This is compatibility verification, not a
  Wine v5 hosting claim.

Corrections and reference work:

- The initial frame pixel at (105,105) overlapped the cursor. The screenshot
  showed its antialiasing, not a frame-rendering error; sample (200,110) instead.
- The first minimize screenshot was taken before the request was flushed on
  the controller's separate connection. Roundtrip that connection and assert
  its observed minimized state before capture. An initial assertion used a
  nonexistent minimized field; correct it to the existing standard state bit.
- Official GitLab and sources.debian.org source access failed. A guessed Debian
  .tar.gz URL returned 404; the official pool lists .tar.bz2. Read matching
  wlroots 0.18.2 source from that archive in an ignored guest reference directory.
  Implementation uses public scene APIs rather than private clip fields; no
  third-party source was copied into the new implementation.
- The first mixed-process Wine regression timed out in its ten-second readiness
  subprocess after both association probes had passed. Cleanup terminated the
  owned Wine server and compositor; no clients remained. A complete sequential
  rerun passed all original assertions without changes or enlarged limits.
  Preserve l2-scene-driver-regression.log and state association.xl9qcjlt alongside
  the passing log; startup timing remains a harness robustness item.

Next is the physical/native versus Wine logical focus handshake, then dynamic
rectangle/resize and policy recovery. Wire v5 into Wine and wexhost BindHost only
with explicit group acknowledgements and a Win32 client-area input exclusion.
Existing desktop sessions and production runtime pins remain unchanged.

## Physical focus acknowledgement checkpoint

Compositor 7c7143d4, private protocol v6, adds set_foreign_focus(epoch, target) and an observed
foreign_focus(epoch, target) event. It retains all v1-v5 requests. The compositor
echoes the client's most recent activation epoch rather than inventing a Win32
foreground decision. Queries and rejected requests return actual physical focus;
seat focus changes also report it. Native activation raises the group. Temporary
frame focus and native restoration apply only if keyboard focus already belongs
to the group, preventing a late menu completion from stealing focus from another
application. Clients must additionally retain the Wine foreground/epoch guards.

Each active scene listens to wlroots' existing keyboard focus-change signal.
Surface ancestry resolves subsurfaces and xdg popup parents to the associated
native or frame root. Unrelated/layer surfaces and null focus report none. The
listener is removed before scene destruction; detached groups always report none.
Requests do not manufacture a wl_keyboard enter or redirect key events: the
ordinary compositor seat focus path determines their recipient.

The synthetic scene probe now binds v6 and verifies native/frame/native focus
roundtrips with actual virtual-keyboard press/release delivery to only the
selected connection. It also checks epoch echoes, unsolicited external-focus
loss, rejected late frame-focus/restoration requests, explicit reactivation,
minimize/restore focus state and queries after controller loss. Existing scene,
input, rendering and abrupt connection-loss checks remain in the same suite.

Wine remains a v4 client and wexhost remains v3. No Wine foreground-retention or
menu-loop implementation is claimed by these compositor tests. Driver adoption
must consume observed group/focus acknowledgements, reject old epochs and retain
logical proxy activation while physical focus belongs to its associated native.
Only a current foreground proxy may request menu-completion restoration. Resize,
decoration-policy recovery and full application/scaling coverage remain required
before enabling hosting/AUTO.

The Wine regression runner also addresses the previous checkpoint's startup
timeout: a timed-out read-only readiness query retries within a fixed 30-second
window deadline, logs the retry and continues checking that the created process
is alive. Mutations still have their original limits and are never retried. The
outer stacking budget is 180 seconds to cover three possible cold starts plus
the unchanged screenshot/foreground assertions and cleanup.

Verification: full compositor suite passes 37/37 with XWayland and 37/37 without
XWayland, including the added key-delivery and focus-state assertions. Final logs
are ~/wexcomp-build/l2-focus-final.log and l2-focus-no-xwayland-final.log. Wine's
32/64 association and mixed-process L1 stacking tests pass unchanged assertions
against v6, with the bounded readiness retry enabled. Log:
/srv/protonexplorer/wex-wayland-l1/checks/l2-focus-driver-regression.log; runtime
checks/association.jbmccbjp. No new test failures; the readiness retry itself was
not triggered on this successful run. Owned test compositors/Wine server were
cleaned up, and production sessions/pins remain unchanged.

## Opt-in Wine hosting and safe reconfiguration

Private protocol v7 adds numbered configure_foreign and uncommit_foreign requests
with configured_foreign completion replies. Unsolicited detached notifications
therefore cannot be mistaken for completion of a pending configure. Uncommit
restores native decorations and hides the old proxy, preserving the reservation
for a later matching commit. The compositor also detaches on a native commit
whose client size or decoration policy no longer matches the group. The fixed
rectangle path now fails safely during changes; full interactive native resize
negotiation, fullscreen and workspace handling are still pending.
Compositor checkpoint is fe3abb94; Wine checkpoint is 786643b, both pushed on
their codex/wayland-l2 branches. Production gitlinks/branches are unchanged.

Wine binds up to v7, preserving its existing fallback for older compositors.
WEX_WAYLAND_HOST_PROP explicitly enables hosting only after a prepared
association. Reservation alone never hosts. The driver derives the client cutout
from the same captured window rectangles and surface scale as its frame, and
serializes numbered group requests. A rectangle change first uncommits the old
group. A mismatch reports NOT_READY, leaving the native application independently
usable; matching geometry can reattach. Removing the opt-in detaches without
discarding the reservation. No reconfiguration retry loop runs after failure:
the bridge can notify a retry using SetWindowPos with SWP_FRAMECHANGED.

The caller must exclude native client content from Win32 hit testing before
enabling the property. The synthetic PE probe supplies a ring window region and
checks WindowFromPoint cannot select the proxy through its client hole. This
contract is not yet implemented by wexhost BindHost or Explorer. AUTO adoption
remains disabled, and no native hosting capability is advertised by wexhost.

Group and physical-focus status are published as scalar HWND properties. Wine
consumes focus acknowledgements only for the current activation epoch; UI-thread
messages preserve the logical foreground proxy while physical keyboard focus
belongs to its native client or temporary frame menu. Native focus can activate
its logical proxy, subject to WS_EX_NOACTIVATE. One-shot frame/native restoration
commands are consumed only for the current foreground proxy. Existing L1
keyboard-leave messages cannot clear a proxy whose group retains native/frame
focus. Group loss and external focus deactivate normally. Driver surface
destruction removes its published reservation/group/focus status.

Maintained reproduction uses scripts/check-wayland-association.sh. Set
WEX_ASSOCIATION_HOSTING=1 for the new SSD hosting probes; omit it for reservation
only. Both build 32/64 PE binaries and use an isolated prefix, private D-Bus and
headless compositor. Mixed-process stacking remains enabled by default; set
WEX_ASSOCIATION_STACK=0 for focused reruns after its assertions already passed.
The native publisher's --publish-hosting mode requests server decorations;
the original --publish-association mode retains its previous behavior.

Corrections during this checkpoint:

- Changing only xdg window geometry while keeping the old buffer extent is
  ignored by labwc's existing Qt compatibility rule. The resize probe now
  attaches an actual resized buffer and restores the original afterward.
- The native resize assertion initially still saw the preceding group state:
  scene_pump drained the bridge before sending native requests. Drain the
  bridge again after the native roundtrip so cross-connection events are
  observed before assertions. The resize and CSD-policy recovery checks pass.
- A temporary compositor diagnostic was lost when the runner cleaned its
  runtime directory. Remove the diagnostic and retain compositor.log in the
  screenshot/output directory during cleanup for future failures. Initial
  failing logs are l2-hosting-acceptance.log, l2-hosting-acceptance-retry.log and
  l2-hosting-debug.log under ~/wexcomp-build; final passing log is separate.

Next: implement the bridge's opt-in binding and client input exclusion, wire
actual frame menu/move/resize loops and native resize negotiation, then complete
CSD, popup, scaling, real application, fullscreen/workspace and launch routing
acceptance. Synthetic focus commands do not constitute real shell-menu coverage.

Verified results on the Proton VM:

- Compositor suite: 37/37 with XWayland and 37/37 without. Logs:
  ~/wexcomp-build/l2-hosting-final.log and l2-hosting-no-xwayland.log; output
  directories tests/wex/output and
  /srv/protonexplorer/wex-wayland-l2/no-x-hosting-output retain compositor.log.
- Combined WoW64 winewayland driver target builds successfully; log
  /srv/protonexplorer/wex-wayland-l1/checks/l2-hosting-driver-build.log.
- 32/64 PE hosting probes pass client hit exclusion, actual group activation,
  native logical foreground retention, temporary frame focus, native restore,
  rejected late restore, safe size mismatch, reattachment, opt-out/re-enable and
  controller loss. Final run after status-cleanup changes:
  checks/l2-hosting-cleanup-final.log, state association.omwqyjvw. Previous full
  hosting plus mixed-process stacking run also passes unchanged stacking and
  foreground assertions: l2-hosting-driver-final.log, state association.dsgfw0fv.
- Screenshot assertions additionally verify native pixels inside the Wine client
  rectangle in both architectures (HOSTING_RENDER_PASSED). Log
  l2-hosting-render-final.log, state association.bx75eg28; hosting32.png and
  hosting64.png retain the screenshots. The pixel is sampled inside the native
  client and away from the raised owner, rather than inferred from an ACTIVE ack.
- Reservation-only probes still pass both architectures, including an explicit
  assertion that reservation alone never hosts: l2-hosting-opt-out-final.log,
  state association.qsoa4wdf. Opt-in counterpart: l2-hosting-opt-in-final.log,
  state association.r6wo5va4. These files/states share the same checks directory.
- Shipping X11 smoke passes: two tray icons, zero overlaps/out-of-bounds windows
  and all eight native dialog geometry cases. Log l2-hosting-x11-smoke.log and
  checks/status=CHECK_PASSED; tray-layout.txt and linux-dialog.log hold assertions.
  It used prefixes/smoke-l2, with no real desktop/session changes.

## wexhost HWND binding checkpoint

Main b41028b1 implements WexWmBindHost for the explicitly selected Wayland
backend. It retains the existing WEX_NATIVE_WINDOW layout and all Unix-call
indexes, appending one fixed-width internal binding query to both 64-bit and
WoW64 tables. That query retrieves the compositor view token and this bridge
observer's controller handle; no connection-local objects or pointers cross
the boundary. The bridge binds private protocol up to v7, with dynamic frame
capability reporting only when the compositor offers v7. Older protocol peers
retain observation-only behavior. AUTO adoption remains disabled.

The PE helper owns global atoms, installs generic Wine properties before
ShowWindow and uses a standard window subclass for the host lifetime. BindHost
returns ERROR_IO_PENDING while the driver's group acknowledgement is pending;
repeat on the host's UI thread until success. Bind, configure, release and stop
belong to that thread; wrong-thread bind/configure/release are rejected. One
native view cannot use two hosts and one host cannot bind multiple native IDs.
Only server-decorated views are eligible at this checkpoint; CSD/borderless
requests return ERROR_NOT_SUPPORTED rather than installing an unverified proxy.

The helper subtracts the client area from a copy of the original window region,
updates that input hole on geometry changes and restores the original on
release. Callback references protect binding storage through reentrant HWND
notifications. Nested WM_ENTERMENULOOP/WM_ENTERSIZEMOVE notifications request
temporary frame focus and restore native focus only after the last loop exits
and the proxy is still foreground. Release, WM_NCDESTROY and UI-thread backend
stop remove owned atoms/properties/subclasses. Releasing the bridge observer
also destroys its compositor controller, restoring native decorations even
across the separate Wine graphics connection.

Verification uses WEX_ASSOCIATION_BRIDGE=1 with the maintained association
runner. Both 32/64 tests pass real wexhost API binding before map, native pixel
rendering, owner preservation, client hit exclusion before/after focus and
geometry changes, nested focus notifications, size mismatch/retry, wrong-thread
rejection, duplicate/stale binding rejection, exact original-region restoration,
atom cleanup, release, host destruction, backend restart and stop. The screenshot
capture is acknowledged through a test file gate before release, avoiding a
timing-based capture delay. These are notification-path tests, not acceptance of
real interactive shell menus or native resize negotiation.

Logs under /srv/protonexplorer/wex-wayland-l1/checks:

- l2-bridge-bind-build.log: combined WoW64 wexhost/Unix library build passes.
- l2-bridge-bind-verified.log, state association.txvqqlip: both PE binding/render
  probes and the original mixed-process L1 stacking assertions pass.
- l2-bridge-observation-regression.log, state wayland-backend.5vqsjt: both PE
  inventory/control/restart/transport-loss withdrawal probes pass.
- l2-bridge-x11-abi.log: both X11 bind/manage/release ABI probes pass.
- l2-bridge-x11-smoke.log: tray two icons, zero overlaps and eight native dialog
  geometry cases pass; checks/status is CHECK_PASSED.

Corrections:

- The probe's helper name bind conflicted with MinGW's Winsock declaration.
  Rename it wait_binding; both architectures then compile.
- An exact-region assertion used the constructed rounded shape as its baseline,
  but GetWindowRgn already reported a different shape before binding. Moving
  restoration after FRAMECHANGED alone did not resolve the assertion. Capture
  the actual GetWindowRgn state immediately before binding and compare that
  exact region after release; both architectures pass. Failed diagnostics are
  l2-bridge-bind-final.log, l2-bridge-bind-region-retry.log and
  l2-bridge-bind-region-debug.log; passing baseline log is
  l2-bridge-bind-region-baseline.log. No diagnostic print remains in the probe.
- An inspection guessed a nonexistent check-wayland-backend.py. The maintained
  backend runner is the self-contained check-wayland-backend.sh.

Next is Explorer's explicit lab-only adoption gate, binding before map,
WEX_WM_OBSERVED reconciliation without request loops, and correct CSD exclusion
and withdrawal cleanup. Complete real menu/taskbar/application, native resize,
fullscreen/workspace, scaling and launch routing acceptance before AUTO or
shipping session changes. No real desktop, installed runtime or production pin
was changed by these tests.

## Explorer lab adoption checkpoint

Main 97707ed6 adds the explicit WEX_WAYLAND_NATIVE_WINDOWS=1 environment gate.
It applies only without an X11 virtual-desktop window; X11 and the default native
session retain their previous selection. This is a test-session switch, not an
ISO/session default or completed L2 acceptance. Use the matching Wine 786643b
and compositor efb40ac9 topics. No production gitlink/branch was advanced.

Explorer now binds eligible SSD proxies before ShowWindow, preserving independent
owned toplevel mapping. Existing native client coordinates remain the anchor.
Wayland skips the X11 pixel-format/reparenting workaround. Complete OBSERVED
snapshots update native geometry, ownership, constraints and minimize state with
echo suppression. Pending minimize requests are tracked separately from observed
state, so an earlier observation cannot reverse a newer request. Foreground
events use Wine's existing driver acknowledgement path rather than X11 handles
or repeated native activation calls. Native size changes adjust the proxy and
reattach after matching buffers; observations from that movement are not requests.

This first gate leaves CSD/borderless, initially minimized, maximized and fullscreen
windows independently managed by the compositor. Active proxies are withdrawn
when the native view switches to an unsupported decoration/fullscreen/maximize
state, while observation continues so supported states can be adopted later.
Interactive resize/maximize affordances are disabled until native negotiation
exists. Taskbar coverage, owner/transient dialogs, scaling, fullscreen/workspace
transitions and native launch routing still need broader acceptance.

The compositor fix efb40ac9 removes a real adoption race. Hiding native SSD used
to emit a temporary NONE decoration snapshot before the frame scene pointer was
published. A shell could interpret that as a CSD switch and destroy its new
proxy, depending on reader/event-thread scheduling. Publish the fully allocated
group before the decoration notification; a raw observer regression asserts that
the SSD policy never briefly downgrades during activation. That assertion fails
against the old code and passes after the fix. Genuine native CSD changes still
detach and report CLIENT normally.

Maintained tests: scripts/check-wayland-shell.sh, check-wayland-shell.py and
wayland-adoption-probe.c. The runner creates isolated headless compositor,
private D-Bus, prefix/server and an actual explorer /shell. It matches the L1
session's disabled default keybindings. A 64-bit Explorer adopts a pre-existing
native SSD client; a 32-bit Explorer adopts one launched after its taskbar is
ready. The probes verify Explorer's architecture as well as their own.

Both cases pass one-proxy enumeration, native rendering before/after resize,
frame-edge hit testing, live title updates, native client pointer/button delivery
and logical proxy activation, a real Win32 system menu opening with frame keyboard
focus and cancellation restoring native focus, minimize/restore in both directions,
CSD proxy removal, SSD readoption, forwarded close and withdrawal cleanup. The
test-only native publisher now accepts title/resize/policy/state/click commands
and honors close requests. Its view stays in the compositor's normal scene graph.

Corrections and inspection notes:

- A raw decoration regression reproduced the intermediate NONE snapshot before
  the fix: ~/wexcomp-build/l2-shell-decoration-regression-before.log. Keep the
  passing final log separate; no coalescing delay is used to hide the transition.
- A test filename collision overwrote the existing L1 wayland-shell-probe.c in
  the working tree. It was caught before commit and restored byte-for-byte from
  HEAD; the new test is wayland-adoption-probe.c. Existing L1 callers are intact.
- PowerShell positional taskbar/session globs failed; use rg --files or rg with
  directory plus -g. No source changes resulted from those inspection errors.

Next: complete real taskbar interactions, transient/owner and late-state coverage,
native resize negotiation and move loops, then CSD task representation, scaling,
fullscreen/workspace and native application/launch acceptance. AUTO remains
disabled. The lab's working menu test does not establish native global-hotkey
forwarding, which remains L4.

Final verification logs on the Proton VM:

- checks/l2-shell-adoption-build.log: combined WoW64 wexshell target passes.
- checks/l2-shell-acceptance.log, state shell-adoption.0jq8rjhv: actual 64/32
  Explorer/PE probe pairs pass the complete lab scenario and screenshot checks;
  32-bit Explorer's native peer is launched after shell readiness. Each case
  retains initial.png, resized.png, menu.png, shell/peer/compositor logs.
- checks/l2-shell-disabled-acceptance.log, state shell-adoption.dwognx6x: the same
  SSD client stays unadopted with the gate absent after taskbar readiness.
- checks/l2-shell-x11-smoke.log: CHECK_PASSED, two tray icons with zero overlaps
  and all eight native dialog-placement assertions pass.
- ~/wexcomp-build/l2-shell-decoration-regression-final.log and
  l2-shell-no-xwayland-final.log: full compositor suite passes 37/37 with and
  without XWayland after the atomic decoration fix. The new assertion observes
  every raw decoration event, independent of bridge queue coalescing.

The check paths above share /srv/protonexplorer/wex-wayland-l1 as their root.
No test compositor or Wine server remains in use, and all screenshots originate
from isolated tests. No proprietary reference assets were added.

## Real taskbar input checkpoint (2026-10-14)

The maintained full-shell test now operates the native application's task
button with actual compositor virtual-pointer events. Both 64-bit and 32-bit
Explorer pass active-button minimize, minimized-button restore, inactive-button
activation after a desktop click, and native keyboard-focus restoration.
Right-click menus are opened through the same pointer; a virtual keyboard sends
Escape or the visible menu's N/R/C mnemonic for cancellation, Minimize, Restore
and Close. The final Close still waits for the native application to honor the
request and for its proxy to disappear. Existing native rendering, title/size,
system-menu focus and CSD withdrawal/readoption checks remain in the sequence.

The isolated one-client case locates MSTaskListWClass under Shell_TrayWnd and
samples the first button using its actual screen origin. It retains active,
minimized, restored and context-menu screenshots. Bounded screenshot polling
waits for title pixels to change on both minimize and restore; it excludes the
hover border/icon. This checks the lab's current theme, not XP visual fidelity
or acceptance across every theme. No production introspection hooks were added.
Compositor topic 50646994 adds only right-button and virtual-keyboard commands
to the existing test publisher; production pins and Wine topic are unchanged.

Corrections:

- The first menu probe tried GetMenuBarInfo/GetMenuItemRect across processes.
  Wine menu handles are process-local, so that failed at the minimize entry
  (checks/l2-taskbar-controls-first.log). Replace that inspection with actual
  keyboard menu input; popup readiness still uses its visible window.
- Immediate minimize screenshots could still contain the active button's last
  frame. Waiting only for the restore repaint compared two active images and
  failed (l2-taskbar-controls-final.log, l2-taskbar-controls-repaint.log and
  l2-taskbar-controls-fix.log). A tentative minimized-state guard was tested,
  then removed: the original taskband code passes once both image transitions
  are awaited. l2-taskbar-before-regression.log records that original-code pass;
  no taskband fix or unresolved visual defect is claimed by this checkpoint.
- Inspection commands included a nonexistent search path and README filename;
  no files were changed. The topic worktree has no origin remote, so its first
  push failed; pushing to the documented zcomputerwiz/labwc URL succeeded.

Final logs under /srv/protonexplorer/wex-wayland-l1/checks:

- l2-taskbar-final-build.log: combined WoW64 wexshell target passes with the
  original taskband source restored.
- l2-taskbar-acceptance.log, state shell-adoption.cvz50gf_: complete scenario
  passes in both actual Explorer architectures, including visual transitions.
- l2-taskbar-disabled.log: absent lab gate leaves the native client unadopted.
- l2-taskbar-x11-smoke.log: CHECK_PASSED, two tray icons with zero overlap or
  out-of-bounds placement and all eight native dialog-placement cases pass.

Next: owned/transient windows and parent changes/closure, then interactive
move/resize negotiation, CSD task representation and scaling/state/application
launch acceptance. Ordinary task-button/menu checks do not establish grouped
task menus, native global shortcuts or completion of L2. AUTO stays disabled.

## Owned native windows checkpoint (2026-10-14)

The isolated full-shell runner has an additional WEX_SHELL_TEST_OWNERS=1 mode.
Both actual 64-bit and 32-bit Explorer now pass a native SSD child mapped with
an existing native parent: two distinct proxy HWNDs, correct Win32 ownership,
active scene groups and screenshot-verified native child content. The native
client removes and reassigns its parent after mapping; Explorer follows both.
The parent then switches to CSD and back to SSD: the same child HWND survives
the parent's frame withdrawal and relinks to the returning proxy. Finally the
native parent closes while the child stays open; the same child remains hosted,
unowned, with one proxy and no duplicate. Forwarding the child's Close removes
its native surface and the last proxy.

This exposed two independent failures, fixed at their owning layers:

- checks/l2-owner-first.log: owner removal never reached Explorer. The compositor
  initialized foreign ownership only on map. Topic 7cf88027 listens to the xdg
  toplevel set_parent signal, updates the standard foreign-toplevel relationship
  (including NULL), and removes its listener on destruction. Initial map and
  later changes share the same helper. No private protocol extension is needed.
- checks/l2-owner-parent-listener.log: parent changes worked, but parent closure
  destroyed the child's proxy. Wine's DestroyWindow recursively destroys owned
  windows on the same thread, independently of their native lifetimes. Explorer
  now detaches directly owned native proxy HWNDs before removing an owner host,
  across withdrawal, unsupported-policy, failed-binding and shutdown paths.
  The native owner ID is retained for later relinking. This applies only to the
  explicit Wayland gate; the shipping X11 path keeps its existing behavior.

The test publisher adds initial owned mapping, parent changes and independent
parent destruction. The normal taskbar scenario still uses its original mode.
No tests or fixes change production pins or enable automatic native adoption.

Verification on the Proton VM, under /srv/protonexplorer/wex-wayland-l1:

- checks/l2-owner-shell-build.log: combined WoW64 wexshell target passes.
- checks/l2-owner-lifetime.log: initial owned/parent-change/closure scenario
  passes both architectures before the additional policy-return coverage.
- checks/l2-owner-acceptance.log, state shell-adoption.io_hpx02: expanded scenario
  passes both architectures, including CSD withdrawal and SSD parent relinking.
- checks/l2-owner-taskbar-regression.log, state shell-adoption.op16lqnd: full
  task-button/menu/native rendering scenario still passes both architectures.
- checks/l2-owner-x11-smoke.log: CHECK_PASSED, two tray icons without overlap or
  out-of-bounds placement and all eight dialog-placement checks pass.
- ~/wexcomp-build/l2-owner-suite.log and l2-owner-no-x-suite.log: full compositor
  suites pass 37/37 with and without XWayland. Retained output directories are
  /srv/protonexplorer/wex-wayland-l2/owner-suite-output and
  /srv/protonexplorer/wex-wayland-l2/owner-no-x-suite-output.

Remaining owner acceptance includes initially unmapped/late native parents,
real application modality/popups and more complex owner trees. This checkpoint
does not complete L2. Native move/resize negotiation, CSD task representation,
scaling/state/workspace and launch/application acceptance remain pending.

## Native parent remap checkpoint (2026-10-15)

The maintained owner scenario now unmaps and remaps the parent's native surface.
The same child HWND stays hosted and loses its Win32 owner when the parent is
unmapped. The remapped parent has one new eligible proxy; neither the standard
native observer nor Explorer restores the old relationship automatically. An
explicit xdg_toplevel.set_parent after remap relinks the child to the new proxy.
Both actual Explorer architectures pass, including the existing subsequent
parent/child close and duplicate-proxy checks. This follows the installed stable
xdg-shell protocol: only mapped surfaces can have children, and remapping does
not restore their parent relationship. An initially unmapped parent therefore
must be treated as NULL, not as a deferred relationship to restore implicitly.

Compositor topic d710b4c6 adds only publisher observation/control commands.
checks/l2-remap-first.log records a harness timeout: consecutive remap and query
commands were read ahead by fgets, leaving poll unaware of the buffered line.
The publisher's command stream is now unbuffered; no product fix was needed.
checks/l2-remap-acceptance.log, state shell-adoption.lbpv4yt_, passes both
architectures; the publisher build log is l2-remap-publisher-build.log. These
paths are under /srv/protonexplorer/wex-wayland-l1. The first XML inspection used
a nonexistent tests-local path; the protocol was read from the guest's installed
/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml instead.

Real application modality/popups and complex owner trees still need acceptance.
Next is native move/resize interaction. Adoption remains opt-in.

## Native frame move loops and pointer capture (2026-10-15)

Topics: Wine codex/wayland-l2 at 903ac75, compositor codex/wayland-l2 at
deb9e06a. Production Wine is 2e8bcfa following Claude's independent header-only
build fix; the L2 topic merges that fix. Ordinary wine/ is aligned to the new
gitlink, while topic development remains in build/wayland-l2/wine. The private
test compositor and assembled Proton-VM directories remain in use; nothing
was installed into real desktop sessions or Gemini's WSL distributions.

The maintained full-shell scenario now drives four real move operations in
both actual Explorer architectures: task-menu Move finished with Enter, Move
rolled back with Escape, a title-bar press/drag/release across native content,
and a menu move interrupted by WM_CANCELMODE. Each verifies frame keyboard
focus, observed physical pointer capture, exact local displacement, final native
geometry within one pixel, native content pixels, release of capture and return
of native keyboard focus. Cancellation compares against the original rectangle;
matching Win32/native geometry at an accidentally reverted location is not
accepted. The existing taskbar, native resize observation, title, policy and
close checks remain in the same sequence.

The implementation follows the existing ownership split:

- Explorer activates Move/Size menu targets before posting their system command.
  Local geometry requests are separate from observed native rectangles. Older
  queued observations cannot undo a local move; matching observations complete
  the request. A two-second bounded fallback permits recovery from an unmet
  request. Reconciliation itself never creates a new request.
- Wine uses its Win32 move loop only for active HOST windows on a private v8
  compositor. Generic Win32u fixes remember whether a mouse press was observed
  before treating a release as consumed, and end the loop on lost capture. This
  supports menu/keyboard starts, mouse starts and explicit cancellation.
- Private protocol v8 adds capture_foreign_pointer(epoch,capture) and an observed
  foreign_pointer_capture acknowledgement/revocation. Capture requires an active
  group, physical frame focus and the current focus epoch. Stale acquire/release
  requests cannot displace a newer capture. Capture redirects pointer hit testing
  to the Wine frame even across its native client hole, and is revoked on physical
  focus loss or group/resource teardown. Wine's SetCapture hook requests it only
  for GUI_INMOVESIZE; the CAPTURE scalar property reflects acknowledgements.
- Pointer warps now pass through the normal cursor motion/hit-test pipeline,
  retargeting pointer focus before sending local coordinates, and subtract xdg
  window-geometry offsets. Pure zone moves commit existing buffers immediately,
  including Escape rollback without a repaint. Hosted proxies ignore old zone
  placement echoes because the native observation path reconciles actual state.

The compositor suite adds stale capture requests, input routing across the
client hole, focus-loss and size-mismatch revocation, and an exact pointer-warp
check with nonzero xdg geometry. The full suite is now 38 checks in each build.
The fixed WEX_NATIVE_WINDOW ABI and existing Unix-call indexes are unchanged;
wexhost's observer can still bind v7. XML copies are synchronized at v8. No
native size-negotiation or maximize UI is exposed by this checkpoint.

Reproduced failures and corrections (logs under
/srv/protonexplorer/wex-wayland-l1/checks unless noted):

- l2-move-first.log, l2-move-focus.log and l2-move-diagnostic.log: the task menu
  left the tray foreground, and the driver swallowed SC_MOVE even without a
  pointer serial. Activating the target alone was insufficient. V8 hosted moves
  now use Wine's loop; stock/old-server dispatch paths are retained.
- l2-move-warp.log: the newly entered loop exited on queued focus messages because
  its initiating left button was already up. The generic press-tracking fix
  keeps it alive. l2-move-loop-release.log and l2-move-geometry.log then exposed
  incorrect warp targeting and absent cross-native pointer capture.
- l2-capture-move.log initially reached a moved frame but returned to its old
  position; the initial assertion compared matching final Win32/native rectangles
  without requiring the displacement to persist. The stricter final-position
  check plus separate requested geometry prevents that false pass. Current
  screenshots show the native content at the requested new position.
- l2-move-final.log and l2-move-zone.log: Escape restored Win32 geometry while
  native content stayed moved. Suppressing old placement echoes alone was not
  enough: pure position changes could wait forever for a repaint before sending
  zone placement. Immediate placement/commit fixes the actual rollback.
- l2-move-cancelmode-before.log reproduces a loop that outlived WM_CANCELMODE.
  The capture-loss exit fixes it; l2-move-acceptance.log passes the new case.
- A quoted inline shell trap failed before changing pinned source. The pin build
  was rerun from a saved script with a restoration trap. Restored topic files
  initially kept archive timestamps older than pinned objects, so make did not
  recompile the topic driver: l2-move-owner-regression.log failed to adopt. Restore
  with fresh modification times and rebuild; l2-move-owner-final.log passes.
- Merging production 2e8bcfa into the topic conflicted only at the new capture
  property; keep the production names plus the topic's additional property.
  Early inspections used nonexistent source/header paths; actual definitions
  were located with rg. No generated or private assets were committed.

Final verification:

- l2-move-production-pin-build.log: full combined WoW64 build passes with the
  candidate owned modules and exact 2e8bcfa versions of all topic-modified Wine
  files. The topic-only wlr XML is unused by the pinned Makefile. Existing assembled
  source/build layout was reused without a reset; no fresh-checkout claim.
- l2-move-topic-restored-build.log and l2-move-cancelmode-build.log: full combined
  topic builds pass after restoring current source with fresh timestamps.
- l2-move-acceptance.log, state shell-adoption.t99mq7c4: complete four-operation
  move scenario passes both actual Explorer architectures with retained screenshots.
- l2-move-owner-final.log, state shell-adoption.2it_ejoi: full owner/remap/closure
  scenario passes both architectures. l2-move-disabled-final.log passes gate-off.
- l2-move-bridge-final.log, state association.254nru4m: 32/64 real bridge binding,
  region/lifetime checks and mixed-process shared Win32 stacking pass.
- l2-move-x11-cancelmode.log: final X11 CHECK_PASSED, two tray icons with no
  overlap/out-of-bounds placement, and all eight native dialog checks pass.
- ~/wexcomp-build/l2-move-suite.log and l2-move-no-x-suite.log: 38/38 pass with
  and without XWayland, including offset geometry and capture assertions.
  Output directories: /srv/protonexplorer/wex-wayland-l2/move-suite-output and
  /srv/protonexplorer/wex-wayland-l2/move-no-x-suite-output.

Next is native resize negotiation. WAYLAND_L2_WORKORDER.md records the design
and acceptance sequence, including the stale bind-time theme region reported
by Claude. Repair live frame-shape bookkeeping before enabling resize. Then
separate desired size, native xdg acknowledgement and actual committed buffer
size, honor constraints and cancellation, and keep rendering/input coherent
during asynchronous updates. CSD, scaling/state/workspace, native application
launch and real-application acceptance are still pending. L2 is not complete.
