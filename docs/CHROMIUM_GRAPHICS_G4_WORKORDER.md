<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Gemini work order: G4 composition, with mandatory G3 integration acceptance

Date: 2026-10-18. Handoff branch: `codex/g3-correctness`.
Start from the published commit containing this document and its pinned Wine
repair, not the earlier `5331435a` submission. Read the companion
`CHROMIUM_GRAPHICS_G3_REPAIR_REVIEW.md` for exact revisions, commands and results.
**This document is an implementation order, not a declaration that G4 passed.**

## Scope and ownership

Use a separate available branch such as `gemini/chromium-graphics-g4`, based on
the complete published repair. Keep the useful G0-G3 implementation and retain
the new regression assertions unchanged. Record full parent/Wine/compositor SHAs,
compiler versions, configure options and binary provenance for every test run.
Publish Wine commits on your own branch before publishing the parent gitlink.

Read `CLAUDE.md`, `CHROMIUM_GRAPHICS_PLAN.md`, research, G0/G2/G3 orders, acceptance
history, this repair report, and relevant DWM/Wayland ownership documentation.
The original plan controls scope; earlier blanket PASS captions are not proof.
Do not switch/reset another occupied checkout, reuse someone else's build tree,
modify another agent's WSL distro, force-push, merge/push master, dispatch agents,
or alter the security-token/OOBE implementations. Use owned isolated prefixes,
displays and process groups. No global killall/pkill or deletion of X sockets.
Never reset/reinstall an existing VM without specific designation.

Program changes must remain GPL-2.0-or-later compatible, and Wine changes retain
LGPL notices/authorship. GPLv3-only code may inform design but must not be copied
without a separate licensing decision. Keep proprietary binaries, source, fonts,
art and legal documents out of Git and the ISO. Reference screenshots are for
observation, not copied/traced art. Preserve supported Classic/Open Luna behavior.

## Entry gate: prove G3 behavior, not just successful HRESULTs

The independent repair replaces the unconditional Texture2D cast in CopyResource
with resource-type guards, avoids construction-time DXGI queries for unrelated
resource interfaces, and places pending fence waits into WineD3D's actual
command stream. Signal completion uses a conservative GL/Vulkan completion barrier.
This is a correctness implementation, not an efficient zero-copy GPU path. Shared
textures still use the inherited CPU/readback transport. Follow the report's exact
tested scope; do not relabel a software Xvfb run as physical acceleration.

Before broad G4 changes:

1. Build combined WoW64 (`--enable-archs=i386,x86_64`) in your own native-filesystem
   directory, with both MinGW C++ compilers. Run BOTH PEs of
   `scripts/test_g3_ordering_probe.cpp`, not merely compile them. Use
   `scripts/check-g3-correctness.py --wine-build ABSOLUTE_BUILD --output-dir NEW_DIR
   --full-suite` in an appropriate Linux test environment. Also rerun existing
   G0-G3 probes and Wine d3d11/dxgi tests. Missing test executables are failures,
   never empty successful suites. Identify genuine upstream/baseline failures.
   On WSLg, use an owned socket directory in a private mount namespace if the
   global X socket directory is read-only; never alter/delete global X sockets.
2. Prove ordinary Buffer, Texture1D, Texture2D and Texture3D copies still work.
   The added probe covers Buffer, Texture1D and Texture3D plus a 2D clear readback;
   extend coverage, don't remove them.
   Keep non-shared copies on the normal WineD3D path.
3. Queue Wait on an UNSIGNALED fence, then Clear/Draw/Dispatch/End/Flush/Copy on
   the consumer. Wait and ordinary enqueue operations must return without waiting
   on the CPU. Commands and event-query completion must not pass the barrier;
   release from another device/process and verify correct resulting pixels/data.
   The old test's Wait after completed value1 did not exercise this case.
4. Validate Signal after genuinely delayed GPU writes, including no shared textures,
   several render targets/swapchains, GL context switches, Vulkan where supported,
   repeated timeline values, more than one fence observer/process, and immediate
   object release. Deferred contexts and unsupported non-threaded configurations
   must reject honestly rather than claim a successful no-op or CPU-blocking Wait.
