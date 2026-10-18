<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Implementation plan: Chromium graphics on WineExplorer

Date: 2026-10-18. Status: researched plan, not implementation or acceptance.
Evidence, accuracy corrections, pinned source map and licensing:
`CHROMIUM_GRAPHICS_RESEARCH.md`. Existing lab: `WEBVIEW2.md`.
Revision: integrates the existing DWM/composition roadmap and the project's
older Intel iGPU / low-end AMD and Nvidia hardware priorities. This is a policy
and architecture update, not evidence of new device support.

## 1. Deliverable and boundaries

Support current selected Windows Chromium-derived applications with correct,
continuous on-screen presentation, input and resize, and prove when rendering
and composition use a physical GPU. Deliver progressively:

1. A verified non-DComp accelerated path where the selected runtime supports it.
2. A real Wine DirectComposition/DXGI path with basic retained-tree presentation.
3. Resource sharing, richer composition and optional modern GPU/media features.
4. Integration with existing desktop composition, the token framework, packaging
   and installed-image acceptance.

OpenGL-backed hardware acceleration is a first-class required target, not an
emergency fallback after Vulkan work. Reuse Linux drivers and existing WineD3D/
DXVK machinery; our work supplies missing Windows semantics and efficient surface
delivery. Do not make modern Vulkan, D3D12 or WebGPU a desktop/browser baseline.
Vulkan is an optional enhancement when the actual GPU/driver meets the selected
backend's full requirements. Software presentation remains available below that.

"Intel HD Graphics" and "low-end" do not define a capability floor. In G0 select
representative PCI IDs/generations and driver versions; publish the supported
minimum only after testing. Some old hardware cannot satisfy a maintained
browser's minimum GPU path. State that honestly rather than promising every
Intel HD model will accelerate or downgrading to an obsolete browser by default.

Successful software fallback is a supported compatibility result, not hardware
acceleration. GPU-rendered frames read back for CPU composition are a distinct
intermediate result, not the final efficient GPU path. Basic UI/Canvas/WebGL,
WebGPU and hardware video decoding have separate acceptance labels.

This extends the application-facing side of the existing DWM/composition program,
not a competing XP shell compositor or full DWM clone. Keep Classic/Open Luna
unchanged and preserve the off-by-default X11 desktop-effects policy unless
separately approved. Preserve Wine as the runtime;
do not migrate the desktop wholesale to Proton/GE. No custom Linux kernel is
planned. Native Linux browsers can be controls, not substitutes for this result.

## 2. Branch, ownership and prerequisite decisions

Implement on a separate `codex/chromium-graphics` branch (or `gemini/chromium-graphics`
if Gemini is assigned) from the current reviewed master plus these documents.
Do not pull in unrelated unreviewed OOBE WIP merely because the documents were
published on its handoff branch. Preserve occupied/dirty checkouts; use a separate
worktree when needed. Record parent SHA, Wine submodule SHA and every dependency
pin before editing. Read `CLAUDE.md`, this plan/research, `WEBVIEW2.md`,
`FONTS_RUNTIMES.md`, `STOCK_CHANGES.md`, `WINLOGON_AND_DWM.md`,
`D2_TEST_REPORT.md`, `WAYLAND_L1.md`, `WAYLAND_L2_WORKORDER.md`,
`WAYLAND_SESSION.md`, and `TOKEN_SECURITY_WORKORDER.md`.

The fonts/runtimes repair is not automatically a reviewed release base. Coordinate
per-user graphics packaging changes with its owner; upstream Wine changes belong
in the Wine submodule, with the parent gitlink updated after tested commits.
Keep small attributed patches, notices and source/license provenance; no whole-fork
merge. New project code follows repository licensing, existing LGPL files retain
their licensing. GPLv3 sources remain design-only without separate approval.

This plan authorizes no implementation by itself, no task dispatch, no master
merge, and no destructive VM changes. On implementation assignment, publish
reviewable implementation branches and evidence, not unreviewed master updates.

## 3. Architecture decisions

### One composition program, distinct layers and established owners

