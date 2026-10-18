<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium graphics research and review of Gemini's assessment

Date: 2026-10-18. Implementation plan: `CHROMIUM_GRAPHICS_PLAN.md`.
Scope: Windows Chromium-derived applications on WineExplorer, not Linux Chromium.
This is source research; no new browser, GPU, Wine build or VM experiment was run.
The user's pasted assessment is treated as a hypothesis, not an API specification.

## Executive finding

The subsystem map is useful: DComp, DXGI, D3D sharing/synchronization and Wine
window presentation interact. However, the assertion that current Chromium has
no non-DComp presentation route is false for the inspected revision. Test that
route first. Full DComp is still a valuable compatibility project, not necessarily
a prerequisite to every accelerated browser frame. Pin each tested application:
Chromium HEAD, a released browser, Electron, CEF and WebView2 need not take the
same path or accept the same switches.

## Pinned sources inspected

| Project | Reference | Revision |
|---|---|---|
| Local Wine gitlink | `wineexplorer/11.19` recorded in parent HEAD | `ed5fe967ed471ca524666631ad70dabbed6af076` |
| Wine upstream | master | `7298a818b79446bae153b8d5aa27235ab3703bf9` |
| Chromium | main | `2aa482d1e15bf9d2c382df5cfcb474fb6c0acbd1` |
| giang17/wine | d2d1-dcomp-11.0 | `eb72ef008555348bf114e5da64c0d8868c46f49a` |
| DXVK | master | `d744290a405ec542f7b59b91ed11128be797104f` |
| Proton-GE | GE-Proton11-7 | `c191f35dcebbeccfacd3b4c6f6eea026e588c1c2` |
| Wine-Wineland | wineland_20260713-reorg | `53568149989c0ea24ad0a8b6fab0c7e291e13630` |
| ANGLE | main | `b82a564646a26535ccedfa7a69134c0fd6654ac2` |
| Dawn | main | `67d4d6e64b0755d638f76549794692badfdbc8ab` |

These are independently resolved upstream references, not proof that the deployed
image uses them. The runtime manifest actually bundles **DXVK 2.4.1** and
**vkd3d-proton 2.13**. Do not silently replace these with HEAD. Chromium's own
DEPS pins, not unrelated ANGLE/Dawn main revisions, govern a tested Chromium build.

## Accuracy audit

### A. Non-DComp fallback was not universally removed

At the pinned Chromium revision:

