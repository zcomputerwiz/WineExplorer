<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# DWM architecture and implementation plan

Date: 2026-11-02. Design against the **working tree** first inspected on Wine branch `dwmapi-d4`, including its then-uncommitted changes to `dlls/dwmapi/{dwmapi_main.c,dwmapi.spec}` and `include/dwmapi.h`. During final verification the shared checkout had advanced independently to `development` at `d575a8ee` (DWM review fixes); the described table/version behavior was rechecked there. This task changed no Wine code or checkout state. This is a plan, not implementation or runtime acceptance.

## 1. Decision and actual baseline

Deliver Vista–11 application semantics and independently authored open themes, with XP parity as the default. A thin `src/programs/dwm/dwm.exe` owns Windows-facing state/policy **per ordinary Wine desktop** within a Linux login session. It owns no display connection, desktop pixels, rendering context, X Composite selection or Wayland scene. Applications retain standalone DComp presentation and bounded dwmapi fallback without it.

| Layer | Authority / actual files and functions |
| --- | --- |
| Identity, access, lifetime | wineserver: `wine/server/{window.c,winstation.c,queue.c,user.h}` and existing token/handle checks. |
| DWM policy | New `src/programs/dwm`: attributes, colorization, thumbnail relationships, composition capability publication and completion accounting. Program/directory **not found** today. |
| Application rendering | `wine/dlls/dcomp/device.c:dcomp_device_internal_commit` → `wine/dlls/dxgi/swapchain.c:dcomp_binding_target_scene` → `wine/dlls/wined3d/swapchain.c` → Wine client surfaces. |
| X11 desktop pixels | `src/dlls/wexhost/compose.c` in the Explorer process hosting the WM: `compose_set`, `compose_event`, `compose_paint`, `name_surface`, `paint_window`, `paint_shadow`. |
| Wayland desktop pixels | Repository-root `wexcomp`: `src/protocols/wex-window.c`, `src/protocols/wex-scene.c`, `src/output.c:output_frame_notify`. |
| Win32 frame behavior | `wine/dlls/win32u/defwnd.c` and `wine/dlls/uxtheme/window.c:UXTHEME_NonClientFrameDraw`, `nc_update_region`, `UXTHEME_SetWindowRgn`. |
| Shell previews/settings | `src/dlls/wexshell/taskband.c`, `alttab.c`, `desktop.c:WexShellStart/WexShellStop`. |

The draft is not a reliable current-code inventory:

- `attribute_table[256]` and `thumbnail_table[64]` are process-local, protected by `thumbnail_cs`. `get_stored_attribute` exists; `set_stored_attribute` and `WexDwmAttr*` property storage are **not found** in this working tree. HWND plus `IsWindow` reclamation does not prevent handle-reuse bugs.
- `DwmExtendFrameIntoClientArea` is an unconditional success stub; it neither validates nor saves margins. `DwmEnableBlurBehindWindow` validates HWND/pointer, then discards the request. Iconic setters validate some arguments, then discard pixels; invalidation only checks HWND.
- `DwmIsCompositionEnabled` derives its answer from `RtlGetVersion` (true for >= 6.3), not shell preference/backend health. `NCRENDERING_ENABLED` always returns true, `CLOAKED` zero and `HAS_ICONIC_BITMAP` false.
- `DwmGetCompositionTimingInfo` estimates refresh/QPC alignment; `DwmFlush` sleeps to an estimated boundary with `NtDelayExecution`. Neither observes presentation. The two historically missing exports now exist in `dwmapi.spec`.
- `docs/DWMAPI_BEHAVIOR.md` records an older measured baseline. `wine/dlls/dwmapi/tests/dwmapi.c` already contains tests worth extending; many skip when composition is off.

Rules from `CLAUDE.md` remain: combined WoW64 (`--enable-archs=i386,x86_64`), open art only, GPL-2.0-or-later headers on new project files, existing notices retained, future Wine edits listed in `docs/STOCK_CHANGES.md`. This task writes only this document and makes no commit.

## 2. Transport decision

**Use a small wineserver-mediated, desktop-bound control channel; use existing sections/events for snapshots and completion.** No pixels, visual-tree traversal or rendering algorithms in wineserver.

| Candidate | Benefit | Decision / cost |
| --- | --- | --- |
| Wine requests and channel objects | Actual caller/thread identity, HWND/desktop validation, object rights, death cleanup and established WoW64 handle conversion. | Selected for connection, bounded control, handle transfer and lifecycle. More stock delta than pipes, justified by secure desktop and lifetime rules. Keep policy in dwm.exe. |
| Named objects/shared sections alone | Existing NT primitives; cheap reads/waits. | Selected only as broker-issued data plane. Global/Local names and DACLs do not prove the calling thread's desktop or HWND authority. No publicly writable global page. |
| Local RPC/named pipe | Existing NDR or overlapped I/O; avoids a custom server mailbox. | Rejected as primary. Still needs endpoint protection, desktop authentication, handle transfer and event/lifetime machinery. `kernel32/sync.c:GetNamedPipeClientProcessId` exists, but PID/impersonation does not identify a multithreaded caller's current desktop. |
| Win32/X window properties | Existing scalar hints such as `_WEX_DROPSHADOW`. | Binding hints only, never canonical state/access checks or region/bitmap transport. Structs/pointers break across bitness; HWND/XID reuse and multi-property tearing remain; Wayland needs a different path. |
| D-Bus | Existing wexhost system-service integration. | Keep for login1/LightDM/system settings, not per-frame DWM traffic. `wexhost/unixlib.c` uses `DBUS_BUS_SYSTEM`. No spawned helper tools. |

### 2.1 Concrete private protocol and stock delta

Proposed **new, not found** files/functions: `wine/server/dwm.c`, `dwm_connect`, `dwm_channel_io`, `dwm_register_backend` requests; `wine/dlls/dwmapi/dwm_client.c` and `wine/include/wine/dwm_protocol.h`. A versioned opcode envelope avoids a new server request for every attribute.

- Declare requests in `wine/server/protocol.def`, regenerate protocol headers and register `server/dwm.c` in `wine/server/Makefile.in`. The channel uses existing waitable-object machinery; sections/events need no new NT syscall or object type.
- Connect binds to the server's actual thread desktop/window station. Server-stamped sender identity replaces wire PID/desktop-name claims. Recheck the calling thread's current desktop on every operation; one process may need multiple connections.
- Add a narrow validator in `server/window.c`: resolve endpoints, check operation-specific rights and mint window lifetime tokens with monotonic serials. Revalidate tokens when applying messages; destruction revokes tokens and queues removal. Do not retain destroyed windows indefinitely.
- Attach endpoint/epoch metadata to `struct desktop` in `server/user.h`; revoke on destruction in `winstation.c`. Channel teardown removes process registrations. Add explicit `process.c` exit glue only if normal object release cannot cover it.
- Broker registration requires a one-use capability minted for the actual desktop-window owner, checked against `desktop->top_window`, then explicitly transferred to the child. No executable-name authentication, public first-writer mutex or automatic system role for dwm.exe.
- Register backend controllers similarly. A session renderer attaches ordinary desktops through separately authorized channels; it receives no unrestricted cross-desktop enumeration right.
- Compile the private client helper into dwmapi and, when needed, dcomp from shared private source/header. It uses Wine/NT primitives and never imports wexhost. Standalone Wine must not require project DLL installation.