The earlier D1/D2 work is the outer desktop-composition layer: layered-window
alpha, shadows, blend effects and capture. D3 plans thumbnails/presentation;
D4 plans optional Windows `dwmapi` compatibility. Chromium's DComp layer combines
application surfaces inside a window before that window enters desktop composition.
Enabling desktop composition does not implement `dcomp.dll`, and implementing
`dcomp.dll` must not require every application to run inside Explorer.

| Layer | Responsibility | Existing owner / integration point |
|---|---|---|
| Windows graphics APIs | D3D/DXGI resources, DComp tree, Windows handles and applicable DWM API semantics | Wine DLLs, WineD3D or coherent DXVK backend; Wine server for object/handle rules |
| Window/surface delivery | HWND geometry, child ownership, damage and rendering-surface lifetime | Wine win32u, winex11/winewayland and WineVulkan/backend integration |
| Desktop composition | Stack complete windows, apply desktop effects, present/capture the desktop | X11: existing `src/dlls/wexhost/compose.c` with WM integration; Wayland: wexcomp |
| Shell behavior | Frames, taskbar, optional previews, settings and XP appearance | Explorer/wexshell plus wexhost bridge, not a second compositor |

Do not introduce `dwm.exe`, another X Composite selection owner or a second
Wayland scene compositor. On Wayland retain wexcomp's composition/native-surface/
physical-input ownership and the existing native-frame grouping contract. On X11
retain wexhost's event/locking model and protect Wine's already redirected GL/
Vulkan child surfaces from double redirection. The standalone Wine DComp API path
must still work outside our shell, using its driver/backend capabilities.

### Share contracts and useful backend facilities, not competing frame loops

Coordinate D3 presentation work and G2-G4 content delivery around common contracts:
image/surface ownership, alpha/color space, damage, geometry/scale, completion,
occlusion, buffer release, device loss and capture. Reuse existing Wine backend
facilities and wexcomp protocols where they fit. Extract a small reusable helper
only after identifying a genuine shared implementation; do not force X11 and
Wayland into one platform-specific renderer or duplicate token authorization.

Each layer can have necessary scheduling, but avoid redundant full-frame copies,
two throttles for one producer, periodic repaint without damage, or waiting on
the desktop compositor while holding a Wine graphics/event lock. Define capture
at the desktop layer so screenshots/thumbnails include the final composed image
without feeding that capture back into the application's rendering path.

Keep D4 optional and scoped. Implement/report `DwmIsCompositionEnabled`, thumbnail
and `DwmFlush` semantics only for the mode actually supported; never enable broad
Vista/glass behavior just to satisfy Chromium initialization. Document how DComp
application availability relates to desktop-effects settings rather than using
one misleading global "composition supported" boolean.

### Two presentation paths, one traceable capability policy

Path A retains Chromium/ANGLE's ordinary HWND/EGL presentation where available.
Fix Wine's D3D11/device/swapchain/window delivery without claiming DComp support.
Path B adds DComp composition swapchains and retained visual targets.
Neither path is selected merely because a flag was requested: capture actual
Windows child command lines, loaded DLLs, selected ANGLE/Skia backend and traces.

Start Path A and the DComp proof on an internally coherent OpenGL-backed
WineD3D/builtin-DXGI stack, because of the target hardware and the strongest
reusable DComp fork's dependencies. This is not a promise that WineD3D is fastest.
In parallel, prove accelerated Path A using a coherent native DXVK D3D11/DXGI
stack on devices that meet that version's requirements.
Native DXVK and builtin WineD3D are not interchangeable COM/private structures.
Do not replace a working OpenGL route with mandatory Vulkan. Select by tested
features and stability, not marketing name, highest API version or vendor alone.
XRender is an interface, not proof that rendering is CPU-only or GPU-backed;
classify the actual driver/path by measurement.

### DComp/DXGI content bridge

Specify a small explicit producer/consumer interface before implementing Path B:

- Producer: composition swapchain or drawable surface, independent of an HWND.
- Frame: image identity, dimensions, format, color/alpha semantics, damage and
  synchronization ownership; no raw process-local COM pointer in IPC.
- Consumer: DComp committed tree and target, ultimately presented via Wine's
  driver/window surface path on both X11 and Wayland.
- Lifecycle: reference retention, buffer acquisition/release, resize generation,
  device loss, target detach/reattach, process death and failed commit.