5. Exercise handle duplicate/close, creator exit, imported re-export, named handles,
   process death and device loss. Verify fence/device/callback references, KMT and
   OS handles, view/mapping ownership, failed allocation cleanup and bounded idle
   CPU. In particular audit multiple waiters on the inherited shared-event design;
   a one-observer probe is not broadcast-notification proof.
6. Repeat producer/consumer sharing with changing patterns, not just one green
   frame. Verify BOTH staging readback and actual sampling/rendering by the other
   GPU device, after it has already cached/used the texture. Test keyed-mutex
   release/acquire and fence-only handoff independently. CPU mapping readback must
   not hide a stale GPU texture; a consumer Signal must not publish stale data over
   a producer's newer image. Audit shared-memory ownership/double-unmap/close.
7. Limit accepted resource formats/layouts to implemented ones. The inherited
   width*4 transport is not support for arbitrary formats, mip/array/MSAA layouts
   or planes. Validate descriptors, arithmetic, pitches and allocation bounds;
   reject unsupported cases before publishing handles. Do not fabricate textures
   with guessed dimensions/format for an unrecognized resource descriptor.
8. Trace the selected browser's actual SharedImage and synchronization calls and
   demonstrate progress through them. Record child command lines/DLL origins and
   distinguish the software fallback route from genuine D3D11/Viz resource sharing.

Any entry-gate defect gets an isolated regression and repair commit first. Keep
G3 partial until its required coverage is evidenced. This is not permission to
hide gaps by disabling the browser sandbox, spoofing an old Windows version,
returning fake capabilities, or weakening assertions. If a supported policy cannot
be implemented yet, document the exact blocked route and keep its claim off.

## G4A — retained composition semantics and correct pixels

Extend the minimal application-level DComp slice through Wine's existing window
delivery path. Do not add a second desktop compositor/overlay to make it visible.

- Implement the selected consumer's required tree operations: insertion/removal,
  sibling order, reparenting, offsets, matrix transforms, rectangle/rounded clips,
  alpha/opacity, replacement/detach and shared content references. Do not confuse
  SetOpacity with SetOpacityMode. Unsupported interfaces must fail accurately.
- Prove pending edits do NOT take effect before Commit, then apply atomically on
  Commit. Commit once and present changing frames afterward without re-committing.
- Cover first frame, unattached swapchain, nested/child HWND, resize, minimize/
  restore, destroy ordering, shared references, occlusion and device loss. Implement
  surface BeginDraw/EndDraw/update and virtual-surface behavior only as exercised,
  with correct failure behavior for unsupported cases.
- Use original deterministic patterned probes that check actual target-window
  pixels, overlapping alpha/clip/z-order and multiple frames. Checking only an
  intermediate texture, a CDP screenshot or successful Present is insufficient.

Entry/exit evidence: both PE architectures; X11 outer desktop composition OFF
and ON; an actual Wayland/wexcomp session. Inspect input hit-testing, focus,
scrolling and resize while animating. Keep application composition, driver
rendering and outer desktop composition separately identified in each result.

## G4B — efficient Linux-backed delivery, older hardware first

Our primary target includes older Intel HD iGPUs and low-end consumer AMD/Nvidia
GPUs. Vulkan is optional, not a new minimum requirement. Keep the builtin
WineD3D/OpenGL route working on machines that lack a usable selected Vulkan stack.
Use Linux's existing driver capabilities rather than advertising features that
the actual device/backend cannot provide.

- Replace mandatory full-frame CPU readback/copies in the accelerated path with
  real image sharing/composition supported by the backend. Investigate appropriate
  GL sharing/export/import and Wayland/EGL/DMA-BUF dependencies; pin and document
  any borrowed code. Shared iGPU RAM does not itself establish cache coherence or
  zero-copy. Measure transfers, memory and synchronization instead of assuming.
- Preserve an explicit correct CPU/software fallback. Identify exactly which
  operations copy/read back and which execute on the physical GPU. Do not promote
  a working CPU mapping transport into a hardware-acceleration claim.
- Replace the conservative per-signal completion/readback stalls with ordered
  backend fences/timelines as supported, retaining the new unsignaled-Wait tests.
  Use event/completion-driven pacing; no continuously spinning or polling thread.