Envelope: fixed-width opcode/version/length, request ID, connection epoch, window token, revision and capabilities. Marshal canonical HWNDs with Wine user-handle helpers; never cast 64-bit tokens into x86 pointers. Arrays use checked offsets/lengths and explicit alignment; no C `long`, `size_t`, COM pointer, HRGN or HBITMAP on the wire. Assert layouts in both architectures.

Small calls use bounded messages. Copied blur rectangles/iconic pixels use section handles duplicated with required access through wineserver. Give readers read-only snapshots. Cap region count, dimensions, stride, allocation, queue depth and per-client bytes. Copy untrusted shared input before validation/application so later sender writes cannot mutate an accepted request. Unsupported versions/overflow leave prior state intact.

**Mechanism chosen (P1 slice 1):** the private process information class `ProcessWineDwmChannel` (1004) on `NtSetInformationProcess`, one operation per call (connect, get capability, broker register, send, recv, reply) described by `struct dwm_channel_call` in `include/wine/dwm_protocol.h`; the server requests behind it are `dwm_*` in `server/dwm.c`. Chosen over a new NT syscall (no syscall table change, WoW64 passes it through) and over named objects (no first-writer race, identity is stamped by the server). Verified by `dlls/dwmapi/tests/dwm_channel.c` (97 checks, Wine 32/64-bit): no-broker status, sizes 0/1/100/max, max+1 refused, 4x100 concurrent echoes, epoch advance and stale handles, queue quota, per-desktop endpoints, no handle leak. A test becomes the desktop-window owner by creating the desktop window itself, as explorer does.

### 2.2 Desktop/security rules

`server/winstation.c:cross_desktop_denied` rejects other-desktop access when sysrun enforcement is active, except the system role. `window.c:window_desktop_denied` applies it to guarded mutations; `queue.c` applies it to messages. It is not a universal HWND guard: `get_window_info` and `set_window_property` do not themselves call it. New DWM requests must explicitly validate.

Use `get_thread_desktop`/desktop object rights, existing cross-desktop policy and token/handle checks. Additionally prohibit thumbnail/capture relationships across desktops/window stations even for a system broker: its own privilege cannot substitute for the caller's. Allow foreign-process sources on the same desktop subject to capture policy. Measure each setter/getter's native rights; do not impose a blanket same-process rule on all dwmapi calls.

Native thumbnails require top-level endpoints and an owned destination, with the documented desktop-window exception. Their handles belong to the registering process, despite service/compositor visibility. See [DwmRegisterThumbnail](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmregisterthumbnail). Key broker records by `(connection, local handle, generation)`, not a globally usable HTHUMBNAIL.

Secure state/images/timing pages never map into ordinary connections. Names, atoms, XIDs and same-user DACLs cannot bypass this. Do not weaken `sysrun_process_is_system` or Chromium restricted-token rules for graphics. Project-private negative tests must cover enforcement on and safe behavior when it is off.

### 2.3 DwmFlush and timing

Control IPC is not the frame clock. Publish a read-only completion page containing epoch, active output, QPC frequency/conversion, submission/consumption/presentation serials, refresh estimate and validity bits. A 32-bit sequence lock protects split 64-bit fields for x86 readers; use release/acquire publication.

`DwmFlush` snapshots the caller's outstanding producer serials, sends one barrier, and waits non-alertably on a broker-issued per-connection event until the serial predicate and applicable presentation boundary complete. Recheck after arming the wait; one auto-reset event cannot represent all clients. Timing reads need no service round trip. No unconditional 16 ms polling.

The caller's queued DirectX work must reach the screen; this does not flush the whole session. See [DwmFlush](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/nf-dwmapi-dwmflush). Hook ordinary DXGI/WineD3D producer submission as well as DComp; XDamage alone does not identify outstanding caller GPU work.

First expose explicitly labelled submission completion, then implement X Present completion/idle handling and applicable GL synchronization, or Wayland presentation feedback, before claiming Flush/presentation conformance. XFlush/XSync and frame callbacks alone are insufficient. Map backend timestamps into Wine QPC; distinguish submitted, consumed, vblank and displayed. Define output selection and test mixed refresh, occlusion/minimize and fullscreen bypass. Do not fabricate unknown counters.

Never wait under `thumbnail_cs`, DComp/device/WineD3D locks, USER/window-data locks, `x_mutex` or a Wayland callback. Broker/backend death cancels waiters. Proposed stalled-backend watchdog: 2 s, configurable in private tests; fail rather than return success for unpresented work. Measure native error mappings where possible.

### 2.4 Absent broker / composition off

No application API starts dwm.exe. Cache failed connection briefly, invalidate on endpoint epoch change, and avoid reconnect loops or launches from DllMain.

Keep process-local desired attributes, copied margins/blur and thumbnail bookkeeping as fallback. Copy regions/bitmaps while handles are valid. Preserve scalar behavior that can be fulfilled locally. Desired setters keep validated local state; colorization reads use persisted defaults/preferences. Proposed disconnected contract: DwmFlush, DwmGetCompositionTimingInfo and new DwmRegisterThumbnail calls return DWM_E_COMPOSITIONDISABLED after argument validation. Already-issued local thumbnail handles can still update/query bookkeeping or unregister, but render no pixels until authorized reattachment. Verify per-function native argument precedence and keep any intentional disabled-mode divergence explicit. A success result must not claim live thumbnail pixels.

Product default: `Shell\Composition=0` or absent active broker/backend means `DwmIsCompositionEnabled=FALSE` and `uxtheme!IsCompositionActive=FALSE`. DComp device creation/Commit/Present remain available through ordinary driver presentation. Do not globally spoof Windows version or enable fictitious composition for Chromium. Any per-app reporting exception is opt-in and independently tested.

On reconnect validate still-live fallback records; canonical connected state wins over stale mirrors. Registry preference alone never enables composition. Windows 11's no-op DwmEnableComposition does not measure native composition-off semantics: add Vista/7 controls when available and label XP product policy separately.

## 3. Lifecycle and ownership

**One policy broker per ordinary desktop, one pixel renderer per display.** A station-wide broker would cross desktop boundaries routinely and require broader authority. This topology is a project choice, not a claim about Microsoft's implementation.

