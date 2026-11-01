<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 ordinary application underlay repair — second continuation

Branch: `codex/g4-r1-completion`; starting parent 0ec082cc / Wine 273cacd.
Published Wine step: `48000ca83d2240c4dd5d0101d4ebff5fc76f6b74`.
This is an R1 slice, NOT complete G4 or desktop/theme/hardware acceptance.

## Contract and discriminating negative controls

Microsoft's [CreateTargetForHwnd contract](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice-createtargetforhwnd)
places composition content above ordinary content drawn to the target HWND.
The independent test paints the window's GDI backing green, presents a smaller
red/blue composition bitmap and requires the uncovered screen pixels to remain
green. A second point below the red leaf reads the GDI backing itself to ensure
that background is not accidentally the already-composited screen image.

`background-negative-20261020/`: both PEs FAIL at Wine 273cacd. Window DC reads
green under the leaf and in uncovered space, but actual uncovered screen pixels
are black. This directly isolates the renderer's hard-coded background clear.
No Microsoft implementation code, binary/art or reference source was copied.

## Implementation / costs

- Prepare a reference-counted 32-bit top-down DIB with the target surface before
  publication; release it with prepared/active scene resources. Reuse matching
  dimensions and prepare replacement storage before dropping resize resources.
- Capture the window's separate GDI backing, not GetDC(NULL)/screen/previous GL
  output. Select the cached DIB only into a temporary CS-thread memory DC, then
  restore/delete that DC. No persistent HDC tied to a caller thread survives.
- Keep that backing alive in X11: its ordinary direct-drawing optimization
  previously discarded the window DIB when a GL client window existed. A
  private HWND-owned surface-policy property keeps GDI on the ordinary window
  surface, including across resize and target recreation; it dies with HWND,
  not the last target. Win32u invalidates cached DCs and updates the surface on
  policy changes. Each layer separately owns its live registration; duplicate
  targets on that layer return WINDOW_ALREADY_COMPOSED, and final release frees
  that slot. These properties are implementation coordination, not a security
  boundary or public API. Retaining a normal backing DIB for the window's life
  is intentional; no timer, polling worker or target-owned HDC is retained.
- Reference composition starts with that current opaque GDI image, then blends
  retained premultiplied content/effect groups over it. Repeated Commit must not
  accumulate the previous composition into its own background.
- Eligible opaque GL scenes upload the GDI image below their GPU-resident leaves
  when uncovered space requires it. A fully covering opaque leaf makes the
  underlay irrelevant and skips that capture/upload. Producer image readbacks
  are still avoided by the existing narrow GL route.
- Cost: another width*height*4 CPU DIB per ordinary target; GDI capture/copy and,
  where needed, an underlay texture upload each composition. The existing four
  path counters describe producer-image traffic, NOT this new GDI work. Do not
  call zero producer readbacks a zero-copy/CPU-free/power or speed result.
- Layered/no-redirection windows retain the previous transparent base behavior;
  this does not establish arbitrary layered-window, DirectX-present underlay,
  native child-window ordering, Wayland or desktop composition integration.

Backend capture failure does not publish a fabricated black replacement. The
late asynchronous device/window/driver-failure reporting gate remains open;
this is not a general transaction rollback or device-loss implementation.

## Test setup corrected without weakening pixel assertions

Initial full runs `background-full-gl-20261020/` and
`background-full-vk-20261020/` FAIL old probes that expected black underlay while
their helper painted only screen pixels. Their actual GDI backing remained
white, so correct composition exposed white and blended translucent blue over
white. All original expected RGB values were preserved. Scene initialization
and black() now paint the actual application HWND through GetDC(hwnd), check
initial paint success and no longer manufacture a screen-only background.
The manually constructed second target also receives an explicit black GDI base.

`background-explicit-gdi-full-gl/` and `background-explicit-gdi-full-vk/` then
PASS all 20 groups on both architectures. The new background group covers IGNORE
and PREMULTIPLIED modes, moves, opacity over green, repeated retained commits,
clip contraction and a changed blue GDI backing on subsequent Present. Exact
RGB values remain required; no broad tolerance or observational message pump.
This fixes the setup precondition, not a claimed automatic repaint/expose path.

`background-webgl-20261020/`: actual cached WebView2 still PASSes strict WebGL1/2
default/FBO red pixels, complete status, no errors/loss or observed GPU/Skia
fatal/crash signatures; 20 samples / 306 frames. Software-driver diagnostic only.

## Final-leaf teardown and prepared-scene interaction

`background-detach-negative-gl/`: both PEs FAIL when SetContent(NULL) exposes
black instead of green. The extended case also checks SetRoot(NULL), reattach
and target destruction, using actual target-window pixels.

A first repair prepared/published an empty scene through the normal API before
clearing. `background-detach-full-gl/` and `background-detach-full-vk/` correctly
FAIL shared-content, multi-target, interpolation and detach cases: that approach
overwrote a next scene that Commit had already prepared. It was not committed.

The corrected path stays inside WineD3D's active-scene revocation. After draining
the CS, render only the active underlay with layer_count temporarily zero, use
retained-only delivery (no producer capture or rotation), then restore the count
for resource cleanup. The next prepared scene is neither replaced nor consumed.
DXGI no longer performs an extra clear-to-black after this revocation. Revoking
a dead target still releases resources rather than leaving a live binding.

The retained-only delivery must explicitly name the active application HWND:
a NULL destination means the producer's hidden device window, NOT the current
composition target. That mistake was exposed by the detach pixel regression.

