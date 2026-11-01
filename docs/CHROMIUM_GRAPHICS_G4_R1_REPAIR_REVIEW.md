<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Interrupted G4 R1 reference renderer: repair and verification

Date: 2026-10-20. Review branch: **codex/g4-r1-completion** in parent and Wine.
Wine repair: **0fd6cabcdd2a6047877ba67185b1732ee2b272d4**.
Machine-readable results/hashes: evidence/CHROMIUM_G4_R1_REPAIR_20261020.json.

## Outcome and provenance

The interrupted retained-renderer prototype and its failing multilayer test have
been repaired. The expanded seven-group screen-pixel suite passes for x86_64 and
i686 using both software OpenGL and explicitly selected software Vulkan. Earlier
G0/G1/G2/G3 diagnostics, the eleven-case ordering probe, and the five-case pacing
probe also pass for both architectures. This is a verified **bounded CPU reference
slice**, NOT full R1/G4, accelerated composition, browser support or merge approval.

The occupied graphics checkout initially had parent 97c92930, Wine f3ecadd3, two
dirty test files and eight dirty Wine files. These were preserved on new review
branches as parent 7d3142de / Wine aa8be2e before repairs. No reset/clean, OOBE/
token import or master change was performed.

Gemini's published parent 941e97f0 points to Wine 26a2d8d. Its additional 33 lines
were fetched and reviewed: old independent-blit reflection bounds are superseded
by inverse affine sampling; fully_clipped reset and correct GL back-drawable
delivery are included in the repaired route; unconditional diagnostic fprintfs
were not retained. Thus the dirty checkout was not incorrectly assumed identical
to the slightly newer published WIP. That WIP remains independently recoverable.

## Findings fixed

1. The original local prototype treated a scratch texture as a window drawable,
   crashing on a NULL swapchain owner. GL output now uses the actual back drawable;
   Vulkan output uses its normal presentation image. Painting only GDI underneath
   a GL child window is not accepted as successful visible delivery.
2. Reading another producer's mutable back buffer exposed unpublished frames and
   rotating-buffer state. Each producer now owns a reference-counted CPU copy of
   its last presented frame; scenes reference those frames, not raw producer
   swapchain pointers. Fresh readback prevents stale SYSMEM cache reuse.
3. Commit prepares every bounded surface/scene before changing active tree state.
   Preparation failure cancels staged resources. Reused content is prepared and
   published once, even when drawn in multiple places. Commit redraws retained
   pixels without rotating/consuming a producer back buffer. No extra Present is
   necessary to restore a removed layer's retained background.
4. GetDesc1 always reported IGNORE alpha. Composition creation now preserves its
   requested alpha mode, while ordinary HWND swapchains keep their old behavior.
   BGRA/RGBA and actual premultiplied pixel alpha have separate pixel assertions.
   The factory validates builtin type before accessing d3d11-only structure fields;
   D3D12/custom objects cannot be blindly cast as d3d11 composition objects.
5. Window resize now reallocates the reference target. Vulkan recreates a stale
   presentation extent before copying, avoiding accidental scaling/color changes.
   Fractional clip bounds remain floats instead of being enlarged to integer
   rectangles. Destination bounds are clamped before float-to-integer conversion.
6. Target destruction revokes bindings. Reparenting retains a child before its
   old parent releases it. Foreign/cross-device visual roots/children reject.
   Scene masks/references are replaced only after old queued work finishes.
7. Unsupported ancestor clipping, isolated non-unity LAYER-opacity groups,
   cross-device scenes, multiple rooted targets, unsupported formats and excessive
   reference dimensions reject rather than silently misrender. The reference
   limit is 32 content entries, 128 visuals, and at most 8192 per target dimension /
   16,777,216 target pixels (64 MiB per reference surface, not a total memory cap).

The extended private binding has a new IID. Install/build matching dcomp, dxgi and
wined3d DLLs together; old private bindings/native DXVK do not gain compatibility
by implementing public DXGI.

Opacity inheritance/defaults follow the documented distinction: LAYER isolates a
subtree, whereas MULTIPLY applies opacity to each surface. A default non-unity
LAYER group with children is rejected before active state changes; explicit
MULTIPLY has a different, discriminating pixel oracle. See Microsoft's
[SetOpacityMode specification](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionvisual2-setopacitymode).
No Microsoft implementation, assets or reference captures were copied.

## Actual checks