Decide whether the initial coherent backend can stay within one process; where
images must cross processes, use Wine handle/IPC conventions with real lifetime
and access semantics. Do not implement token policy independently inside the
graphics bridge. Match the token project's handle duplication/access checks.
Normal GPU-process-owned child windows parented into browser windows must work;
do not make arbitrary foreign-process DComp targeting succeed contrary to API rules.

Preserve the distinction between publishing visual state (`Commit`) and publishing
new content (`Present`/surface completion). A static committed tree must show
subsequent frames. Do not drive every frame from an unconditional 16 ms polling
timer. Use producer notification/completion and display pacing, sleep when idle,
and measure any temporary timer/readback fallback explicitly.

### GPU resource sharing and synchronization

Define NT-handle duplication, cross-device/process import, keyed-mutex ownership,
fence timelines, CPU wait events, timeout and cleanup contracts. Use real GPU
completion when resources are consumed elsewhere. A CPU counter after command
submission or a plain event standing in for an image/fence is not sufficient.
For OpenGL/EGL, validate texture/image sharing and sync support, context ownership
and cross-process transport on the actual drivers. A GL name is not a cross-process
resource handle. Select safe supported paths rather than assuming Vulkan-style
external-memory features exist on every old Intel iGPU. For Vulkan, validate
compatible external-memory/semaphore handle types, adapter,
format/modifier, memory ownership and import/export support. DMA-BUF support in
Wayland window delivery is not automatically D3D shared-NT-handle support.

Advertise only capabilities backed by tested behavior. Unsupported overlays,
HDR, protected content and advanced composition textures stay off. Do not expose
Device4/6 or pretend hardware decode exists to push initialization further.

## 4. Phases, implementation tasks and exit gates

### G0. Reproducible baseline and hardware inventory

Extend existing `scripts/webview2-probe.c`, `run-webview2-lab.sh` and analyzer
instead of throwing away their provenance/late-frame checks. Add a separate
Chromium graphics runner where an embedded host is not appropriate. Build small
original 32/64-bit PE probes for D3D11/ordinary swapchain, DComp and sharing.
Keep source/test pages open and reproducible; compile shaders from open sources
or original fixtures, not copied proprietary DLLs/blobs.

For each run record runtime/executable hash/version, matching Chromium revision
and DEPS, Wine build, DLL load origins/overrides, GPU adapter/vendor/device IDs,
driver, Vulkan/OpenGL capabilities, display backend, prefix and all flags. Pin
one maintained Chromium build first, then a representative Electron and CEF host,
and the existing WebView2 host/runtime. Do not assume one validates the others.

Establish a modern Windows positive control for the same probes where available;
XP cannot validate DComp. Ask before acquiring/reconfiguring another VM. Confirm
basic native Linux GPU operation as an environmental control if available.

Classify test hardware: VirtualBox SVGA, CPU Vulkan/GL (lavapipe/llvmpipe), virtual
GPU acceleration and real physical GPU are distinct. The existing lab documents
a VirtualBox GL ceiling; do not infer it was solved by moving disks or cold boot.
Query available devices afresh. If no real GPU is available, continue functional
work but keep hardware-acceleration acceptance explicitly blocked.

Create an explicit hardware/backend table: older Intel HD iGPU with usable
OpenGL but no supported selected Vulkan path; Intel with both; representative
low-end AMD and Nvidia with the drivers we intend to ship/support; and software/
VM controls. Record available versus tested devices, missing extensions, RAM/
shared-memory pressure and supported resolutions. Do not infer a whole vendor's
support from one new GPU. Lack of a representative physical device leaves that
hardware acceptance cell pending, not the whole project's independent work.

Capture D1/D2 and current Wayland baselines before graphics changes. Inventory
which D3/D4 items are actually implemented now; historical plan text is not current
acceptance. Record the desktop composition owner/settings separately from the
application API and graphics backend.

Exit: reproducible failures and successful controls, exact selected runtime/source
matrix, capability/first-failure table and original logs. No development based
only on a white window or successful device creation.

### G1. Accelerated non-DComp path first

Compare default launch with DComp disabled on the selected current runtime,
without global Windows-version spoofing. Test its default ANGLE D3D11 selection;
compare another supported backend only if the exact runtime accepts it. Follow
`image_transport_surface_win.cc`, native EGL surface creation and actual Viz
output selection, not the obsolete child-surface filename from the pasted map.

