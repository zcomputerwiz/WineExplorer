<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# DirectComposition versus Wine-Staging: selective compatibility plan

## Slices (ordered; each must build and test independently)

1. **S** Port staging tests for device creation/QI, visual setters, SetRoot and AddVisual; fix only
   HRESULT, NULL-output, version-gating, reparent and COM-lifetime mismatches in `dcomp/device.c`.
2. **S** Port `test_visual_SetBitmapInterpolationMode`, `SetBorderMode`, `SetBackFaceVisibility`,
   `SetOffsetX`, and `SetOffsetXAnimation`; retain our affine/scene implementation and add animation
   only after the test contract is understood.
3. **M** Port `test_surface_factory_CreateSurface` and `test_surface_begin_end_Draw`, then add
   staging's DXGI/D3D10/D3D11 descriptor assertions to our existing surface tests; reconcile the
   Windows same-surface BeginDraw behavior with our device-wide active-draw state.
4. **M** Implement animation objects and animated property evaluation (staging is only a test of a
   real Windows object; its implementation still returns `E_NOTIMPL`); test End/SetOffsetXAnimation.
5. **M** Adopt staging 0063/0064 `IDCompositionSurfaceUnknown::Unknown3` and resize semantics after
   mapping it onto our virtual-surface/physical-texture lifetime; preserve our private surface accessors.
6. **L** Adopt staging 0062 shared visual handles. Add the exact wineserver protocol/object ABI only
   when cross-process/shared-handle consumers require it; same-process-only use can defer.
7. **M** Fill effect/transform/clip parity: keep our working effect-group/rectangle-clip scene path,
   add public object/vtable/error semantics, then implement only effects demanded by a consumer.
8. **M** Add rendering tests for retained Commit, alpha/glass targets, child HWND/Chromium targets,
   surface layers, detach/rollback, and CPU+GL presentation; run staging tests on fresh WoW64 prefixes.

## Executive findings

1. Staging is not a rebase candidate: 0003–0025 build API/vtable shape, target/root/content and a
   polling compositor, while ours already has retained transaction semantics and broader producers.
2. Staging Commit starts a composition thread that scans targets, sleeps `1000/VREFRESH`, and uses
   D2D GDI interop plus `GdiAlphaBlend` (0017); it is simple but wake-rate, D2D allocation and GDI
   child-window cost are poor for many targets and it has no real vblank wait.
3. Staging only composites `IDXGISwapChain1` and later surface content; no alpha-window policy,
   premultiplied-alpha target contract, child-HWND policy, thumbnails, GL path, or retained scene.
4. Staging 0060 replaces per-device locking with one process-global DComp critical section. This
   avoids cross-device list/state races but serializes unrelated devices and does not make D3D state safe.
5. Staging 0061 correctly identifies the D3D hazard: it enables `ID3D11Multithread`, enters it, swaps
   `ID3D11DeviceContextState`, and restores/releases on every exit. Ours has the same isolation principle
   around staging copies, but must keep checking all HRESULT cleanup paths.
6. Staging surface BeginDraw allocates a guarded render-target texture and EndDraw copies the updated
   box immediately. It validates first non-virtual draw/full rect, bounds, output pointers and returns
   `DCOMPOSITION_ERROR_SURFACE_*`; its tests expose that Windows permits repeated BeginDraw on one
   surface, contrary to the comment/MSDN assumption. Our newly written SuspendDraw/ResumeDraw/Scroll/
   virtual-surface rules therefore need native test confirmation before being called compatible.
7. Staging 0062 is a real wineserver object, not a fake pointer: `server/d3dkmt.c`, `server/directory.c`,
   `server/object.h`, `server/protocol.def`, plus `dlls/dcomp/{device,target,visual}.c` and ntdll object
   tests. It is explicitly same-process (server checks owner process); pointer-sized `client_ptr_t`,
   generated protocol layouts and WoW64 handle conversion are the ABI risks.
8. Staging 0063 changes the public surface vtable to inherit a 100-method `IDCompositionSurfaceUnknown`
   layout; almost all methods remain `E_NOTIMPL`, while Unknown3 resizes. This is useful ABI prior art,
   not a reason to replace our physical-texture/private interface design.
9. Staging's tests are its strongest contribution: they pin down Windows HRESULT precedence, QI
   exposure, refcounts, target/root ownership, setter enum/duplicate-child behavior, surface formats,
   descriptors, draw state and rendering. Port tests before borrowing implementation.
10. Keep `IWineDCompSurface` and `IWineDCompSwapChain`: they carry our physical texture/frame validity,
    retained producer, alpha/glass, thumbnails, Chromium child targets and CPU/GL compositor contracts.
    `dcomp_private_iface.h` can replace duplicated private declarations only if vtable ownership and
    lifetime stay ours; it cannot replace these producer-specific interfaces at low cost.

## What the staging groups implement and quality