| Check | x86_64 | i686 | Evidence directory |
| --- | --- | --- | --- |
| Seven-group G4 correctness, llvmpipe | PASS | PASS | staged5-gl |
| Same seven groups, explicit Vulkan ICD | PASS | PASS | final-vk |
| Eleven-case G3 ordering/lifetime probe | PASS | PASS | g3-full-gl |
| Earlier G0/G1/G2/G3 diagnostic phases | PASS | PASS | g3-full-gl/full-suite.json |
| Five-case flip/latency/minimize/pacing probe | PASS | PASS | pacing-gl |
| Windows/native source byte comparison | PASS | PASS | 10 Wine files + 3 test sources |
| Upstream Wine d3d11/dxgi test binaries | NOT RUN | NOT RUN | inherited --disable-tests build |
| Real browser, physical GPU, installed desktops/themes | NOT RUN | NOT RUN | separate acceptance gates |

Every final runner exited 0. OpenGL is Mesa 25.0.7-2+deb13u1 llvmpipe, LLVM 19.1.7,
256 bits, accelerated=no (glxinfo.txt). Vulkan uses
/usr/share/vulkan/icd.d/lvp_icd.json with its hash in the manifest. Wine's advertised
"NVIDIA GeForce GTX 470" adapter is NOT physical Nvidia hardware evidence.
Both MinGW compilers report 14-win32. The owned native build is combined WoW64:

```
../../wine/configure --enable-archs=i386,x86_64 --prefix=/usr/local --disable-tests
make -j6 dlls/dcomp/all dlls/dxgi/all dlls/wined3d/all
```

The seven groups exercise supported placement/clip/detach transactions; invalid
properties and cycle checks; multilayer z-order, unpublished next-frame isolation,
removal without another Present, group/pixel alpha, leaf rounded clips, reflection,
shear and rotation; no-message-pump cross-thread completion; imported fence
observers including a child process; 60 changing frames after one Commit; and
BGRA/RGBA PREMULTIPLIED/IGNORE variants, target resize, caller/producer/target
release, duplicate content and deterministic preparation rejection. Expected
colors are exact encoded UNORM values, not broad-tolerance matches. This does not
establish linear-light blending or antialiased edge/interpolation fidelity.

The preparation failure case explicitly bypasses default resize clamping with
SWP_NOSENDCHANGING, verifies an actual 8292 x 266 client area, receives E_NOTIMPL
(80004001), restores normal size, and confirms the staged offset did not leak.
It is an allocation-POLICY failure test, not exhaustive allocator fault injection.

## Reproduction and evidence paths

Native source: /var/tmp/wex-g3-repair-20261018/clean-source.
Build: that directory's build/wow64. This is the reviewer's prior owned lab, NOT
Gemini's lab or a shared VM. Current production sources and test sources were
copied into it and compared byte-for-byte; exact source/DLL/loader/config/probe
hashes are in the manifest. No builds were modified while the final suites ran.
Private logs are /var/tmp/wex-g4-r1-repair-20261020/evidence.

Run from a private mount/network namespace with the owned x-sockets directory
bound over /tmp/.X11-unix; never replace global WSLg sockets. Exact runner form:

```
unshare --mount --net --propagation private sh -c 'mount --bind /var/tmp/wex-g4-r1-repair-20261020/x-sockets /tmp/.X11-unix; python3 -B /var/tmp/wex-g3-repair-20261018/clean-source/scripts/check-g3-correctness.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --output-dir /var/tmp/wex-g4-r1-repair-20261020/evidence/NEW_DIRECTORY --g4-correctness'
```

Final GL used staged5-gl. Vulkan used final-vk with appended
`--renderer vulkan --vulkan-icd /usr/share/vulkan/icd.d/lvp_icd.json`.
Earlier-suite invocation uses output g3-full-gl and replaces --g4-correctness
with --full-suite. Pacing uses check-g4-pacing.py, output pacing-gl, no mode flag.
Use a NEW output name on repetition; runners intentionally refuse overwrites.
Commands, exits, cleanup, probe hashes and renderer selection are also retained
directly in each results.json and the checked-in manifest.

## Failures retained, not converted into passes

For the following run labels, the exact command is the reproduction command
above with NEW_DIRECTORY replaced by the label (and Vulkan flags for -vk runs).

- prototype-gl: compiler could not include test_g4_composition_probe.cpp. Copied
  that required include into the owned native source.
- prototype-gl-retry: linker reported undefined __imp_GetPixel,
  __imp_GetStockObject and __imp_GdiFlush. The native compiler helper was stale;
  copied current test_graphics_probes.py, which already links -lgdi32.