First prove builtin WineD3D/OpenGL/DXGI on the older-GPU target. Compare packaged
DXVK 2.4.1 where supported using isolated per-app/prefix overrides; keep
D3D11/DXGI/D3D10Core coherent. Trace device creation, accepted
feature level, swapchain creation/present, browser/GPU child HWND ownership and
visible frame delivery. Add only the fixes demonstrated by minimal probes.
Test X11 with outer composition off and on; on Wayland test the actual wexcomp
session. Distinguish application rendering failure from outer-window delivery.

For Wayland, evaluate GE/Wine-Wineland child-surface patches by reproducer. Build
a dependency list and check what current Wine already contains. Rebase the smallest
correct subset; preserve authors/Source trailers. Independently test X11 rather
than assuming a Wayland fix applies there. No duplicate SNI or whole Proton stack.

Exit: continuous visible UI, Canvas and WebGL on the selected maintained runtime,
correct input/scroll/resize/minimize/restore, with actual adapter/backend evidence.
If only software works, record that result and continue diagnosis. A supported
per-app DComp-off compatibility mode can ship only after integrated acceptance;
it is not permission to make obsolete Windows versions the global default.
The required OpenGL-target acceptance cannot be replaced with a modern-GPU
Vulkan pass. Missing optional Vulkan support must not disable the working path.

### G2. Minimal, real DirectComposition vertical slice

Evaluate `giang17/wine` in an isolated comparison build against G0/G1 probes.
Inventory its DComp/DXGI/WineD3D/driver dependencies and tests. Cherry-pick/adapt
reviewed pieces or implement independently where dependencies are excessive.
Document what was reused, rejected and why. Do not bring unrelated modules along.

Implement device creation/QI/refcounting needed by the selected runtime; correct
DesktopDevice target ownership, root visual/content, buffer-backed composition
swapchain and functional Present-to-target delivery through the existing Wine
window layer and outer compositor. Include first frame,
unattached chain, resize, detach, device loss and destroy ordering. Return correct
unsupported results for unimplemented interfaces/features.

Create a deterministic two-color animated probe. Commit the tree once, then
Present changing frames without committing it again: the target must update.
Also test pending tree edits before/after Commit and GPU/CPU completion ordering.
Verify pixel output in the target window, not just intermediate textures or CDP
screenshots. If initial presentation uses readback, label it explicitly.
Test alpha/clip/z-order at both composition layers and ensure ordinary applications
still present when X11 desktop effects are disabled. Do not add a second desktop
overlay to make this application-level probe visible.

Exit: 32/64 probes render and resize correctly, retained-tree/content semantics
pass, no successful no-op COM methods masking missing output. This phase alone
does not establish efficient hardware composition or full Chromium compatibility.

### G3. Required SharedImage resources and synchronization

Trace and implement the selected runtime's actual missing `CreateSharedHandle`,
`OpenSharedResource1`, keyed mutex and shared-fence operations. Start with the
minimal required formats and D3D11 path. Coordinate Wine server/ntdll/win32u
handle work with token development; support duplicate/close and process exit.

Use DXVK resource/fence code as a permissively licensed candidate for a Vulkan
backend, with its Wine interop dependencies understood. WineD3D needs corresponding
behavior or honest capability rejection. The reference fork's process-wide/CPU
fence approach must not be promoted to cross-process GPU correctness without proof.

Tests: same-device, cross-device and cross-process producer/consumer patterns,
deliberately delayed GPU writes, key mismatch/timeout, shared fence wait/signal,
handle duplication/close, process death, resize/device removal and repeated use.
Measure stale frames/corruption, leaked descriptors/images and idle CPU.

Exit: actual required SharedImage operations pass independent probes and the
browser traces progress through them, without disabling those checks or enabling
unimplemented caps. Requirements unused by the first runtime remain tracked.

### G4. Full basic composition and efficient GPU delivery

Implement visual ordering, add/remove, offsets, matrix transforms, rectangle and
rounded clips, alpha/opacity semantics, and surface BeginDraw/EndDraw/update rules
actually required by the selected Chromium build. Add virtual-surface scroll/
resize/trim when exercised. Test overlapping visual content, removal/replacement,
shared references, dirty rectangles and color correctness. Do not conflate the
properties `SetOpacity` and `SetOpacityMode`.

