<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Gemini work order: complete G4 on the independently repaired baseline

Date: 2026-10-19. Handoff branch in parent and Wine: codex/g4-correctness.
Update 2026-10-20: the interrupted Gemini R1 reference slice was repaired and
published on codex/g4-r1-completion (parent and Wine). Read
CHROMIUM_GRAPHICS_G4_R1_REPAIR_REVIEW.md and its exact gitlink/evidence before
continuing; use that newer baseline. Seven-group pixel controls now pass on both
architectures with software GL/Vulkan, but the full R1 gate below is NOT complete.
Do not reintroduce mutable-backbuffer/raw-pointer scenes or remove the remaining
honest rejection guards to satisfy an old HRESULT-only probe. The report lists
the precise unimplemented features and NOT RUN acceptance cells.
Start from the published parent commit containing this file and its exact Wine
gitlink. Read CHROMIUM_GRAPHICS_G4_REPAIR_REVIEW.md first. This is G4 completion,
NOT authorization to label G4 done or move directly to G5.

## Ownership and unchanged constraints

Create your own new branch/worktree from the complete published handoff; do not
switch/reset an occupied checkout. Use your own native Linux source/build,
owned prefixes, displays and process groups. Do not use the repair lab or another
worker's build. Do not restart/reinstall existing VMs, alter global X sockets,
use global killall/pkill, import token/OOBE changes, dispatch agents, or merge
master. Publish Wine before the parent gitlink. Push both branches at handoff.

Preserve LGPL notices/authorship in Wine and GPL-2.0-or-later compatibility in
new project code. GPLv3-only implementations may inform design but may not be
copied without a separate licensing decision. Keep proprietary runtimes, font
files, vendor legal documents and XP reference captures outside Git/ISO.

Original scope remains CHROMIUM_GRAPHICS_PLAN.md and G4_WORKORDER.md.
Vulkan is optional; older Intel HD OpenGL-only machines remain first-class.
Application composition must use Wine's existing window delivery path, not an
extra desktop compositor or overlay. Physical hardware results, software GL/Vulkan
controls and normal installed desktops are separate evidence categories.

## What this baseline does and does NOT establish

The repair removes PulseEvent, provides persistent per-imported-observer
notifications, makes supported bindings direct private COM calls, and tests
actual target-client screen pixels rather than only HRESULTs. It fixes stale
drawable pixels in the deliberately OPAQUE single-content slice.

The current renderer does not implement general retained multilayer composition,
non-unity group opacity, rounded clips, general matrix rotation/shear/reflection,
or background-preserving transparency. Those cases reject with E_NOTIMPL where
guarded. The whole G4/browser route is not certified. Do not remove rejection
guards before a renderer and discriminating positive pixel assertions exist.

The private IWineDCompSwapChain interface is builtin-only. DXVK does not acquire
it merely because it implements public DXGI. Direct binding synchronizes supported
state application with Present; it is not proof of physical scanout completion.
The opaque clear path paints outside a leaf black; it is not an alpha compositor
over arbitrary pre-existing application content.

## Gate R0 — reproduce the exact baseline and keep useful tests

Build combined WoW64 with --enable-archs=i386,x86_64 and both MinGW C++ compilers.
Record source SHAs, configure options, compiler/driver versions and binary hashes.

Run BOTH architectures of:
- check-g3-correctness.py --wine-build BUILD --output-dir NEW --full-suite
- check-g3-correctness.py --wine-build BUILD --output-dir NEW --g4-correctness
- Optional software Vulkan control with --renderer vulkan --vulkan-icd EXACT_JSON.

Use an owned Xvfb and private mount namespace when WSLg's socket mount is
read-only. No inherited Wine override/prefix or installed runtime fallback.
Existing upstream d3d11/dxgi tests remain mandatory with a recorded baseline;
missing binaries or disabled tests are NOT passing suites.

Keep the new placement, stale-pixel clearing, pending-versus-committed placement,
detachment, no-message-pump cross-thread completion, 60-frame changing-pixel and
imported-observer/child-process assertions. The unsupported-property test is a
temporary truthful capability boundary, not a permanent requirement to reject:
when implementing a feature, replace its rejection assertion with stricter
positive pixel assertions, plus invalid-input and failed-update-state checks.
Do not accept either S_OK or E_NOTIMPL as an interchangeable PASS.

## Gate R1 — real retained composition, not independent window blits

Implement coherent retained target surfaces/trees and presentation scheduling.
Tests must show:
1. Two independently changing content leaves maintain tree z-order regardless
   of the order their Present calls arrive. Re-presenting a lower leaf cannot
   erase an unchanged upper leaf.
2. Opaque replacement, detach, move, resize, clip contraction/expansion, removal,
   reparenting and root replacement restore the correct underlying content.
   Do not clear to black where another retained layer or application content
   should be visible.