**0001 and 0003–0025, device/targets/visuals/Commit.** 0001 is a DXGI HACK: it creates a hidden
`STATIC` popup and delegates `CreateSwapChainForComposition` to `CreateSwapChainForHwnd`, with no
composition swap-chain validation. 0003/0005/0006 add Device, Device2 and DesktopDevice stubs;
0007/0008 add HWND targets; 0009–0013 add Visual/Visual2, QI and SetContent (initially swapchain-only);
0014/0015 SetRoot; 0016/0017 Commit; 0018–0022 add shared-handle/clock/device3 stubs/tests; 0023–0025
add partner Unknown1. COM objects are refcounted and target/device ownership is explicit, but the
composition thread holds the device lock while doing D2D/GDI work and waits on no producer fence.
SetContent creates D2D device/context/interop from the app swapchain device; failed creation is returned,
old content is released after new resources are ready. Limits: swapchain1 only, D2D dependency, no
alpha-window/child-HWND/premultiplied-target policy. Quality: useful API skeleton and tests, weak
presentation architecture.

**0026–0046, unknowns/visual properties/transforms/clips/animations.** 0026, 0028, 0030, 0032,
0034, 0036, 0038, 0041 add conformance around private/partner/unknown interfaces; 0027/0029/0031/
0033/0035/0037/0040 add mostly stub or S_OK slots; 0039 implements interpolation, border, back-face
visibility and AddVisual; 0042/0043 SetOffsetX; 0044/0045 animation tests; 0046 surface-factory entry
tests. This group is primarily ABI/vtable and HRESULT discovery, not a complete effects/transform/
animation engine. Locking improves in 0060 later; AddVisual rejects silent reparent/root conflicts and
does not take an extra pending-child reference. Quality: strong compatibility probes, incomplete feature
implementation; no performance cost until properties are rendered.

**0047–0064, factory/surfaces/composition.** 0047–0049 add SurfaceFactory and DXGI-backed CreateSurface;
0050/0051 accept IDCompositionSurface as content; 0052/0054 implement BeginDraw/EndDraw; 0053 tests
visibility only after EndDraw+Commit; 0055 composites surfaces; 0056/0057 child visuals; 0058/0059
formats (including R8G8B8A8); 0060 global lock; 0061 D3D context protection; 0062 shared visual handle;
0063/0064 SurfaceUnknown/resize. Surfaces use guarded D3D11 render-target textures, immediate copy of
the update box and front-buffer composition. The tests verify formats B8G8R8A8/R8G8B8A8/R16G16B16A16F,
alpha PREMULTIPLIED/IGNORE, bounds, descriptors, returned DXGI/D3D10/D3D11 interfaces, EndDraw errors,
Commit visibility, child z-order and color rendering. D2D BeginDraw is unsupported; virtual surfaces,
Suspend/Resume/Scroll are not implemented in staging. Quality: clear ownership and useful tests, but
copy-heavy, D3D11/D2D dependent, polling and limited to same-process object graphs.

**Tests, final 0065–0067.** Final test functions are: `test_DCompositionCreateDevice`,
`test_DCompositionCreateDevice2`, `test_device_CreateTargetForHwnd`, `test_device_CreateVisual`,
`test_visual_SetContent`, `test_target_SetRoot`, `test_device_Commit`,
`test_DCompositionCreateSharedVisualHandle`, `test_DCompositionWaitForCompositorClock`,
`test_DCompositionCreateDevice3`, `test_IDCompositionDesktopDevicePartner`,
`test_IDCompositionVisualUnknown`, `test_IDCompositionDeviceUnknown`,
`test_visual_SetBitmapInterpolationMode`, `test_visual_SetBorderMode`,
`test_visual_SetBackFaceVisibility`, `test_visual_AddVisual`, `test_visual_SetOffsetX`,
`test_visual_SetOffsetXAnimation`, `test_device_CreateSurfaceFactory`,
`test_surface_factory_CreateSurface`, `test_surface_begin_end_Draw`, and `test_surface_interface`.
0065 removes stack-address checks, 0066 forces the front buffer, and 0067 permits Device3. The tests
verify the Windows contract, but many earlier tests use `todo_wine`; do not treat a green staging run
as proof of visual fidelity or thread safety.

## Ours: comparative assessment

Ours (`dcomp/device.c:2688`, `dxgi/swapchain.c:426–669`, `wined3d/swapchain.c:1374+`,
`include/wine/dcomp_layer.h`) retains pending and active visual state, validates/rejects cycles and
bad reparenting, commits all bindings transactionally, supports swapchains and surfaces as layers,
glass/alpha targets, thumbnails, Chromium child targets, and CPU plus GL composition. Surface backing,
content-valid state, SuspendDraw/ResumeDraw/Scroll and virtual Resize/Trim are now present in the tree.
That is materially better for DWM and Chromium integration than staging's one swapchain per target.