- prototype-gl-retry2: reproduced the NULL-owner drawable page fault, WineDbg
  attachment and the runner's 120-second timeout. The bounded runner cleaned up
  its own prefix/server; the crash is not a passing Gemini baseline.
- repair1-gl: GDI delivery was hidden by the GL child surface. repair2-gl,
  diag-gl and diag2-gl: front-drawable output followed by SwapBuffers exposed the
  wrong buffer. Retained data was correctly red; output remained black. Fixed
  the real back-drawable route and removed temporary G4_DIAG diagnostics.
- repair3-gl: detach left old pixels, and changing-frame readback reused stale
  SYSMEM data. Clear only after detaching the old scene; force current GPU readback.
  repair4-gl then passed all six inherited groups on both architectures.
- extended-gl, extended2-gl and extended3-gl: new premultiplied pixel-alpha test
  caught GetDesc1 falsely reporting IGNORE. extended-vk additionally covered the
  same failure. Corrected composition alpha metadata, not the expected colors.
- staged-gl and staged-vk: new test's `s.visual->SetOffsetX(0)` was ambiguous
  between float and animation-pointer overloads. Changed exactly to
  `s.visual->SetOffsetX(0.0f)`; no assertion was removed.
- staged2-gl and staged2-vk: the new factory guard queried a composition-only
  private interface BEFORE comp_wnd was assigned, rejecting every creation.
  Replaced it with checked builtin-vtable identification before the cast.
- staged3-vk: resize scaled the target into a stale Vulkan extent (expected
  half-blue over red, actual full blue). Recreated the actual presentation extent.
- staged3-gl, staged4-gl and staged4-vk: the preparation test got S_OK because the
  default window procedure clamped its requested oversized window. Added
  SWP_NOSENDCHANGING and logged/verified actual client dimensions. The oracle
  still requires E_NOTIMPL and unchanged old placement, not either HRESULT.

Authoring/inspection failures: apply_patch could not find
`static void STDMETHODCALLTYPE d3d11_swapchain_wined3d_object_destroyed(void *parent)`;
actual callback is `_released`, and the corrected hunk applied atomically.
`git -C wine diff --check` found three whitespace-only lines after branch removal;
fixed with apply_patch. Guessed blitter.c / blitter_gl.c lookups returned OS error
2; actual GL blitting lives in texture_gl.c. An adapter*.c rg argument returned
Windows OS error 123; used explicit files/directory searches. A non-escalated WSL
inspection returned Wsl/E_ACCESSDENIED; reran the read with approved access.

`git -C wine fetch origin gemini/chromium-graphics-g4-completion` succeeded but
`git -C wine diff --stat aa8be2e origin/gemini/chromium-graphics-g4-completion`
and `git -C wine log -1 --oneline origin/gemini/chromium-graphics-g4-completion`
failed with ambiguous revision: this submodule's restricted refspec only populated
FETCH_HEAD. Used `git -C wine diff --stat aa8be2e FETCH_HEAD` and
`git -C wine log -1 --oneline FETCH_HEAD`, then reviewed the four-file difference.

## Still required before claiming the larger milestone

Ancestor/nested transformed clips and isolated non-unity LAYER groups need real
rendering, not removed guards. Multi-target transactions and cross-device content
remain rejected. Nearest/binary sampling, interpolation/border-mode fidelity,
overlapping/oversized corner-radius behavior and arbitrary underlying application
background transparency need further implementation/coverage. This reference
canvas clears uncovered pixels to black; it does not claim transparent desktop
composition. Do not equate the passing original multilayer group with all R1.

R2 still needs exhaustive lifetime/failure/concurrency, fence registration-race,
leak and bounded idle-CPU proof. R3 needs measured accelerated sharing/delivery,
less readback/copying, native DXVK integration and actual pinned Chromium,
Electron/CEF/WebView2 behavior without sandbox/version bypasses. Upstream Wine
d3d11/dxgi tests must be built/run, not inferred from the project probes.

R4 remains actual installed X11 outer-composition OFF/ON and Wayland/wexcomp,
Classic/Open Luna, actual 800x600 and normal resolution, locked/unlocked taskbar,
toolbar grippers/separators/overflow and expanded/collapsed/absent tray chevrons,
ordinary apps plus older Intel/low-end AMD/Nvidia/virtual GPU hardware. None was
silently certified from unchanged theme files or software Xvfb.

No VM was started/reset/reinstalled. The OOBE checkout and workers were untouched.
All final runners exited; owned servers/display processes were absent at audit.
Private source/build/log directories remain for reproduction; no proprietary
payloads, license documents, art or XP captures were added to Git or an ISO.
