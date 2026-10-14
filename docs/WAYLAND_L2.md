<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# L2 implementation and acceptance

Status: in progress; no acceptance claimed.

Scope and agreed decisions: WAYLAND_L2_WORKORDER.md. Project source is on master;
Wine and compositor work use codex/wayland-l2, initially f0108dc and 9a47d74f.
Host compositor worktree: build/wayland-l2/wexcomp. VM compositor checkout remains
~/wexcomp-build, used only for isolated nested tests. No default/session deployment.
Current L2 topics: Wine 786643b (host worktree build/wayland-l2/wine), compositor
fe3abb94 (build/wayland-l2/wexcomp). The ordinary wine/ submodule checkout remains
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