Risks are proportional to that scope: extra layer references and auxiliary 1x1 producers, cross-DLL
producer activation/rollback, staging copies and D3D immediate-context synchronization, and more COM
interfaces in one file. Known review findings include rollback revoking already-activated producers
(old scene is not re-published), and any future missed HRESULT/lifetime edge in `set_target_scene` or
`dcomp_binding_*` can blank a target. `GetFrameStatistics` is a zeroed success stub; animation and
Device3 transforms/effects are public stubs. Our retained scene is better; staging's narrow object
ownership and conformance coverage are safer.

## Compatibility matrix: predicted staging tests against ours

**Device/target/visual:** `test_DCompositionCreateDevice` P for NULL output, v1 QI and refcount,
but TODO for native no-BGRA/device-null behavior; `test_DCompositionCreateDevice2` P for exports/QI,
T for exact rendering-device validation. `test_device_CreateTargetForHwnd` P (including desktop HWND
E_INVALIDARG). `test_device_CreateVisual` P for version-gated QI/refcount. `test_visual_SetContent` P
for swapchain/surface acceptance, T for exact unsupported-IID HRESULT. `test_target_SetRoot` P for
root ownership, T for every native duplicate-root error. `test_device_Commit` P for retained scene
visibility, but not staging's asynchronous polling timing. `test_visual_AddVisual` P for cycle,
duplicate-child and z-order semantics; verify reference counts. Setter tests `SetBitmapInterpolationMode`,
`SetBorderMode`, `SetBackFaceVisibility`, `SetOffsetX` are P/T (properties exist; enum/error precedence
needs native comparison). `test_visual_SetOffsetXAnimation` F (`CreateAnimation` is E_NOTIMPL).

**Unknown/partner/clock:** `test_DCompositionCreateSharedVisualHandle` F (not exported as staging's
handle object); `test_DCompositionWaitForCompositorClock` T (ours has commit completion, not staging's
clock contract); `test_DCompositionCreateDevice3` P for public Device3 QI, F/T for exact vtable stubs.
`test_IDCompositionDesktopDevicePartner`, `test_IDCompositionVisualUnknown`, and
`test_IDCompositionDeviceUnknown` are F unless the private interfaces are exposed with the exact
versioned vtables; do not expose them merely to make tests green.

**Surfaces:** `test_device_CreateSurfaceFactory` P; `test_surface_factory_CreateSurface` P for formats,
alpha and physical textures, T for exact size/unsupported-render-device HRESULT. `test_surface_begin_end_Draw`
mostly P for bounds, first full draw, returned descriptors and EndDraw errors; F/T for staging's repeated
same-surface BeginDraw behavior, D3D10/D3D11 guarded flags and error precedence. `test_surface_interface`
F until `IDCompositionSurfaceUnknown` is added. The staging rendering portions of `test_device_Commit`
are F/T for surface layers until our current producer path is runtime-tested. Child-visual rendering
tests should pass semantically but differ in premultiplication/alpha and timing.

## Adoption details, risks and licensing

Port tests that need no staging compositor first: device creation/QI and NULL output; visual property
setters; SetRoot/AddVisual; animation object calls; SurfaceFactory; BeginDraw/EndDraw; then
SuspendDraw/ResumeDraw/Scroll/virtual-surface tests. In each slice, change only `device.c` unless a
test requires `tests/dcomp.c`/Makefiles; compare exact HRESULT precedence and refcounts on x86_64+i386.

0062 requires `server/d3dkmt.c`, `server/directory.c`, `server/object.h`, `server/protocol.def`,
generated protocol headers, `dlls/dcomp/{dcomp_private.h,device.c,target.c,visual.c}`, and
`dlls/ntdll/tests/om.c`. It adds a new `Composition` server object with owner-process reference and
three requests carrying `obj_handle_t`/`client_ptr_t`. Same-process-only design may defer it; do not
fake it with a pointer or alter our cross-process policy. 0063/0064 requires the exact inherited
surface vtable, QI identity, Unknown3 resize validation and resize invalidation; retain
`IWineDCompSurface` for physical texture/frame validity.

Animation: staging has no implementation, only `End(animation, time, value)` and property tests.
Implement a refcounted keyframe object, validate unfinished/end ordering and evaluate at Commit; do
not copy staging's E_NOTIMPL path. Effects/transforms/clips: ours already has effect-group opacity,
rectangle clip and affine scene evaluation; borrow staging method ordering/error tests, then add public
objects incrementally. The DXGI HACK is not suitable: replace it only with a real composition swapchain
descriptor/producer path, which ours already approximates through `IWineDCompSwapChain`.

Ported staging tests/code retain LGPL-2.1+ and Zhiyi Zhang/CodeWeavers attribution. Commit messages for
each port should include `Based on Wine-Staging patch NNNN by Zhiyi Zhang <zzhang@codeweavers.com>` and
preserve the `Signed-off-by` lines exactly as present in the source patch. Keep Wine notices unchanged.

This is static research; no source was edited, no WSL was used, no commit was made, and runtime pass/fail
predictions require fresh-prefix WoW64 runs.