### Additional discriminating failures (all retained, not acceptance)

- `background-detach-final-{gl,vk}/`: preserving prepared state alone still
  leaves red pixels; the restore was delivered to the hidden producer window.
- `background-detach-target-{gl,vk}/`: explicit target fixes basic detach, but
  resize/multi-target teardown still feeds red back into its own underlay.
- `background-detach-backing-diagnostic/`,
  `background-separated-surface-diagnostic/`: failure diagnostics read red from
  both application and whole-window DC, even with registration still live.
- `background-private-underlay-build.log`: first native build FAILed because
  L-string literals have 32-bit wchar_t on the Unix compilation side. Explicit
  WCHAR arrays fix the type mismatch; build2 succeeds. No cast hides it.
- `background-separated-{gl,vk}/`: selecting GDI backing in win32u alone is
  insufficient: X11 still discarded the actual backing after resize.
- `background-resize-stage-diagnostic/`, `background-resize-wine-trace/`:
  backing is black before/after resize and after WM messages, then red after
  the resized Present. The window trace proves the surface became NULL.
  `enable_direct_drawing()` selected this because a GL client window existed.
- `background-x11-surface-{gl,vk}/`: backing retention fixes the stale-red
  multi-target teardown. Two old tests now expose white in the NEW area of a
  larger window, where they never established their expected black background.
  Their setup now paints ONLY that new area, explicitly excluding the old
  client rectangle; it cannot hide failure to preserve old backing pixels.
  All original expected RGB values remain unchanged. The new colored-background
  case additionally checks resize and detach without repainting the old area.
- `background-final-21-{gl,vk}/`: all original groups and colored resize pass,
  but stronger target recreation exposes white instead of saved green. Dropping
  backing policy with the last target discards the application image. Making
  the backing policy HWND-owned (while freeing the two live target slots) fixes
  this; `background-window-life-21-{gl,vk}/` passes all 21 groups, both PEs.
- A read-only attempt to open root `STOCK_CHANGES.md` failed (file is under
  `docs/`); file discovery located the correct report. Two `rg` diagnostic
  lookups used nonexistent old layout names (`blitter.c`, `client_surface.c`,
  `prop.c`, `dc.c`); `rg --files` / actual symbol searches located the current
  files. No missing file or grep-no-match result was treated as test evidence.
- `background-window-life-frozen/` was rejected by argument validation because
  --freeze-scene requires --electron-app, not a WebView2 host. The corrected
  `background-window-life-frozen-electron/` run succeeds and its actual screen
  image was inspected privately. A hash command used the historical native name
  winex11.drv.so; find located winex11.so and the corrected hash succeeds. A log
  lookup used nonexistent host.stderr.log; listing identified wine.stderr.log
  and results.json. These are tooling failures, not acceptance runs.
- A combined documentation patch failed context verification on the status
  heading; inspection confirmed no edits applied and exact-context retry worked.

## Verified checkpoint (software controls only)

- `background-window-life-21-gl/`, `background-window-life-21-vk/`: all 21
  required groups PASS on both PEs. Includes colored underlay, opacity, repeated
  retained commits, changed GDI painting, clip, move, resize without repainting
  old pixels, final content/root detachment, second unused layer, duplicate
  layer rejection and target recreation. Neither producer nor expected RGB
  assertions were weakened. Vulkan uses /usr/share/vulkan/icd.d/lvp_icd.json.
- `background-hresult-detach/`: final documented duplicate HRESULT regression
  PASS both PEs after correcting the initial generic error result. Public
  [error contract](https://learn.microsoft.com/en-us/windows/win32/directcomp/directcomposition-error-codes)
  used for behavior; no implementation copied. Same-process ownership enforced.
- `background-window-life-g0-g3/`: baseline G0-G3 + 11 ordering groups PASS both
  PEs, including ordinary non-DComp HWND animation/resize. No FPS acceptance.
- `background-window-life-pacing/`: all five pacing groups PASS both PEs.
- `background-window-life-webgl/`: strict real WebGL1/2 default/FBO pixels PASS,
  20 samples / 568 frames, no observed GPU/Skia fatal/crash signatures.
- `background-window-life-electron/`: original Electron lifecycle all seven
  checks PASS, 34 samples / 626 post-reload frames, no observed GPU/Skia fatals.
- `background-window-life-frozen-electron/`: 24 samples / 1088 frames; actual
  screen-18 inspected: one blue square at x=160, correct frozen text, uniform
  cream content background, controls and scrollbar intact. No internal-only
  capturePage substituted for the front-window image. Private image stays out
  of Git and the ISO.

All prefixes, hashes and logs remain under /var/tmp/wex-g4-tree-20261020; use the
per-run filesystem-socket launcher from the status report. Runners clean only
their own Wine server and Xvfb; no owned test session is intentionally left up.

## Outstanding boundaries

Automatic repaint/expose WITHOUT a new Commit/Present is still unproven. The
next test must dispatch an actual application WM_PAINT and obscure/expose the
window without letting the pixel reader itself pump messages or trigger repair.
Full soft-border/AA, two target layers/child ordering, DirectX underlays, broader
publication faults, R2/R3 and actual Classic/Luna/X11/Wayland/physical-GPU matrix
remain open. No master merge, sandbox bypass or OOBE/token import is authorized
by these functional controls. Proprietary runtime/SDK/screenshots stay private.