Replace mandatory whole-frame CPU readback with GPU image composition/output for
the physical-GPU acceptance path, including the required OpenGL route. A shared
memory iGPU still needs correct cache/synchronization behavior and benefits from
avoiding redundant copies; zero-copy is a measured optimization, not a blanket
requirement on hardware unable to export the needed image type.
Align completion/pacing with D3's desktop presentation work without requiring
optional taskbar/Alt+Tab previews to be enabled. Preserve D1/D2 capture and effects;
do not compose the same child/window twice or add a continuously polling frame loop.
Specify how DXVK composition swapchains publish
content through the G2 bridge; do not assume native DXVK honors builtin WineDXGI
private interfaces. A separately pinned DXVK patch/upgrade may be needed. The
current master dummy path is only a diagnostic candidate, not the bridge itself.

Implement correct buffer queueing, waitable frame latency, visible/occluded/minimized
pacing, wakeups and completion statistics. Avoid busy waits and invented frame
stats; sustained Present success without displayed new pixels is failure.

Exit: selected browser/Electron/CEF/WebView2 cases pass actual on-screen checks
with DComp enabled, plus low-level visual/pacing tests and a bounded resource/
idle-CPU profile. Retain G1 fallback for unsupported backends, with honest caps.
Report application GPU rendering, application composition and outer desktop
composition separately, including any remaining CPU/readback segment.

### G5. Optional feature tracks, independently gated

- Video playback first with software decode and correct RGB composition. Then
  NV12/P010 views, YUV conversion/video processor and actual hardware decode
  profiles as supported. Use actual frame/decoder evidence, not a preference bit.
  Codec/patent/distribution questions need separate review; no proprietary codec
  or Windows Media Foundation binary is bundled as a workaround.
- Hardware overlays, HDR, protected content and new composition-texture interfaces
  are later, capability-gated features. Verify fallback when each is unsupported.
- WebGPU/Graphite/Dawn D3D11/D3D12 are separate tests. Add vkd3d-proton interop only
  when required; pin the coherent DXGI/D3D12 stack and validate all device features,
  not just Vulkan 1.3. Do not turn a WebGPU gap into a blocker for basic WebGL/UI.

Exit per feature: native API probes, consumer regression and explicit hardware/
software/unsupported status. Skipped features are not silently counted as passing.

### G6. Token integration, packaging and release acceptance

Coordinate with `TOKEN_SECURITY_WORKORDER.md`: same selected consumer versions,
ordinary unelevated browser, restricted children and functional handle semantics.
Sandbox-disabled runs can isolate failures using local pages in disposable
profiles with no credentials/network; pair them with sandbox-enabled runs.
No `--no-sandbox`, single-process GPU or equivalent default is acceptable evidence
of integrated completion. Functional sandbox configuration is not a claim of
hostile-code containment; hardening remains that project's later phase.

Extend `scripts/wex-graphics.py`, packaging/runtime manifests and launch integration
only as needed, retaining per-user private graphics views, next-login changes,
hash-verified 32/64 bundles and builtin fallback. Prefer per-app isolation where
the DComp backend differs from the desktop's selected graphics stack. Do not edit
shared system DLLs beneath live users, remove their overrides or install optional
proprietary compiler/fonts to make probes pass. Keep licenses/sources complete.

The baseline installation must boot and remain usable without a Vulkan-capable
GPU. Keep OpenGL and software modes discoverable and recoverable. Do not introduce
an unsupported legacy DXVK package solely to make a capability check pass; any
additional backend version needs a separate maintenance, security and test review.
Keep the user's desktop-effects setting independent of per-app API/backend policy.

Final exit: fresh installed image, tested upgrade/rollback, both split user types,
X11/Wayland, Classic/Open Luna and existing shell behavior. Hardware acceptance
requires actual physical-GPU evidence; VM/software tests remain separately useful.
Include physical older-Intel/OpenGL acceptance, not only a high-end Vulkan machine.

## 5. Test matrix and evidence requirements