- `wine/programs/explorer/desktop.c:manage_desktop` already creates/selects/switches the named desktop, creates its desktop HWND, optionally loads wexshell and launches the command. Add one optional startup hook after desktop ownership exists and before command launch. Keep the helper in project code.
- When composition is requested, the manager registers an endpoint and uses CreateProcessW with explicit path/desktop and transferred capability to start the packaged native 64-bit dwm.exe. WoW64 clients use the same broker. Build both program architectures; test an x86 desktop manager and explicit native-path selection without redirection accidents.
- WexShellStart starts/adopts the renderer and attaches its channel. Effective composition becomes true only after backend acknowledgement. WexShellStop detaches it; a broker can remain while applications/desktop remain. Calling WexCompositionSetEnabled inside dwm.exe would address its own process-local wexhost state, so do not do that.
- The desktop manager monitors the process handle on a worker/timer. Proposed restart backoff: 250 ms, 1 s, 4 s; stop after three crashes/minute. Readiness has a bounded timeout. No blocking restart wait on the desktop message thread. Publish unavailable and notify once per transition.
- Logoff/desktop destruction stops the broker. Service channel references must not keep an otherwise dead desktop alive: exclude them from ordinary desktop-user accounting or explicitly stop on last ordinary user/manager departure. Avoid reference cycles.
- Explorer restart can reclaim its server-validated controller capability after old ownership dies and adopt a surviving broker. Renderer restart changes backend generation and reapplies canonical state before effects become available.
- `explorer /desktop=Setup`, `/desktop=OOBE` and arbitrary NAME use the same optional hook even without wexshell/EnableShell. Default XP installer desktops launch no DWM. Requested composition without a renderer remains opaque/off; never create another X compositor.

X11 still has one `_NET_WM_CM_S<screen>` owner. Current compose.c has one global desktop, not multiple desktop roots. Extend the owning wexhost with an authorized desktop-root registry and active-desktop selection; other Explorer instances attach to that renderer. Do not steal an unrelated compositor's selection.

**Winlogon initially has no DWM broker/effects.** The elevation broker creates/switches it under existing system-role rules. `reserved_desktop_name` protects Winlogon/Screen-Saver creation; `set_input_desktop` requires DESKTOP_SWITCHDESKTOP, protects secure transitions and records recovery ownership. Preserve the recent manage_desktop SwitchDesktop fix.

`queue.c:send_hardware_message` now requires sender desktop, target desktop and input desktop to coincide; host events cannot choose the input desktop. DWM is never another input/switch authority.

`winex11.drv/secure.c:x11drv_secure_init/x11drv_secure_exclusive` gives non-Explorer Winlogon processes a dim snapshot, overlay and input grabs. A GUI-initialized dwm.exe there could steal grabs; launch none and initialize no GUI driver there. Before enabling outer composition with consent, add secure-switch notification/renderer acknowledgement, suspend ordinary capture/previews and prevent normal composition from painting above consent. Until that gate passes, disable outer effects before secure entry. A future secure broker must be separately system-launched and isolated while retaining one authorized renderer.

LightDM greeter runs in a separate account/prefix/display, not this user's Winlogon desktop. Lock/user switching keeps existing D-Bus paths. Wayland secure presentation/session-lock isolation requires separate acceptance; X11 grabs prove nothing there.

## 4. Broker ↔ compositor link

Proposed `src/include/wexdwm.h` and `WexDwmApplySnapshot/WexDwmReadEvents` are **not found** today. Implement through `wexhost.c`, `wexhost.spec`, `wexhost_unix.h`, `unixlib.c` and existing src/include Unix-call declarations, following fixed-size WoW64 records. Extend `wm.c` dispatch; current composition entry points live in x11wm.c, not generic backend dispatch.

`application DLL → server channel → dwm.exe → backend channel → Explorer PE bridge → wexhost Unix backend → existing compositor`.

Reverse: `source lifetime/damage + consume/present completion → Unix event queue → PE bridge → broker pages/events → DLLs/shell`.

An Explorer PE worker takes immutable batches, makes bounded Unix calls and publishes results after return. Acknowledged storage, applied policy and presented pixels have separate revisions. No renderer-lock-held application callback or synchronous service round trip.

### 4.1 X11 thread/lock model

`x11wm.c:wm_loop` drains events and calls compose_paint under x_mutex, flushes X, unlocks, then polls display/wake pipe (up to 1000 ms idle). `composition_set/shadow` use `begin_call/end_call` and that mutex; composition_get locks directly. queue_mutex protects the separate WM queue. There is no compositor thread separate from the WM loop.

- Copy state before taking x_mutex, validate backend generation, apply atomic batches on wm_loop and wake it with existing wake_loop. No blocking Wine IPC, NT waits, PE callbacks or broker calls while locked.
- Copy reverse events while locked, unlock, then notify the bridge. Keep queue/X lock ordering acyclic; drain the queue before making Unix calls. Present handlers enqueue completion serials.
- Owner-side `winex11.drv/window.c:create_whole_window` publishes a versioned binding hint alongside current whole-window/shadow hints. An authorized driver message registers `(window lifetime token, XID, XID generation, desktop token)`. Do not use SetWindowPos(SWP_FRAMECHANGED) as transport.
- Correlation properties are not authorization. Reject stale generations after visual recreation, resize/remap and destruction. Keep Wine's deeper client surfaces under Wine driver ownership.
- compose_event supplies damage/lifetime; name_surface supplies pixmaps/Pictures; paint_children queries stacking. Damage to a source invalidates subscribed thumbnail destinations. Material damage expands for blur sampling; repaint for damage, explicit barriers or active effects only.
- Add X Present at compose_paint's output stage, retaining buffers until idle/release. Disable fullscreen bypass when required sources/effects/capture cannot be supplied, or implement a tested direct-scanout completion/source route.

Today Unmap calls remove_window and frees the surface. Retain/copy the last immutable source **before** release; a pointer to a freed Picture is not a cache. Budget bytes/age, purge on desktop/security transitions, and do not promise live minimized content when the application stops presenting.

### 4.2 Wayland

Actual files: `wexcomp/protocols/wex-window-v1.xml` (manager/window v11), `wexcomp/src/protocols/wex-window.c` and `wex-scene.c`. `src/wexcomp/src/wex_wm.c` and `wex-window-manager-unstable-v1.xml` are **not found**. Synchronize protocol copies in `src/dlls/wexhost/wex-window-v1.xml` and `wine/dlls/winewayland.drv/wex-window-v1.xml`; negotiate capabilities instead of assuming a future version number.

Extend privileged controller transactions for DWM policy, source subscriptions and completion. `wex_client_is_privileged` recognizes the private socket; it is not a Wine desktop authority. The PE/server bridge must bind desktop capabilities. Normal socket clients cannot become controllers with caller-supplied token strings. Use compositor-generated lifetime handles, not titles, PIDs or another connection's local wl_surface ID.

Preserve `wex_scene_create` / `wex_scene_commit_resize` native/frame groups and physical-input ownership. `waylandhost.c:update_mask` maintains the outer frame/client hole. waylandwm.c binds manager versions up to 9 although XML is v11: preserve old-peer fallback and L2 focus/resize transaction rules.