- Define DXVK composition-swapchain interoperability explicitly. Builtin WineDXGI
  private interfaces are not automatically implemented by native DXVK. Test the
  coherent pinned d3d11/dxgi/d3d10core stack in a private per-user/app view; no
  machine-global replacement of builtin graphics DLLs. If patches/upgrades are
  needed, stage and review them separately, preserving the OpenGL route.
- Implement correct buffer queueing, frame-latency waits, visible/minimized/
  occluded pacing, wakeups and meaningful statistics. Don't report loop iterations
  as displayed FPS. Zero-copy and efficient hardware composition are measured
  results, not compulsory labels for every supported older device.

Hardware matrix: available versus actually tested older Intel OpenGL-only,
Intel with usable Vulkan, representative low-end AMD/Nvidia, VM/virtual GPU,
llvmpipe/lavapipe. Record adapter/vendor/device IDs, driver versions/extensions,
backend, memory limits and missing features. A modern RTX pass does not validate
old Intel. Missing physical hardware leaves that cell pending, not fabricated.

## G4C — real consumers and desktop regressions

Pin executable hashes/versions and corresponding Chromium source/DEPS. First
validate one maintained Chromium-derived runtime in default Windows10/11 mode;
then representative Electron/CEF hosts and the existing WebView2 host. One does
not validate the others. Use an existing lawfully supplied runtime, and obtain
appropriate authorization before any new vendor acquisition/terms acceptance.

Require visible continuing UI, DOM/CSS, 2D canvas and WebGL, correct keyboard/
mouse input, scrolling, resize, minimize/restore and restart/profile persistence.
Record actual target-window pixels and input/state assertions, not just a green
caption saying sandbox active. Correlate browser/GPU/renderer trace timestamps
with the frame evidence and DLL load origins. Repeated process creation or zero
matched error signatures alone do not establish a functioning consumer.

Diagnostic --disable-gpu, --disable-direct-composition, SwiftShader or version
overrides must be separately labeled. They cannot prove the default accelerated
DComp route. No --no-sandbox, --single-process, disabled GPU sandbox, AppContainer
or Win32k policy bypass may silently enter acceptance. Token/security fixes remain
a separate workstream; report the exact blocker rather than importing its
unreviewed implementation or calling rendering proof of containment.

Regression matrix on X11 and real Wayland: ordinary 32/64-bit apps; Classic/Open
Luna at 800x600 and normal resolution; locked/unlocked taskbar; toolbar grippers/
separators/overflow; system-tray show/hide chevron expanded/collapsed/absent;
Start menu/dialogs; desktop effects, captures, native Linux and Wine child windows.
Check DWM/outer-compositor ownership, not double redirection. Compare against an
identified baseline and retain inherited differences separately from regressions.

## Acceptance, limits and handoff

Maintain a case matrix with PASS/FAIL/BLOCKED/NOT RUN, exact commands/exit codes,
source and binary hashes, architecture, adapter/backend/display/theme/resolution,
expected/actual pixels, trace/log locations, CPU/resource measurements and cleanup.
Measure idle, continuous animation, resize, occluded/minimized and repeated
create/destroy runs. Preserve failed commands and exact workarounds. State which
results are native Windows controls, extracted fixtures, software Wine controls,
physical hardware and installed-image tests; never combine them into a false total.

Deliver `docs/CHROMIUM_GRAPHICS_G4_ACCEPTANCE.md`, update STOCK_CHANGES and the
existing matrix truthfully, and identify remaining G3/G4 limitations. Supply open
probe sources, reproducible safe runners, redacted artifacts and current VM/process
state. Keep private runtime binaries, proprietary documents and XP captures out
of Git/ISO. Make clean, reviewable commits and **push both Wine and parent branches**.
Hand back exact branch names/SHAs, report path and pending gates. Do not merge master.

G5 video/overlays/HDR/protected content/WebGPU and G6 security/packaging/release
remain separately gated. Do not broaden this stage to a whole Proton stack,
unreviewed token rewrite or a mandatory Vulkan-only desktop.