- [image_transport_surface_win.cc](https://github.com/chromium/chromium/blob/2aa482d1e15bf9d2c382df5cfcb474fb6c0acbd1/gpu/ipc/service/image_transport_surface_win.cc)
  returns a DComp presenter when supported, but also implements a native EGL
  surface route without DComp.
- [skia_output_surface_impl_on_gpu.cc](https://github.com/chromium/chromium/blob/2aa482d1e15bf9d2c382df5cfcb474fb6c0acbd1/components/viz/service/display_embedder/skia_output_surface_impl_on_gpu.cc)
  retains non-presenter output selection; the GL branch explicitly covers Windows
  with DComp disabled. Other backends have their own selection logic.
- [direct_composition_support.cc](https://github.com/chromium/chromium/blob/2aa482d1e15bf9d2c382df5cfcb474fb6c0acbd1/ui/gl/direct_composition_support.cc)
  honors `disable-direct-composition` and initializes via `DCompositionCreateDevice3`,
  requesting DesktopDevice then Device3. Device4/texture support is separately gated.

This disproves the universal removal claim; it does not prove a particular
packaged application will use that fallback successfully under our Wine.

### B. Several interface and ownership details need correction

- The GPU-side
  [child_window_win.cc](https://github.com/chromium/chromium/blob/2aa482d1e15bf9d2c382df5cfcb474fb6c0acbd1/ui/gl/child_window_win.cc)
  creates the intermediate window.
  [rendering_window_manager.cc](https://github.com/chromium/chromium/blob/2aa482d1e15bf9d2c382df5cfcb474fb6c0acbd1/ui/gfx/win/rendering_window_manager.cc)
  registers/validates a child for a browser parent. Cross-process parenting is
  relevant, but the normal target is not simply a browser-owned child arbitrarily
  targeted from another process: DCLayerTree explicitly expects its target HWND
  to belong to the calling process.
- [dc_layer_tree.cc](https://github.com/chromium/chromium/blob/2aa482d1e15bf9d2c382df5cfcb474fb6c0acbd1/ui/gl/dc_layer_tree.cc)
  uses both `SetOpacity` and `SetOpacityMode`; these are distinct operations, not
  alternative names for one property. Newer optional interfaces must be traced
  rather than advertised just to satisfy QueryInterface.
- `ExtendedResourceSharing` belongs to
  [D3D11_FEATURE_DATA_D3D11_OPTIONS](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_feature_data_d3d11_options),
  not OPTIONS2. [OPTIONS2](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_feature_data_d3d11_options2)
  has different fields. NV12 needs its actual format/view/resource tests; this
  single boolean is not sufficient evidence of plane support.
- There is no verified basis here for imposing a universal Windows 10 1809 / WDDM
  2.x minimum across all Chromium-derived versions. Record the selected product's
  supported OS/features and actual runtime gates instead.

### C. Composition is more than returning COM objects or blitting on Commit

[Commit](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice-commit)
publishes pending state atomically. A retained tree must also display newly
presented swapchain frames without the application rebuilding that tree every
frame. Presentation pacing, completion and resource ownership must be real.
An offscreen-image design is plausible, but a delayed HWND-backed Vulkan
swapchain is not automatically equivalent for unattached, shared or multiply used
content. Reject the assumption that a successful one-plane demo guarantees full
WebGL, browser UI, video and WebGPU compatibility.

The two-layer strategy (GPU rendering plus separate display composition) is
reasonable. Choose the interop contract and implement it; do not infer one from
private WineD3D structures when a native DXVK DLL is loaded.

### D. DXVK version and backend mixing matter

[DXVK 2.4.1 factory](https://github.com/doitsujin/dxvk/blob/v2.4.1/src/dxgi/dxgi_factory.cpp)
unconditionally returns E_NOTIMPL for composition swapchains. Inspected
[DXVK master](https://github.com/doitsujin/dxvk/blob/d744290a405ec542f7b59b91ed11128be797104f/src/dxgi/dxgi_factory.cpp)
has an optional dummy-composition path. This is not a full DComp backend.
Maintainer discussion in [DXVK #5053](https://github.com/doitsujin/dxvk/issues/5053)
identifies missing composition integration. Installing or upgrading DXVK alone
does not solve our DComp gap.

### E. Advanced features and testing advice are overbroad

Shared resources/keyed mutexes are important, while shared fences, NV12, video
processing, D3D12, Graphite and modern composition textures are path-dependent.
Do not make every one a prerequisite for basic accelerated UI/WebGL, or claim
software video playback proves hardware decoding. Truthful unsupported overlay
caps can select fallbacks, but must be tested in the selected application.

The source map mixes generations: `ui/gl/direct_composition_child_surface_win.cc`
is absent at the inspected revision. The pinned
[BUILD.gn](https://github.com/chromium/chromium/blob/2aa482d1e15bf9d2c382df5cfcb474fb6c0acbd1/ui/gl/BUILD.gn)
does include `dcomp_presenter_unittest.cc` in the **Windows** `gl_unittests` target.
Do not claim a Linux build of that target exercises Windows DComp, or that a test
filter with zero matched/skipped tests is passing coverage.

Reject the recommendation to **always** use `--no-sandbox`: it can be a labeled,
offline diagnostic comparison only, never an acceptance or shipping default.
Token work and graphics work must eventually pass together.

## Prior work: ranked reuse recommendations

### 1. Wine/WineD3D: baseline, tests and upstreamable core

Local `dlls/dcomp/device.c` and the inspected upstream version still stub the
three creation functions. Local DXGI composition creation, NT resource sharing
and D3D11 fences also have concrete gaps. Extend existing Wine modules/tests and
server handle conventions rather than adding an unrelated desktop service.
Inspected Wine files are LGPL-2.1-or-later; preserve file notices.

### 2. giang17/wine: strongest located DComp implementation candidate

Inspect the pinned [device.c](https://github.com/giang17/wine/blob/eb72ef008555348bf114e5da64c0d8868c46f49a/dlls/dcomp/device.c),
[factory.c](https://github.com/giang17/wine/blob/eb72ef008555348bf114e5da64c0d8868c46f49a/dlls/dxgi/factory.c),
and [fence.c](https://github.com/giang17/wine/blob/eb72ef008555348bf114e5da64c0d8868c46f49a/dlls/d3d11/fence.c).
The inspected DComp/fence headers are LGPL-2.1-or-later. The fork has retained-tree
and composition-swapchain work, but also readback/GDI/timer paths and a CPU-timeline
fence model. Its `OpenSharedResource1` remains stubbed. Treat it as selective code
and test material, not a complete modern Chromium GPU backend.

Its [README](https://github.com/giang17/wine/blob/eb72ef008555348bf114e5da64c0d8868c46f49a/README.md)
warns against DXVK for that path. Its [patch inventory](https://github.com/giang17/wine/blob/eb72ef008555348bf114e5da64c0d8868c46f49a/PATCHES.md)
describes broad unrelated changes. Do not merge the entire fork or import font,
heap, audio, installer and application-specific work to obtain DComp. Verify GPU
completion rather than assuming a CPU event/value can stand in for shared fences.

### 3. Proton-GE / Wine-Wineland: child-window delivery and lifecycle

[GE-Proton11-7](https://github.com/GloriousEggroll/proton-ge-custom/releases/tag/GE-Proton11-7)
reports game-specific WebView2 rendering improvements, not general DComp support.
Its pinned [child-rendering notes](https://github.com/GloriousEggroll/proton-ge-custom/blob/c191f35dcebbeccfacd3b4c6f6eea026e588c1c2/patches/wine-hotfixes/wineland-child-rendering/README.md)
identify Erhan Bilgili / [Wine-Wineland](https://github.com/nanomatters/wine-wineland/tree/53568149989c0ea24ad0a8b6fab0c7e291e13630)
and 85 selective patches. Useful areas: DMA-BUF producer/consumer lifetime, EGL
surface reattachment, geometry and frame throttling. Select by reproducer and
dependencies; avoid importing duplicate SNI, unrelated overlay or desktop policy.
Keep original authors/Source trailers and inspect each modified file's license.

### 4. DXVK and vkd3d-proton: GPU-side interop, not compositor replacements

DXVK's resource, keyed-mutex and fence implementations are candidates for the
Vulkan-backed path and tests. Its LICENSE is permissive zlib-style; verify each
file/dependency. Pin a proposed upgrade separately from our bundled 2.4.1.
[vkd3d-proton 2.13](https://github.com/HansKristian-Work/vkd3d-proton/blob/v2.13/README.md)
is a later D3D12 interop candidate, not an initial prerequisite. Vulkan 1.3 alone
does not establish its full device requirements. Inspect its per-file notices.

### 5. ANGLE / Dawn / Chromium: requirements and consumer-side tests

Use Chromium's pinned DEPS to find corresponding ANGLE native-window/swapchain,
external-image and synchronization code, and Dawn shared-texture/fence tests.
The inspected ANGLE and Dawn LICENSE files use BSD-style terms with possible
file/third-party exceptions. Prefer adapting small tests and contracts; do not
fork all of Chromium or replace ANGLE globally as the first solution.

### 6. Older Wine DComp prototypes: secondary, unverified leads

DXVK #5053 links baikaishiuc's `wine-dcomp` / `zxw/dcomp/dxvk4` and zhiyi's
`bug-23698-react-native` on WineHQ GitLab. Direct access failed during research;
their current code, provenance and completeness were not verified. Revisit if
accessible, but do not present them as ready-to-import implementations. ReactOS
was not established as a competitive modern DComp/Chromium graphics backend.

## Research limitations and failed retrievals

Initial sandboxed GitHub API calls failed DNS resolution; approved read-only
public-source requests succeeded and resolved the pins above. Several web raw/
Gitiles views failed or returned incomplete search results; direct pinned public
source reads succeeded. The old child-surface path returned 404, confirming that
the supplied map needs versioning. A Windows `rg installer/*.json` glob produced
os error 123; reading the explicit runtime manifest established the bundle pins.
No external code, proprietary screenshot or application payload was imported.