Apply policy on the compositor loop. Return buffer damage/commit, source generation/release and actual output feedback through events. output_frame_notify and wlr_scene_output_send_frame_done provide scheduling, not display proof; wire presentation feedback and clock conversion separately.

Thumbnail passes use existing window/group sources, including native content and Wine frames, without recursively sampling their destination. Offscreen effects/thumbnails inside wexcomp are allowed; a second scene server or native-pixel-copy hosting path is not. Keep Wayland objects on their owner thread and defer Wine UI work, preserving L2's USER/window-data lock-order fix.

**One compositor remains:** dwm.exe sends metadata/relationships/barriers; images/GPU work stay in app backends or the existing compositor. Only compose.c owns root/desktop X redirection; only wexcomp owns scene/output. Application-local DComp combines a target's content, not the desktop.

## 5. State, migration and recovery

| State | Canonical owner / lifetime |
| --- | --- |
| Window/process/desktop identity, ACLs and opaque tokens | wineserver; death revokes bindings. Channel metadata only, no theme/blur algorithm. |
| Requested/effective attributes, copied margins/blur, cloak reasons | dwm.exe, keyed by window lifetime; destroy purges state. |
| Composition capability/mode, colorization/theme policy | dwm.exe; registry persists preference, backend acknowledgement determines effective availability. |
| Thumbnail relationships/iconic metadata | dwm.exe; process-local public handles, source/destination/process-death cleanup. |
| Pixmaps/buffers/last images/effect caches | Existing compositor; generation/refcount/fence release, quotas and capture exclusions. |
| DComp trees, COM refs and producer resources | App dcomp/DXGI/backend initially; broker gets target identity/capability/serial only. |

Extract current tables into fallback helpers without losing D4 fixes. Add native field-specific validation tests before replacing the permissive 4-byte setter. Route supported calls through the broker; local caches become revisioned mirrors. Connected cross-process Get sees canonical accepted state.

Store whole requests atomically: signed margins, GetRegionData rectangle copies, selective blur flags and color default/none sentinels. Distinguish requested cloak from effective CLOAKED reason bits. Painted visibility, hit testing and shell enumeration must agree; skipping pixels alone creates invisible interactive windows.

Iconic setters copy premultiplied BGRA before return so callers can delete HBITMAP immediately. Message/invalidation requests occur outside locks; renderer owns imported images. Confirm native dimensions/message packing using host probes rather than copying the draft's unverified MAKELPARAM(max_w,max_h).

Client replay alone cannot recover an attribute set by a process that has since exited while the target HWND survives. Publish immutable revisioned recovery checkpoints into broker-created sections retained by its desktop channel. Make checkpoint publication and acknowledgement one ordered transaction; server retains the latest acknowledged section reference/sequence, not policy. On restart validate every window token/registration and reconcile renderer applied revision before new writes.

Retry unacknowledged requests with deduplicated request IDs. Offline fallback records fill unset state or become explicit new writes, never overwrite newer canonical state through stale snapshot replay. Broker loss invalidates timing immediately; retained pixels may remain during bounded recovery, but enhanced policy/previews stop accepting changes until reattachment. Backend loss invalidates image/completion generations.

Checkpoints are session recovery, not persistence across logon. Registry stores preferences only. Quotas bound state, bitmap and invalidation memory. Desktop teardown releases checkpoint/channel references.

## 6. DirectComposition and Chromium/Edge

Keep the working target pipeline. dcomp_target_create enforces same-process HWND ownership/top-bottom uniqueness and sets WINE_DCOMP_UNDERLAY_PROP. Slot properties contain process-local target pointers; never marshal them. dcomp_device_internal_commit validates/prepares targets, applies pending→active state and binds IWineDCompSwapChain scenes. WaitForCommitCompletion currently waits only for the device lock, not displayed pixels.

`wine/include/wine/dcomp_layer.h:wine_dcomp_target_scene` contains IUnknown *content and HWND: it is **not IPC**. Keep trees local until concrete foreign-resource requirements justify serialization. Shared resources need duplicated NT handles, adapter/format/alpha metadata, acquire/release GPU fences and generation cleanup, never raw COM/GL names or a CPU counter pretending to be a GPU fence.

The end-to-end path:

1. Commit the tree locally; optionally register target identity/producer stream with the broker.
2. Present content independently of Commit through `dxgi/swapchain.c:dcomp_binding_target_scene` and WineD3D. Publish completion after backend work, not after queueing.
3. `winex11.drv/init.c:client_surface_update_offscreen` attaches or individually redirects deeper client surfaces; X11DRV_client_surface_present blits the offscreen case. Outer compose.c:name_surface uses IncludeInferiors. Test actual pixels/damage for both paths rather than assuming that flag proves them.
4. Wayland uses driver client surfaces and existing wex_render_surface_v1 into wexcomp's scene; native hosted applications retain native/frame groups.
5. Outer compositor paints material below target content, then target alpha/frame/effects once. Source/completion subscriptions use matching target/size generations.

The underlay is the application's own GDI content. `win32u/window.c:invalidate_dcomp_underlay_dc` refreshes DC/window state on marker changes; win32u/dce.c and `winex11.drv/bitblt.c:enable_direct_drawing` keep it independent of the DComp drawable. `wined3d/swapchain.c:dcomp_capture_background` captures that application underlay and forces it opaque where required; it must not capture desktop materials recursively.

