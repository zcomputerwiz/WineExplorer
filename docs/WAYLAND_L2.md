<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# L2 implementation and acceptance

Status: in progress; no acceptance claimed.

Scope and agreed decisions: WAYLAND_L2_WORKORDER.md. Project source is on master;
Wine and compositor work use codex/wayland-l2, initially f0108dc and 9a47d74f.
Host compositor worktree: build/wayland-l2/wexcomp. VM compositor checkout remains
~/wexcomp-build, used only for isolated nested tests. No default/session deployment.

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
