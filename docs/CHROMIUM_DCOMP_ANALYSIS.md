# Chromium 159 DirectComposition `E_INVALIDARG` analysis

## Scope and source version

This is a static comparison. The exact Chromium 159 tag/source snapshot was not
available through the Gitiles lookup, so the call-site trace uses Chromium's
current `ui/gl/dc_layer_tree.cc` plus the nearby historical implementation at
137.0.7151.68. The relevant `VisualSubtree::Update` structure and DirectComposition
calls are present in both. No Wine build, guest run, native Windows run, or pixel
validation was performed for this note.

## The call

The failing HRESULT is from `IDCompositionVisual::SetTransform`, in
`DCLayerTree::VisualTree::VisualSubtree::Update`, in the background-color visual
scaling branch. Current Chromium creates a 1x1 solid-color DirectComposition
surface and scales it to the overlay rectangle:

> `const float scale_x = static_cast<float>(quad_rect_.width()) /`
>
> `static_cast<float>(kSolidColorSurfaceSize.width());`
>
> `hr = ... background_color_visual_->SetTransform(matrix);`

Chromium then asserts `CHECK_EQ(hr, S_OK)`. See Chromium
[`ui/gl/dc_layer_tree.cc` lines 709-721](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/gl/dc_layer_tree.cc#709)
and the surrounding `VisualSubtree::Update` setter sequence at lines 419-466 and
679-681. The historical file has the same `VisualSubtree::Update` transform path
at [137.0.7151.68 lines 609-667](https://chromium.googlesource.com/chromium/src/+/137.0.7151.68/ui/gl/dc_layer_tree.cc#609).

Therefore the reported `dc_layer_tree.cc:717` check is a source-line-number
variant of the `SetTransform(matrix)` check, not `Initialize`, `SwapChainPresenter`,
`SetContent`, `SetClip`, `SetOpacityMode`, or `CreateSurfaceFromHandle`.

Chromium's argument is a finite `D2D_MATRIX_3X2_F` whose diagonal contains
`quad_rect.width / 1` and `quad_rect.height / 1`; an ordinary 800x600 overlay
therefore passes approximately `(800, 0, 0, 600, 0, 0)`. The exact rectangle is
runtime data and is not asserted here.

## Chromium's expectation

Chromium treats this setter as a normal retained visual-property update. The
source comment says that visual-tree methods should only fail for OOM and the
caller asserts success. In particular, Chromium deliberately uses a 1x1 solid
surface plus an arbitrary finite scale rather than allocating a full-size color
surface. This is why a scale greater than 64 is an expected input, not an invalid
API argument.

The same update routine separately calls `SetClip`, `SetOpacity` and
`SetOpacityMode`, and later calls `SetContent`; those are distinct operations.
The current-main source shows the `SetContent` call at lines 679-681 and the
opacity-mode calls at lines 531-545. None of those calls explains the specific
`E_INVALIDARG` if the failing check is the reported line-717 transform check.

## Our current behaviour

Wine's implementation is in
[`wine/dlls/dcomp/device.c`](../wine/dlls/dcomp/device.c):

* `dcomp_visual_SetTransform_locked()` accepts only finite matrix components,
  then rejects `_11`, `_12`, `_21`, or `_22` whose absolute value exceeds `64.0f`
  and rejects translations beyond `1048576.0f` (around lines 3041-3065).
* The Chromium background scale therefore returns `E_INVALIDARG` for normal
  window-sized overlays such as 800x600. This directly matches
  `0x80070057`, the HRESULT in the GPU-process fatal log.
* `dcomp_visual_SetClip_locked()` also has finite/absolute-value checks (around
  lines 3239-3270), but ordinary Chromium clip rectangles do not require the
  transform's 64-component limit.
* `dcomp_visual_SetOpacityMode_locked()` accepts the three public enum values
  `LAYER`, `MULTIPLY`, and `INHERIT` (around lines 3491-3517); the values used by
  Chromium are not rejected by this code.
* `dcomp_visual_SetContent_locked()` accepts an `IDXGISwapChain1`, an
  `IDCompositionSurface`, or this tree's `IWineDCompSwapChain` and returns
  `E_INVALIDARG` for other content (around lines 3285-3325). That is a separate
  compatibility boundary, not the transform failure identified above.

There are additional unimplemented handle paths: `DCompositionCreateSurfaceHandle`
is a spec stub (`wine/dlls/dcomp/dcomp.spec:19`), and both
`dcomp_device_CreateSurfaceFromHandle()` and
`dcomp_desktop_device_CreateSurfaceFromHandle()` return `E_NOTIMPL` (around
lines 4507-4512 and 5440-5445). Chromium's decode/YUV path does use
`DCompositionCreateSurfaceHandle`, `IDXGIFactoryMedia::Create...SurfaceHandle`,
and `IDCompositionDesktopDevice::CreateSurfaceFromHandle`; Chromium's source
shows the handle creation and decode-surface calls at
[`direct_composition_surface_win.cc` lines 977-1024](https://chromium.googlesource.com/chromium/src/+/15651144f3e2b74fd10ba26f94af6871a3eab8f9/gpu/ipc/service/direct_composition_surface_win.cc#977).
Those gaps can affect video overlays, but they do not produce the shown
`E_INVALIDARG` at the transform assertion.

## Minimal Wine change proposed

Change only `wine/dlls/dcomp/device.c`, in
`dcomp_visual_SetTransform_locked()`: remove the arbitrary `64.0f` magnitude
limit for the four scale/skew components, while retaining the finite-value
check and the existing translation safety policy until native behavior is
measured. The immediate semantic requirement is: accept any finite ordinary
2-D transform that Chromium uses for a visual, including scale 800 or 1920.

Do not broaden this fix into `CreateSurfaceFromHandle`, DXGI media swap-chain
creation, or interface advertisement. Those are separate implementation slices
and need their own native contract tests.

## Wine-Staging overlap

The local staging review says patches 0047-0064 add surface factories, surfaces,
surface content acceptance, BeginDraw/EndDraw, and composition tests; it also
explicitly describes staging as incomplete for virtual surfaces and handle-like
features (`docs/DCOMP_STAGING_PLAN.md:82-105`). `docs/STAGING_OVERLAP.md:11-16`
and `:32-37` recommend selectively adopting surface tests and shared-handle
ideas, not replacing this retained compositor. Neither document records a fix
for the 64-component transform rejection, and the staging overlap does not make
the `DCompositionCreateSurfaceHandle` spec stub implemented.

## Native-Windows-compatible unit test

Add a focused case to `wine/dlls/dcomp/tests/dcomp.c` (the implementation change
itself is intentionally not made by this research task):

1. Create a BGRA-capable D3D device, a DComp device, and a visual as existing
   tests do (`test_visual_SetTransformObject` is the nearby setup pattern).
2. Call `IDCompositionVisual_SetTransform()` with a finite matrix such as
   `_11 = 800.0f`, `_22 = 600.0f`, all other entries zero.
3. Require `S_OK`, then call it again with `NULL` and require `S_OK`.
4. Keep separate invalid-input checks for `NAN`/infinite values if the native
   oracle confirms their HRESULT; do not use the old 64 limit as the expected
   contract.

This test is API-level and should pass on native Windows as well as Wine after
the minimal change. It does not prove Chromium presentation or visible WebGL;
those still require a fresh-prefix WoW64 run and presented-pixel validation.