3. Premultiplied content alpha and effect-group opacity actually blend; cover
   LAYER versus MULTIPLY group behavior and inherited opacity. Example: an opaque
   red background and 50%-opacity blue layer yield the specified blended color,
   not pure blue. Specify color space and a small justified tolerance.
4. Rectangular and independently rounded-corner clips mask pixels correctly,
   including nested transformed clips and live clip-object updates only at Commit.
5. All accepted affine matrix terms affect geometry, including rotation/shear,
   negative scale/reflection and parent transforms. Validate finite coordinates,
   arithmetic and descriptor bounds; reject genuinely unsupported interfaces.
6. Edits do not leak before Commit. One transaction applies coherently across
   leaves/targets or fails without partially changing active state. Successful
   WaitForCommitCompletion cannot depend on a blocked swapchain-owner message pump.
7. Once committed, changing frames continue without re-committing. Cover first
   frame, nested child HWND, unattached/detached content, minimize/restore,
   occlusion, resize, content-sharing references, destruction and device loss.

Use original deterministic nonuniform patterns and actual target-window pixels.
Staging texture/CDP screenshots and successful Present are insufficient. A screen
read must use the HWND's actual ClientToScreen coordinates and confirm visibility.
Do not use a broad color tolerance, an unchecked setup branch, or foreground
overlap from a different test window to turn the pixel oracle into a false pass.

## Gate R2 — synchronization and lifecycle proof

The shared-observer registry currently has an explicit 64-observer capacity.
Do not hide exhaustion or silently drop notifications. If changing its layout,
version and validate shared descriptors, and test creator/importer compatibility.

Add a deterministic regression for signal during observer registration/rearming,
not only probabilistic stress. Test multiple processes with different timeline
thresholds, rapid/coalesced signals, repeated values, late observers, creator exit,
duplicate/close/re-export, process death, slot reclamation, callback teardown and
allocation/registration failures. No PulseEvent, single auto-reset broadcast event,
or permanently signaled manual-reset event with a hot callback loop.

Preserve asynchronous unsignaled Wait ordering for draw/clear/dispatch/query/copy.
Test cached GPU sampling as well as CPU readback, keyed-mutex and fence-only
handoffs, multiple resources/contexts and immediate releases. Do not call all
G3 entry gates complete from the inherited limited eleven-case probe.

Record bounded idle CPU, no leaked mappings/events/KMT handles/COM references,
and no stale pointer use when Commit/Present/resize/release run concurrently.
A source review or absence of crashes in one run is not a lifetime proof.

## Gate R3 — older-hardware delivery and real consumers

Keep correct software fallback explicitly labeled. Replace mandatory readback
and conservative per-signal full-GPU waits in the accelerated path with supported
Linux/backend image sharing and completion-driven synchronization. Measure copies,
memory and stalls; iGPU shared RAM is not itself zero-copy evidence. Specify a
coherent private-app DXVK stack and implement/test its composition interop before
advertising it; do not replace machine-global graphics DLLs.

Pin one maintained Chromium-derived runtime, then separately Electron/CEF and
the WebView2 host. Record executable hashes/versions, source/DEPS, child command
lines, DLL origins, actual renderer/backend and timestamps correlated with pixels.
Verify continuing DOM/CSS, canvas, WebGL, input, scrolling, resize/minimize/restore,
restart and profile persistence in default Windows10/11 mode.

No --no-sandbox, --single-process, GPU sandbox disable, AppContainer/Win32k bypass,
version spoof or software flags may silently enter default acceptance. Keep
diagnostic fallback runs separate. Report token-security blockers without importing
another worker's unreviewed branch or claiming rendering proves containment.

## Gate R4 — required desktop/physical matrix, then handoff

Run ordinary 32/64-bit apps and actual X11 outer-composition OFF/ON plus real
Wayland/wexcomp sessions. Compare against a named baseline:
Classic/Open Luna, actual 800x600 and normal resolution, taskbar locked/unlocked,
toolbar grippers/separators/overflow, tray chevron expanded/collapsed/absent,
Start menu/dialogs, desktop effects and hosted native Linux/Wine child windows.
Unchanged uxtheme source is NOT evidence that desktop behavior is unchanged.

Hardware cells: older Intel OpenGL-only, Intel with usable Vulkan, low-end AMD/
Nvidia, VM/virtual GPU, llvmpipe/lavapipe. Record actual adapters/drivers/features.
Unavailable hardware stays NOT RUN, not a blanket PASS or a fabricated result.

Replace the historical acceptance claims with a case matrix: PASS/FAIL/BLOCKED/
NOT RUN, exact commands/exits, source/binary hashes, architecture, renderer,
display/theme/resolution, expected/actual pixels, logs/captures, CPU/memory,
baseline differences and cleanup. Preserve failures and exact workarounds.
Publish reviewable commits, both branches, report path and current VM/process
state. Do not merge master. G5 video/HDR/protected content/WebGPU and G6 release/
security remain separate gates after actual G4 acceptance.