dcomp_background_for_window already omits the ordinary opaque underlay for WS_EX_LAYERED and **WS_EX_NOREDIRECTIONBITMAP**. Preserve this: no ordinary GDI redirection bitmap does not mean no DComp visual or no thumbnail source. Capture the final target/frame, not an invented opaque bitmap. See [Extended Window Styles](https://learn.microsoft.com/en-us/windows/win32/winmsg/extended-window-styles).

Backdrop attributes cannot make opaque producer pixels transparent. Negotiate alpha capability/mode, preserve premultiplication through offscreen/driver copies and keep ordinary GDI alpha at 255. X11 set_window_visual recreates the window: choose ARGB early for opted-in targets, transition late requests with a new generation and test context/swapchain reattachment. Do not recreate every top-level or automatically substitute WS_EX_LAYERED.

Rounded DComp clips already reach WineD3D's dcomp_render_pixel / dcomp_inside_corner. Complex scenes have a CPU reference path; dcomp_gpu_scene admits a narrow opaque GL subset. Improve alpha/efficiency there from probes instead of reimplementing all clips in device.c or deleting ordinary underlays.

Chromium/Edge tests cover the pinned real browser/GPU-process child HWND path, single Commit with continuing Presents, resize/DPI, menus, minimize/restore and final visible pixels. Separate builtin WineD3D/OpenGL, software controls and coherent optional DXVK; private builtin interfaces do not prove DXVK interoperability. Preserve non-DComp fallback and sandbox-enabled operation. API success/GPU-process existence is not visible or accelerated-composition acceptance.

## 7. Frame/effect responsibilities and XP invariance

**Wine paints frame content; the compositor paints effects.** Keep `win32u/defwnd.c:nc_paint`, hit testing, sizing, caption/button layout and tracking with uxtheme/window.c:UXTHEME_NonClientFrameDraw. Apps retain custom WM_NCCALCSIZE/non-client painting. Extend DwmDefWindowProc only for measured message behavior; no synchronous broker-owned caption input.

uxtheme paints text/glyphs and opaque/theme frame pixels using effective DWM policy. The compositor supplies exterior shadows, smooth clipping/border treatment and material underlays. Select one owner for the visible border. BORDER_COLOR_NONE does not automatically suppress shadows; NC-disabled, explicit app shape, maximized/fullscreen and popup rules need separate measurements.

Add alpha-capable frame surfaces before clearing pixels: GDI black fill is not transparent-alpha proof. Change current COLOR_BTNFACE filling only for opted-in material frames. nc_update_region removes only theme-owned binary shape, preserving app regions, WEX_WAYLAND_FRAME_OUTER_PROP and client-hole ownership. Align visual/input edges without breaking resize gutters.

`uxtheme/system.c:IsCompositionActive` consumes effective capability from the same source as dwmapi, not another registry read. A dedicated DWMWindow renderer is **not found**; generic theme-class loading does not imply a theme cannot define that class. Add support when open styles need it. Bounds queries must use the same geometry that paints/hit-tests, not universal three-button or 7-pixel-inset guesses.

Materials remain existing-compositor passes: wallpaper-derived cached Mica for main/tabbed windows, lower-window sampling/blur/tint for Aero glass and Acrylic. Respect alpha/margins/regions, activation, DPI, high contrast/reduced transparency. Invalidate Mica for wallpaper/output/scale/color policy; expand live-blur damage by sampling radius. No self-sampling, preview recursion or proprietary noise/reflection artwork.

Start with XRender masks/shadows/cached material or tint; gate live blur and later GL on measured capabilities/budgets. Equivalent semantics belong in wexcomp's renderer. No guaranteed 60 Hz or fixed GPU-generation floor without measurements.

**Composition=0 strictly bypasses new behavior:** no broker autostart, X selection/redirection, new ARGB selection, transparent clearing, NC metrics, rounding, shadows or previews. Classic/Open Luna paths/regions execute unchanged. Wayland's indispensable scene still runs with new DWM effects off. Standalone DComp presentation remains available. Compare default XP screenshots and hit tests exactly, including setup/OOBE.

Persist one master preference (`HKCU\Software\WineExplorer\Shell\Composition`, default 0). Standard DWM registry values may mirror preferences/colorization, never backend health. Broker publishes effective state/epoch and delegates desktop-scoped WM_DWMCOMPOSITIONCHANGED/colorization notifications. Measured Win11 DwmEnableComposition remains success/no-op; product settings are separate.

## 8. CRITIQUE of D5–D12

| Draft phase | Disposition |
| --- | --- |
| D5 state/property bridge | Split into validation, server channel, lifecycle, canonical state and backend application. Drop canonical WexDwm properties, FRAMECHANGED notifications and force-enabled headless reporting. Test access/death/restart before alpha/cloak. |
| D6 NC/shadows/corners | Split opaque colors/geometry from alpha/effects. Gate effective backend state, preserve app regions/Wayland holes; remove border-none→no-shadow coupling and universal radii/insets. |
| D7 backdrops | Move after alpha and DComp delivery. Cached Mica first; live blur/shell material separate. Registry/GPU tiers do not establish performance. |
| D8 thumbnails/iconic | Core thumbnails before cosmetic glass, after source lifetime. Broker owns relationships; renderer owns images. Drop destination X-property tables/raw bitmap-pixmap sharing. Retain before Unmap frees; minimized is not necessarily live. |
| D9 shell previews/Peek | After public thumbnails render and clean up. Split taskbar, Alt+Tab and Peek/settings. Use dwmapi/broker policy instead of shell-written XIDs/atoms. XP UI remains default. |
| D10 DComp | Move early alongside source/completion work. Repair actual WineD3D/driver gaps, preserve ordinary underlay, do not redo already-working rounded clips. |
| D11 GL/Flip3D | Separate acceleration and animation. Presentation completion precedes Flush/tear-free claims. Defer optional Flip3D until compatibility/previews/hardware budgets pass. |
| D12 themes/Wayland/installer | Split: Wayland contracts/old-peer work starts with transport; effect parity follows features. Open theme authoring can overlap. Full installer validation stays final. |

Specific code errors:

- compose_init_atoms, compose_set_enabled, compose_handle_event, compose_tick, ensure_window_picture, make_shadow_tile and draw_shadow are **not found** in compose.c. Use section 1's functions. windows is singly linked; stacking is queried. Shadows are six cached overlapping ARGB rectangles, not the claimed A8 nine-slice system.
- WexCompositionCaptureDesktop is **not found**. Existing `scripts/compose-probe.c` uses GDI capture; use BitBlt/GetDIBits plus final screen/QEMU captures, not an invented export.
- `user32/win.c:SetWindowCompositionAttribute` and its export exist as a failing stub; win32u.spec has a stub NtUserSetWindowCompositionAttribute. Implement validated ACCENT_POLICY separately; it is not interchangeable with SYSTEMBACKDROP_TYPE.
- Alt+Tab uses open_switcher, complete, free_list, layout and paint; close_switcher is **not found**. windows[] contains HWNDs, not .hwnd members. Taskband has show_group_list / taskband_proc, no thumbnail popup.
- dcomp_target_create now binds via IWineDCompSwapChain scenes during commit; the draft conflates it with legacy WM_WINE_DCOMP_SET_TARGET handling. Keep the current interface path.
- Extend existing `scripts/build_msstyles.py:build_msstyles` after auditing format/alpha needs rather than immediately adding tools/mkmsstyles.py. Aero/Flat/Fluent open packs still need authoring; v4 loading alone does not supply them.
- `scripts/test-installer.sh` is **not found**. Use `installer/test-vm.ps1` / test-vm.py and `installer/tests/install-interactive.sh` / install-unattended.sh with actual lab configuration.
- Win11 probes do not measure earlier eras or native composition-off/security semantics. Name the OS/build for each number; label unmeasured approximation.

## 9. Quick-iteration implementation roadmap

These are future assignments, not agent dispatches. **Qwen:** scoped coding from exact structure/function specs, no shell. **Antigravity:** larger bounded tasks, protocol/design and tests, parallel work after interfaces agree. **Claude:** integration/review, guest deployment/tests and fixes.

Every phase inherits:

- **N:** extend relevant Wine C/wine-test conformance tests; build x86/x64 PE tests and run the same public-API executable on Windows 11 and Wine. Separate project-private IPC/XP/security policy tests. Record executed/skipped counts; off-mode skips do not establish on-mode acceptance. Reuse dwmapi tests; add a proposed `wine/dlls/dcomp/tests` target (currently **not found**) for public DComp semantics instead of presenting project C++ probes alone as conformance.
- **V:** changed-binary overlay in an isolated QEMU guest; original visual probes with final pixel/input inspection, composition off/on, absent/crashed broker and a 32-bit app. Preserve evidence/backend identity.
- **I0:** prepared installed-guest smoke after each phase: Setup/OOBE named desktops accept mouse/keyboard, Classic/Luna remain correct; include handoff when changed. No fresh ISO per coding step.
- **I1:** after phases work together, full WoW64 build, assembly/package verification and fresh unattended/interactive installer tests with BIOS/UEFI coverage, OOBE, logon/lock/consent, XP default and optional themes.

Retain a known-good combined-WoW64 build/runtime. Rebuild changed DLL/EXE targets and actual dependencies (e.g. `make dlls/dwmapi/all`), inspecting generated targets/layout for both PE architectures and required Unix modules. Hash baseline/overlay; deploy only changed artifacts to a private guest runtime, restart affected test processes and verify loaded hashes. Do not overwrite mapped live modules. Chromium's hash-verified runtime overlays are the precedent; `scripts/g4_combined_r6_deploy.py` is not automatically a generic DWM deployer.

Protocol changes are the exception: deploy matching wineserver, generated-protocol clients and affected Unix runtime together; restart only the isolated prefix/server. If protocol numbering invalidates all clients, rebuild that dependency set once, not a falsely DLL-only overlay. Stabilize the envelope early so later phases rebuild changed binaries only.

Add `src/programs/dwm/Makefile.in`, then run `scripts/assemble.sh` / assemble.py and configure once to register/symlink the new program. Existing programs such as `src/programs/logonui` show MODULE/IMPORTS/SOURCES conventions. assemble.py scans src/dlls and src/programs for Makefile.in, registers WINE_CONFIG_MAKEFILE entries and headers, and links project sources; assemble.sh regenerates configure with autoconf. Assembly rejects dirty undeclared stock replacements; inspect any selected-base dwm destination and declare a replacement only if needed. Do not run assembly as part of this document-only task.

Switches below are **proposed** unless marked existing. Under `HKCU\Software\WineExplorer\Shell` add development gates DwmService, DwmBackendState, DwmFrameClock, DwmAlpha, DwmThumbnails, DwmFrameEffects, DwmMaterials and UI gates, initially off. Existing Composition=0 is master rollback. Disabling invalidates feature leases/serials and restores old drawing; schema/binary rollback restores the matching saved overlay.

| Phase / small deliverable | Files and changed binaries | Who | Acceptance beyond N/V/I0 | Rollback |
| --- | --- | --- | --- | --- |
| P0: exact baseline/spec | `wine/dlls/dwmapi/tests/dwmapi.c`; `scripts/dwmapi-probe.c` and existing four dwm measurement probes | Antigravity tests/design; Claude working-tree review | Native HRESULT/size/default matrix, x86/x64; XP screenshots; distinguish stubs from support. | Retained baseline. |
| P1: authenticated channel, no renderer | New server/client/header files; `server/{protocol.def,Makefile.in,user.h,window.c,winstation.c}`; generated protocol clients | Antigravity protocol/negative tests; Claude integration | x86→x64 echo; spoofed owner/desktop, thread desktop change, HWND reuse/death, secure source, queue overflow; public dwmapi unchanged. | DwmService=0; matching server/client set. |
| P2: broker lifecycle/package | New `src/programs/dwm/{Makefile.in,dwm.c}`; small `explorer/desktop.c:manage_desktop` hook; assembly/install lists | Qwen state machine from spec; Claude integration; Antigravity tests | Two desktops/two brokers; duplicate prevention; missing EXE, crash loop, teardown, Explorer restart; none on Winlogon. I0 no-shell Setup/OOBE. | DwmService=0; previous explorer.exe. |
| P3: scalar canonical state/recovery | Broker state/checkpoint code; dwmapi helper/main; server checkpoint/death glue | Qwen validators/storage; Claude integration; Antigravity cross-process tests | N read/write/default/size rules; A sets/B reads; setter exit, kill before/after ack, replay conflicts, quota/corruption and reused HWND. | DwmService=0; fallback tables. |
| P4: X11 binding/state | `winex11.drv/window.c`; wexhost PE/Unix declarations, `wm.c,x11wm.c,compose.c`; `wexshell/desktop.c` bridge | Antigravity backend; Claude lock review/fixes | N attrs; V revisioned visible change, stale XID/visual recreation, enable/disable, second-XCM refusal, secure-entry suppression, idle CPU; no compositor in dwm process. | DwmBackendState=0; prior composition path. |
| P5: Wayland channel/capabilities | wexcomp XML / `src/protocols/wex-window.c`; synchronized wexhost/driver XML and bridges | Antigravity independent task/tests; Claude integration | N attrs on Wayland; old peers/stock labwc fallback, native lifetimes/frame holes/focus/resize and unprivileged-socket denial. | New capabilities off; shipping X11. |
| P6: real completion/Flush | `compose.c,x11wm.c` output glue; `wexcomp/src/output.c`/feedback; `dxgi/swapchain.c,wined3d/swapchain.c`; broker/client clock | Antigravity design/tests; Claude integration/fixes | N timing/non-alertable APC Flush; delayed producers, no damage, occlusion/minimize/multiple outputs/bypass and backend loss; actual latency/timestamps. | DwmFrameClock=0; labelled legacy estimate. |
| P7a: margins/blur/alpha negotiation | dwmapi/broker state; `winex11.drv/window.c,init.c`; `winewayland.drv/window.c,window_surface.c` as needed | Qwen serialization; Antigravity drivers/tests; Claude review | N margin/blur validation; V late ARGB transition, GDI alpha, resize/remap/context recovery, x86 regions and off-mode baseline. | DwmAlpha=0. |
| P7b: transparent DComp target | `dcomp/device.c`, `dxgi/swapchain.c`, `wined3d/swapchain.c`; DC/driver code only as probes require | Antigravity surface task/tests; Claude fixes | N DComp retained-tree/alpha; extend `scripts/test_g2_dcomp_probe.cpp` / `test_g4_composition_probe.cpp`: single Commit then changing Presents, GDI/top-bottom order, alpha, NOREDIRECTIONBITMAP, child HWND, detach/device loss; absent dwm works. | DwmAlpha=0 / coherent graphics overlay. |
| P8: live public thumbnails | Broker registry, dwmapi thumbnail calls; compose.c source cache/pass; wexcomp group-source pass | Qwen bounded registry; Antigravity rendering/tests; Claude integration | N ownership/top-level/handles/source size; V animated foreign source, clipping/opacity/client-only, minimize retention/restore, death/eviction and no recursion. | DwmThumbnails=0. |
| P9: iconic pixels/requests | dwmapi iconic calls; broker copy/messages; renderer image import/release | Qwen checked bitmap serialization; Claude integration; Antigravity tests | N iconic HRESULT/message/dimensions; immediate bitmap delete, repeated invalidation, hung app, minimize/restore, secure denial, memory budget. | DwmThumbnails=0; icons. |
| P10a: opaque modern NC | `uxtheme/window.c,system.c`; `win32u/defwnd.c` only as needed; dwmapi bounds/DefWindowProc; broker notifications | Qwen layout/colors; Claude integration; Antigravity tests | N geometry/policies/custom NC; V DPI 96/144/192, tool/maximized/button combinations, dark/custom colors, disabled NC; XP exact baseline. | DwmFrameEffects=0. |
| P10b: alpha frame/exterior effects | uxtheme alpha path, compose.c masks/shadows, equivalent wexcomp effects | Qwen mask math; Antigravity backend/tests; Claude integration | N corners/colors; V transparent margins, input/gutters, explicit regions, maximize, native SSD/CSD, single border/shadow ownership; XP baseline. | DwmFrameEffects=0 and DwmAlpha=0. |
| P11a taskbar; P11b Alt+Tab; P11c Peek | Separate changes to `taskband.c`, `alttab.c`, then `desktop.c/taskbar_props.c` and broker Peek policy | Qwen scoped layouts/settings; Claude input/preview integration; Antigravity tests | Each reruns N thumbnail/iconic tests; V hover/cancel/close/focus, groups, native apps, source death. Preserve complete's deferred switching and test Peek/cloak eligibility. | Per-UI gates off; XP menu/grid. |
| P12a cached Mica; P12b live glass/Acrylic | Separate broker material policy, compose.c/wexcomp passes; `user32/win.c` ACCENT_POLICY adapter for P12b | Qwen filter/cache helpers; Antigravity capability/tests; Claude integration | N backdrop/ACCENT separately; V wallpaper-vs-window sampling, high contrast/transparency, dirty expansion/tint fallback; moving checkerboard and budgets. | DwmMaterials=0; opaque fallback. |
| P13: open styles, one era per step | `scripts/build_msstyles.py`, original/free theme sources/resources, `src/dlls/themeui/theme_engine.c:theme_apply` and `src/include/wextheme.h`, packaging | Qwen packager/metric tables; Antigravity visual tests; Claude integration | N uxtheme loader/metrics; V Basic→Aero→flat→Fluent, license provenance, live theme switch/regions/alpha, missing-art fallback; I1 after integration. | Classic/Open Luna; Composition=0. |
| P14: measured acceleration | New `wexhost/compose_gl.c`/build glue; wexcomp optimization; optional Flip3D as later separate step | Antigravity GPU task/tests; Claude review/fixes; Qwen isolated shader math | N graphics/timing; V XRender/GL pixels/stack/damage, physical older Intel OpenGL and AMD/Nvidia, GPU loss/fallback; efficient path avoids compulsory full-frame readback. | Force XRender/effect fallback; Flip3D off. |
| P15: release integration | Matching runtime, assembly/package manifests, installer scripts, stock-change ledger/evidence | Claude integration/review/fixes; Antigravity full matrix | N x86/x64 on/off; V actual Chromium/Edge sandbox-on, DComp/native apps X11/Wayland; I1 fresh install/upgrade/rollback, Setup/OOBE input, lock/consent/recovery. | Known-good runtime/image; Composition=0. |

P4/P5 can proceed independently after P1–P3 contracts and merge after Claude review. P7/P8 need no finished theme art. P10a/P13 authoring may overlap after geometry policy. Live blur/GL never block API/source correctness. Each subphase gets its own binary overlay and evidence.

Within P1, P3 and P6 use separate reviewable slices: channel connect/echo before lifetime validation; one scalar attribute before recovery checkpoints; X11 submission diagnostics before presentation completion, then Wayland feedback. Do not activate broader features until their gate passes. Rebuild/deploy after each slice rather than waiting for a phase-wide multi-subsystem patch.

Acceptance must distinguish API compatibility, visible software composition, real pacing, efficient GPU delivery, theme era, backend and hardware. Full Vista–11 app support is the objective, not inferred from these first slices: missing DComp interfaces/sharing, HDR/protected/video paths and consumer failures remain explicit follow-ups.

## 10. Risks and owner decisions

1. **Server scope:** channel/lifetime/checkpoint plumbing is a deliberate stock delta. Keep it isolated, review protocol first and list each future Wine edit in STOCK_CHANGES. Mixed protocol deployment is a hard failure.
2. **Security:** preserve Wine desktop boundaries without claiming hostile-code isolation from same-user X11/current Wine. secure.c already documents native X11 limits. Wayland secure/capture isolation needs its own proof.
3. **Alpha/identity:** late visual recreation, GDI invalidation, copies and browser child surfaces can lose alpha/damage or contexts. These are early blockers, not theme-polish issues.
4. **Recovery/memory:** checkpoints, last frames and churn require quotas/stress tests; bounded icon/blank fallback beats retaining unlimited images or stale freed resources.
5. **Fidelity/licensing:** Win11 numbers cannot establish earlier eras. Record measurements, never copy/trace proprietary art/theme files/logos. Original/free assets need notices; retain source licenses and GPL headers.
6. **Hardware:** QEMU proves function, not physical acceleration. Inventory older Intel/OpenGL and representative low-end AMD/Nvidia before promising minimum hardware or 60 Hz.
7. **Default decisions:** use measured modern visible/invisible frame geometry, keep XP/off default, prioritize DComp/previews/open Aero/Fluent before Flip3D. Per-app reporting exceptions need a named failing app and isolated acceptance.
8. **Owner choices:** select maintained Chromium/Edge versions and authorized Vista/7/8/10 reference environments/theme-era priorities. Proposed art order: Basic/Aero, flat 8/10, Fluent 11. Win11 is today's API oracle; label unmeasured differences.
9. **Lifecycle:** this plan chooses per-desktop brokers with secure effects initially off. One privileged cross-desktop broker or animated secure consent changes the trust design and warrants owner review.


## 11. Status (2026-11-02)

- P1 slice 1 (control channel) and P2 (broker lifecycle) are merged. `DwmService` (HKCU `Software\WineExplorer\Shell`, default off) makes the desktop manager start `dwm.exe`, which registers as broker and echoes. Tested by `scripts/dwm-p2-lifecycle-test.sh` (gate off, two desktops, restart after kill, crash-loop give-up, no orphan after explorer is killed) and `dlls/dwmapi/tests/dwm_channel.c`.
- Test note: a plain `/desktop=NAME` desktop (not a shell desktop) is closed by the server one second after its only remaining users are the manager's own threads (`remove_desktop_user`), so lifecycle tests must use a shell desktop (`HKCU\Software\Wine\Explorer\Desktops\NAME` `EnableShell`=1), as the real session does.
- P3: scalar attributes live in the broker (cross-process get/set, sweep of destroyed windows). Not done: checkpoint sections for a setter that exited before a broker restart.
- P4 (X11): `DWMWA_WINDOW_CORNER_PREFERENCE` rounds window corners in the compositor through the shell's bridge (`src/dlls/wexshell/dwm_bridge.c`); screenshot-verified.
- P5 (Wayland, compositor side): `wex_window_v1.set_corner_radius` (v12) in wexcomp, 62/62 wexcomp tests and screenshots. Not done: the Wine side (`docs/DWM_P5_WAYLAND.md`); the request is owner-only, so each application's own winewayland connection must send it.
- P6 slice 1: per-desktop frame clock page; `DwmFlush` waits for the next composed frame and timing info reports composed frames (composition, not display). Not done: X Present/Wayland presentation feedback, vblank, DXGI/WineD3D producer serials.
- P8 slice 1 (X11): live thumbnails (broker registry keyed by sender process, compositor scaling); minimized sources keep their last picture. Not done: `fSourceClientAreaOnly`, Wayland.
- P11a/b/c: taskbar hover previews (`TaskbarThumbnails`), Alt+Tab preview (`AltTabThumbnails`), Peek from the preview (`TaskbarPeek`); all default off; screenshot-verified against the XP defaults.
- P7a (X11): `DwmExtendFrameIntoClientArea` glass margins (`DwmAlpha`): ARGB surfaces, `_WEX_DWM_MARGINS`, the compositor blends the margins over what is below. P10a: opaque modern frames (`DwmFrameEffects`). Both screenshot-verified.
- P12a (X11): cached Mica (`DwmMaterials`, default off; also off under high contrast or `EnableTransparency`=0). `DWMWA_SYSTEMBACKDROP_TYPE` 2/4 or `DWMWA_MICA_EFFECT` reach the compositor through the bridge as `_WEX_DWM_BACKDROP`; under the glass the compositor paints the wallpaper (the shell's desktop window, never other windows) box-filtered by five halvings and scaled back up, then a light or dark (`DWMWA_USE_IMMERSIVE_DARK_MODE`) tint. Rebuilt when the wallpaper window is damaged or resized. Screenshot-verified with `scripts/dwm-mica-visual-probe.c`. Not done: inactive-window tint, Wayland.
- P10b slice 1 (X11): glass frames when `DwmAlpha` and `DwmFrameEffects` are on: the caption and borders are part of the glass sheet, so Mica (or plain glass) shows behind the title, alone or merged with extended margins; uxtheme writes per-pixel alpha from a black and a white pass. Screenshot-verified with `scripts/dwm-glassframe-visual-probe.c`. Slice 2: Windows 10/11 geometry for glass frames: invisible resize bands left, right and below (still resizing), caption buttons and hit testing inset to match, `DWMWA_EXTENDED_FRAME_BOUNDS` = the visible frame, which the compositor rounds (8 px, `DWMWCP_DONOTROUND` square) and shadows. Not done: DPI-scaled shadow, inactive-window shadow difference.
- P12b slice 1 (X11): live blur and Acrylic under `DwmMaterials`. `DwmEnableBlurBehindWindow` and `SetWindowCompositionAttribute(WCA_ACCENT_POLICY)` states 3/4 set the private broker attribute `DWM_ATTR_WINE_BLUR` through `DwmSetWindowAttribute` (with `DwmAlpha`, a window without margins becomes a glass sheet); the bridge maps it, and `DWMWA_SYSTEMBACKDROP_TYPE` 3, to `WEX_BACKDROP_BLUR` / `WEX_BACKDROP_ACRYLIC`. The compositor copies what is already composed below the window, halves it 3 (blur) or 5 (Acrylic) times and scales it back under the glass; Acrylic adds a light or dark tint. Any damage that touches a blurred window (plus the 32 px filter reach) repaints all of it, so it never samples itself. Screenshot-verified with `scripts/dwm-blur-visual-probe.c` (moving stripes under four windows).
- P12b slice 2: `GradientColor` (private `DWM_ATTR_WINE_TINT`) tints Acrylic, the blur (accent flag 2) and the gradient accent states 1/2 (`WEX_BACKDROP_GRADIENT`: the tint alone, opaque for state 1); `hRgnBlur` narrows the blur to the region's bounding box (`DWM_ATTR_WINE_RGN_LT`/`_RB`, `WEX_BACKDROP_REGION`). The backdrop property carries backdrop, tint and region. Probe set 2 (`dwm-blur-visual-probe.exe 2`) measured exact blends. Not done: the luminosity layer and noise, regions of several rectangles, inactive-window fallback, budgets.
- P9 slice 1: iconic thumbnails. `DwmSetIconicThumbnail` (window with `DWMWA_FORCE_ICONIC_REPRESENTATION`) copies the bitmap as top-down BGRA into a per-window section (`DWM_ICONIC_NAME`, header + up to 1024x1024, `include/wine/dwm_protocol.h`) before it returns. For a window whose broker-held `DWMWA_HAS_ICONIC_BITMAP` is set (`WexDwmpGetAttr`), the taskbar preview and the Alt+Tab preview send `WM_DWMSENDICONICTHUMBNAIL` (lParam: width high, height low; 1 s, abort if hung), copy the section out (header re-checked after the copy, so a picture rewritten meanwhile is dropped) and draw it with `GdiAlphaBlend` instead of the live thumbnail; no picture: the live thumbnail as before. Slice 2: `DwmSetIconicLivePreviewBitmap` is stored the same way (`DWM_ICONIC_LIVE_NAME`, with the client offset and the window's restored rectangle, which another process cannot read while it is minimized); Peek sends `WM_DWMSENDICONICLIVEPREVIEWBITMAP` and shows the picture in a shaped shell popup at the window's place, so a minimized window can be peeked. `DWM_SIT_DISPLAYFRAME` draws a raised border and the window's caption around the picture (thumbnail, Alt+Tab, Peek). `DwmInvalidateIconicBitmaps` clears the stored pictures. Sections are sized to the picture (up to 4096x4096). Screenshot-verified with `scripts/dwm-iconic-visual-probe.c` (minimized iconic window: framed thumbnail, Alt+Tab, Peek). Not done: Wayland (P5).
- P7b slice 1 (X11, GL renderer): transparent DComp targets under `DwmAlpha`. `dcomp_target_create` makes a top-level `WS_EX_NOREDIRECTIONBITMAP` target a glass sheet through `DwmExtendFrameIntoClientArea` (so the gate decides); winex11 keeps the GL client window of a window with `__wine_dwm_alpha` offscreen (it is opaque); wined3d presents such a target's CPU composition, premultiplied, through GDI to the window's ARGB surface instead of the GL drawable (only without an underlay, which would read the output back). Screenshot- and pixel-verified with `scripts/dwm-dcomp-alpha-visual-probe.cpp` (50 % red, clear and opaque areas over stripes); gate off unchanged. Not done: the GPU path (CPU composition per frame; P14), child-window targets (Chromium's), the Vulkan renderer, Wayland.
- Not started: P13, P14, P15.
- Visual test harness note: run the shell as `explorer /desktop=Default,WxH /shell` (composition needs the virtual desktop window; the shell's keyboard hook installs only on the desktop named Default).