Cover low-level probes in both PE architectures; the product matrix can state
which architectures each vendor actually supplies. Every Wine build must remain
combined WoW64 (`--enable-archs=i386,x86_64`). For Chromium tests, build the Windows
`gl_unittests` target at the pinned consumer revision; list/filter real DComp
tests and record matched, executed, skipped and failed counts. Do not run a Linux
target or count a wholly skipped fixture as Windows DComp coverage.

For every claimed passing path:

- Cover the G0 hardware/backend table, with supported OpenGL as a required path
  and Vulkan as an enhancement. Record unsupported feature cells rather than
  faking WebGPU/video/overlay support or failing the entire baseline for them.
- Test desktop composition off/on where supported, especially X11 D1/D2. On
  Wayland verify wexcomp/native-frame groups and app content remain one scene.
  Check no double redirection, recursive capture or second compositor owner.
- Confirm visible changing pixels, input, scrolling, Canvas2D and WebGL. Test
  resize/DPI, child clip/z-order, menus/popups, multiple windows, fullscreen,
  minimize/restore, occlusion, suspend/resume and device loss where available.
- Record `chrome://gpu`/equivalent feature and renderer data, actual DLL/backend
  traces and adapter identity, with no passwords/profile secrets. GPU process
  existence or a reported hardware feature alone is not proof of acceleration.
- On a real GPU, corroborate adapter selection with vendor/driver engine telemetry
  or GPU captures appropriate to the backend. Check traces for compulsory full
  frame CPU readback. Record CPU/GPU time, frame intervals/drops, restart counts,
  memory/FD growth and idle load, against controls on the same device.
- Measure the full path on older iGPUs: memory bandwidth/readback, upload copies,
  synchronization stalls, repaint area, thermal/power behavior where observable
  and idle wakeups. Report XRender/GL/Vulkan paths as actually measured. Low CPU
  use alone is neither acceleration proof nor evidence of efficient composition.
- Exercise long-running animation and repeated create/destroy/resize loops. Set
  measurable budgets after G0 baseline, publish the numbers before judging
  improvements, and explain any required readback or timer fallback.
- Keep Classic/Open Luna, taskbar locked/unlocked, toolbars, tray show/hide chevrons,
  32-bit apps, native Linux windows and unrelated games/runtime probes working.
  No Microsoft art in committed screenshots; use original local test pages.

Existing `WEBVIEW2.md` results used SwiftShader and a per-app Windows 7 workaround;
they are historical software-presentation evidence, not a current real-GPU pass.
Its old volatile VM paths may have been lost at reboot. Verify infrastructure
before assuming retained logs/binaries exist. Do not restart the user's live
desktop, repurpose an occupied VM or change passthrough settings without approval.

## 6. Deliverables, ownership and decision gates

Deliver `docs/CHROMIUM_GRAPHICS_ACCEPTANCE.md` with the exact matrix, versions,
patch provenance/license list, first-failure traces, commands/log paths, screenshots,
performance/resource results, known limitations and retained VM/session state.
Keep reusable original test sources and deterministic analyzers in Git; runtime
payloads and large private captures stay outside it. Update `WEBVIEW2.md`,
`WINLOGON_AND_DWM.md`, `STOCK_CHANGES.md` and graphics packaging docs only with
demonstrated facts. Include a D1-D4 / G0-G6 integration matrix: shared changes,
unchanged owners, tested old behavior and new application API behavior.

Review G0/G1 before major DComp integration; review G2 bridge design before DXVK
interop, and G3 synchronization before claiming shared GPU correctness. G2 and
G3 work may proceed in parallel after interface agreement; G4 depends on both.
G5 features can ship separately; integrated G6 cannot omit functional token tests
or the older-hardware/OpenGL and existing desktop-composition regression gates.
Coordinate D3/D4 and Wayland protocol owners before modifying their contracts;
do not block basic application graphics on optional shell previews/glass work.
If a dependency demands a wholesale runtime/compositor redesign, present options
and stop that expansion, continuing independent in-scope tests.

Do not promise calendar estimates before the baseline/port dependency review.
This is a multi-subsystem program, not a few stubs. Completion claims must name
the achieved level: software presentation, accelerated fallback, basic DComp,
efficient GPU composition, WebGPU or hardware decode. Submit tested branches for
review; do not merge master simply because a browser window opens.
