<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 R1 border coverage checkpoint — 2026-10-21 UTC

Branch: `codex/g4-r1-completion`. Parent main `fe009f21`, parent Wine
`bc7011fa4785383988faced54ff7d3e496a95c49`. This extends the verified repaint
checkpoint, not a declaration that G4 or native-Windows visual fidelity is done.
Qwen's OOBE checkout, token work, and VM sessions were not changed. Published
Wine implementation commit: `61929ac` on the same branch.

## Public contract and implementation

Primary references inspected:

- [SetBorderMode](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionvisual-setbordermode)
- [Border mode enumeration](https://learn.microsoft.com/en-us/windows/win32/api/dcomptypes/ne-dcomptypes-dcomposition_border_mode)
- [CreateTargetForHwnd](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice-createtargetforhwnd)

Border mode controls transformed bitmap/clip edges and rounded clip corners,
independently of bitmap interpolation. The initial value is INHERIT; absent an
explicit ancestor setting, the tree uses HARD. A child override does not remove
an ancestor's own clip. The documented destination-invert exception is not a
reason to alias ordinary SOFT visuals: destination-invert remains rejected by
the existing implementation rather than silently approximated.

The old SetBorderMode accepted everything, retained nothing, and rendered hard
coverage only. New pending/active state validates the enum, resolves inheritance
while collecting the pending tree, and travels through private DXGI/WineD3D
scene descriptors. Publication still uses the existing all-target preparation
transaction. No state is published at setter time or by producer Present.

The reference compositor uses an original 8x8 target-pixel coverage integration
only at conservatively detected SOFT boundaries. HARD geometry is tested at the
pixel center even when another node is SOFT. Each sample traverses the existing
isolated-opacity groups, correlating coincident ancestor/child boundaries rather
than multiplying independently averaged masks. Bitmap filtering remains sampled
at the target pixel center, with boundary texels clamped; nearest and linear
filter selection are not replaced by supersampling the texture. All-HARD scenes
skip boundary detection. Fully interior/exterior convex regions avoid 64 scene
traversals. The narrow integer-aligned opaque GL path stays eligible: those
bitmaps have no partial edge coverage to resolve.

This is a bounded correctness reference, NOT an efficient GPU AA implementation,
zero-copy/power claim, or exact Microsoft rasterizer reproduction. Native pixel
comparison, broader clip-shape validation and R3 performance work remain open.
No Microsoft source, artwork, runtime or SDK was added to Git or the ISO.

## Discriminating tests and results

`border_mode_transactions` first failed on the unmodified parent binaries on
both PEs: fractional left edge expected red191, observed red255. Its final form
checks root HARD default; pending isolation across Present; SOFT bitmap left and
right boundaries; invalid enum rejection without state loss; parent inheritance;
explicit child HARD and reset to INHERIT; fractional clip coverage; SOFT ancestor
clip with HARD child; oversized-target preparation failure preserving active
border state, followed by successful retry of pending border edits.

`border_mode_geometry` checks an affine-rotated bitmap boundary and a rounded
clip. New curved/diagonal coverage assertions have explicit numerical bounds:
half-area diagonal red128 +/-16 (8x8 boundary quantization), and circle radius8
at pixel [2,3]x[2,3], independently integrated area giving red193.678, expected
red194 +/-4. Outside and interior controls and the HARD rounded-corner control
remain exact. Existing RGB assertions retain tolerance0. This does not assert
that Windows uses the same coverage grid or exact curved-edge pixels.

Evidence root: `/var/tmp/wex-g4-tree-20261020/` in Debian WSL. Fresh output dirs:

| Directory | Verified scope |
| --- | --- |
| border-before | Both PEs fail original fractional-edge test as above |
| border-after-gl | First transaction test passes both PEs |
| border-geometry-gl | Geometry test passes both PEs |
| border-final-24-gl | All24 correctness groups PASS both PEs, software GL |
| border-final-24-vk | All24 correctness groups PASS both PEs, lavapipe Vulkan |
| border-final-g0-g3 | All11 ordering groups and G0–G3 PASS both PEs |
| border-final-pacing | All5 pacing groups PASS both PEs |
| border-final-webgl | Real WebView2 strict WebGL1/2 default/FBO red pixels PASS |
| border-final-electron | Real Electron input/scroll/resize/minimize/restore/reload/resize-back all7 PASS |
| border-final-frozen | Actual screen-18 image visually inspected, not capturePage |

WebView2:21 samples,308 frames; both contexts available, no loss/errors, complete
FBO, exact red RGBA default/texture samples. Electron lifecycle:34 samples,372
post-reload frames. Frozen:24 samples,1076 frames; inspected front-window image
shows one blue square at x160, correct frozen text, uniform cream background,
input/button and scrollbar. No observed GPU/Skia/fatal/crash signatures in these
scoped runs. Concurrent runs are correctness checks, not comparable benchmarks.
Original fixture and lawful private runtime identities match the prior report.

All result cleanup records have empty remaining-server lists and successful
waits. Some kill commands returned1 because the owned server had already exited;
that is not an ignored remaining process. No global Wine/Xvfb cleanup was used.

## Reproduction

Owned native source `/var/tmp/wex-g3-repair-20261018/clean-source`, Wine under
`wine/`, combined WoW64 build `build/wow64`. Scoped rebuilds used `make -j6
dlls/dcomp/all dlls/dxgi/all dlls/wined3d/all`; final WineD3D-only optimization
was rebuilt before all final runs, never while a probe used the build.
Logs: `border-build.log`, `border-build-final.log` under the evidence root.

Every run used `scripts/run-isolated-graphics.py --socket-root
/var/tmp/wex-g4-tree-20261020/isolated-sockets --` before the Python runner,
unique output dirs, fresh owned prefixes, and unique filesystem X sockets.
For correctness use `check-g3-correctness.py --wine-build BUILD --output-dir NEW
--g4-correctness`; Vulkan adds `--renderer vulkan --vulkan-icd
/usr/share/vulkan/icd.d/lvp_icd.json`. Focus a test with `--g4-case NAME`.
Baseline uses `--full-suite`; pacing uses `check-g4-pacing.py`.
Consumer reproduction and private runtime paths are in the prior status report;
the same arguments were used with new border-final-* outputs, WebView2
`--graphics-diagnostic --seconds 25`, Electron `--lifecycle --seconds 35`, or
separate Electron `--freeze-scene --seconds 25`.

Final binary SHA-256:

| Artifact | SHA-256 |
| --- | --- |
| x86_64 dcomp.dll | caab6e3a57e28c3ae06428fb8798f06e2cf37e2118da9c7af1f08f9b1343efde |
| i386 dcomp.dll | 9d68dd8157ac81c79d9b875cf08f89812933d63425b889218b27807404daa988 |
| x86_64 dxgi.dll | b01d1ad86a7375ef5c08d353d913b65841e2f7cf7635e7d2a913fb2d4968779d |
| i386 dxgi.dll | 98360afc0c4700cfba1f7c5e8fd4e0662fabecbb94336495035b6c92b337b65a |
| x86_64 wined3d.dll | 70c308637a3a2df97b044be0ac5a8da6c209da31f32101e7f386cdce1016322d |
| i386 wined3d.dll | 3a85e37ec9d368da47beb50c8e83a15d106ccaef79a403c23a3a5416f9202adf |

## Open gates and continuation

G4 remains INCOMPLETE. Next R1 integration needs discriminating four-layer HWND
tests: application GDI/DirectX base, bottom composition tree, ordinary child
HWND, top composition tree. Source inspection shows `topmost` currently drives
registration ownership but does not reach rendering descriptors; do not infer
correct child-window clipping from the duplicate-target test. Prove both layers
present simultaneously, independent Present, child painting/movement and release
of either target. Check top-level/child/layered and foreign-thread startup too.

Continue R2 deterministic race/fault/lifetime proofs and R3 efficient backend
sharing/completion/private interop. Inherited upstream DXGI67 failures and D3D11
legacy-sharing exit5 were not rerun here and remain OPEN, not waived. Actual
Classic/Open Luna, X11 compositor OFF/ON, Wayland/wexcomp, resolutions/taskbar/
toolbar/tray cells and physical older Intel/AMD/Nvidia acceptance remain NOT RUN.
WSL still lacks physical /dev/dri; synthetic GTX470 branding is not GPU proof.
Sandbox flags are not security-containment evidence. No master merge.

Failure notes: initial multi-file test patch failed exact-context verification
at a combined Python marker line and made no edits; reapplied against its actual
line successfully. The intentional pre-fix regression failed both architectures.
Builds and all final controls above succeeded. One-time account reset credit
remains untouched.
