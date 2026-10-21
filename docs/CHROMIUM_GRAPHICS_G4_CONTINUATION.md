<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 completion progress — 2026-10-20

Latest: `CHROMIUM_GRAPHICS_G4_BORDER_VERIFIED_20261021.md` records R1 border
implementation and 24-group verification. Earlier border-stub descriptions are
historical. G4 is still incomplete; next is four-layer HWND integration.

Branch in both repositories: `codex/g4-r1-completion`. G4 is **in progress**;
this report supersedes only the explicitly repaired limitations in
CHROMIUM_GRAPHICS_G4_R1_REPAIR_REVIEW.md, not its remaining acceptance gates.

Current concise matrix: CHROMIUM_GRAPHICS_G4_STATUS_20261020.md. For NEW parallel
runs use run-isolated-graphics.py with fresh per-run filesystem sockets; the
older shared-socket invocations below are historical and must not be reused in
parallel (a cross-server filesystem-socket collision was reproduced).

## Hierarchical reference composition

Wine `736b2b0` retains contentless ancestors, transformed nested clip masks and
isolated LAYER opacity. MULTIPLY propagates opacity per surface, while LAYER
composes a subtree at unit opacity and blends the result once. Scene traversal
is bounded to 128 visuals / 32 content entries and validated before publication.
Isolation uses per-pixel stack state rather than a full-window allocation for
every group. This is still CPU readback/reference composition with nearest,
binary sampling, not the required accelerated delivery path.

The changed private scene layout has a new builtin-only binding IID:
`37fbf4a2-2c4d-47c0-a2ec-c7716aa35ca1`. Matched DLLs are required. DXVK does
not implement this interface automatically. Newly created rectangle clips are
unbounded instead of incorrectly empty. API behavior was checked against:

- [Microsoft clipping contract](https://learn.microsoft.com/en-us/windows/win32/directcomp/clipping)
- [Opacity modes](https://learn.microsoft.com/en-us/windows/win32/api/dcomptypes/ne-dcomptypes-dcomposition_opacity_mode)

## Verified checkpoint

Owned native WSL source/build reused from the previous repair:
`/var/tmp/wex-g3-repair-20261018/clean-source`, build `build/wow64`.
Combined WoW64, both MinGW compilers; initial configure remains
`../../wine/configure --enable-archs=i386,x86_64 --prefix=/usr/local --disable-tests`.
Software GL environment/driver details remain those in the previous report.
No physical hardware/browser/theme claim is made from Xvfb.

Exact test invocation (inside Debian, root-owned isolated test lab):

```sh
unshare --mount --net --propagation private sh -c 'mount --bind /var/tmp/wex-g4-r1-repair-20261020/x-sockets /tmp/.X11-unix; python3 -B /var/tmp/wex-g3-repair-20261018/clean-source/scripts/check-g3-correctness.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --output-dir /var/tmp/wex-g4-tree-20261020/nested-gl --g4-correctness'
```

Exit 0; eight required groups PASS for **both** architectures. The new group
checks contentless rotated ancestors, unbounded default clips, independently
rounded TL/TR/BL/BR corners, nested clip contraction/expansion, and live clip /
effect mutations not appearing before Commit. Existing multilayer tests now
require distinct positive LAYER `(0,0,128)` and MULTIPLY `(64,0,128)` overlap
colors, plus transactional ancestor clipping, instead of expecting rejection.

Probe SHA256:

| PE | SHA256 |
|---|---|
| x86_64 | `2b39f7062bc389f449bec9345b48c07478724c6ca595febb81ad97868e376b84` |
| i686 | `d5fd7d5fdd20346aa21b459acd12dbf1a90693b95ec481d36c2d5b01e8d06906` |

Raw results and stdout/stderr: `/var/tmp/wex-g4-tree-20261020/nested-gl/`.
Both owned wineservers exited; matching-server backstop lists are empty.
The runner terminates its own Xvfb and leaves global sockets unchanged.

## Further work / truthful boundaries

Still outstanding: multi-target routing/atomicity, arbitrary underlying HWND
background preservation, interpolation/antialias behavior, concurrent object
lifetime/Commit/Present safety, full synchronization fault/race coverage,
efficient backend sharing/fences, coherent DXVK interop and actual maintained
Chromium/Electron/CEF/WebView2 runs. Physical old-Intel/AMD/Nvidia cells and actual
X11/Wayland Classic/Open Luna desktop regressions remain unverified, including
locked/unlocked taskbar, toolbar and tray chevron states. No master merge.

Upstream d3d11/dxgi suites are being enabled separately; absent executables
are not passes. Usage tool at this checkpoint reports 8% of the five-hour
window used; no deferred wake-up is necessary yet. OOBE/token work is untouched.

## Device lifetime checkpoint and upstream controls

The next checkpoint retains the creating device in every visual, target,
effect and clip object; synchronizes pending mutations/teardown with Commit;
rejects foreign-device clips/effects; and repairs NULL transform-object reset
and unsupported transform-parent success. Nine groups PASS on both architectures
in `/var/tmp/wex-g4-tree-20261020/lifetime-boundaries-gl/`, exit 0. The new group
overlaps 64 worker updates/Commits with 64 owner Commits/Presents, checks the
final pixels and releases the caller's device before targets/visuals/effects/
clips. It does not prove every concurrent destruction ordering or allocation fault.

Upstream test build was enabled with the same WoW64 options, replacing
`--disable-tests` with `--enable-tests`, then
`make -j6 dlls/d3d11/tests/all dlls/dxgi/tests/all` (exit 0). The runner's new
`--upstream-tests` mode executes all four built PEs, preserves logs and failures,
and never accepts absent executables. Commands use `MODULE_test.exe MODULE --single`
and `WINETEST_INTERACTIVE=0 WINETEST_NO_MT_D3D=1` in separate owned prefixes.

Results at Wine `736b2b0` versus baseline `0fd6cabc`:

| Suite | x86_64 | i686 |
|---|---|---|
| DXGI, repaired tree | exit 67; 14,468 assertions, 67 failures, 46 skips | same |
| DXGI, baseline | exit 67; 14,464 assertions, 67 failures, 46 skips | exit 67; 14,468 assertions, 67 failures, 46 skips |
| d3d11, both revisions | exit 5, no parent final summary | same |

Failure source-line/count sets match exactly across revisions: DXGI ten sites,
d3d11 two sites (each architecture). The four-assertion d3d11 summary belongs
to its subprocess control, NOT a successful full suite. The parent stops after
the legacy keyed-mutex sharing test's OpenSharedResource failure at line 35549;
its subsequent unchecked output dereference is a likely cause, not yet a traced
crash diagnosis. These inherited failures still prevent upstream acceptance.
No existing assertions were weakened or marked todo to hide them.

Logs: `upstream-tree-gl/`, `upstream-baseline-gl/`, `configure-tests.log`,
`build-upstream-tests.log`, `build-baseline.log` under the owned evidence root.
Baseline source was exported from exact Wine `0fd6cabc` via `git archive` into
`baseline-source/`; only the six changed Wine files were staged into the owned
native source and three affected DLLs rebuilt. Current Windows sources were
then restored/rebuilt before the lifetime runs. Test PEs were unchanged.

An optional `--native-visual-control` mode exercises public visual cases only.
Three native launch attempts in an owned Windows Temp directory were **not
accepted**: target sample points were obscured by another HWND, even after an
explicit visibility/DPI adjustment. The oracle now rejects hidden/minimized/
obscured targets instead of accepting another window's pixels. Native Windows
remains BLOCKED, not evidence against or in favor of Wine's color semantics.
All three controls exited; no system settings or runtimes were installed.

The diagnostic pixel reader must remain observational. A trial that pumped
messages inside the pixel oracle dispatched a pending GDI paint: the pixel was
red immediately before the pump and white afterward, while temporary renderer
instrumentation confirmed both the retained source and composed output were red.
Removed the oracle's message dispatch and the temporary production print; normal
test-owner loops pump BEFORE Commit/Present. This is not proof that reference
composition restores itself over arbitrary subsequent GDI painting; that remains
a delivery/background-preservation gate. Failed runs remain `lifetime-gl/`,
`lifetime-trace-gl/`, `lifetime-paint-gl/`; first passing stress run is
`lifetime-final-gl/`, followed by the full boundary checkpoint above.

## Multi-target and reparenting checkpoint

`/var/tmp/wex-g4-tree-20261020/reparent-gl/`: exit 0, eleven required groups
PASS for both PEs. Earlier ten-group checkpoint: `multitarget-gl/`.
Per-device preparation now covers independent target snapshots before changing
active state. A second-target oversized-surface failure leaves first-target
placement unchanged; restoring its size permits both staged edits to publish.
Unsupported one-producer/multiple-target routing fails before active changes,
and detaching one target preserves the other. Same-HWND multiple targets and
repeated visual roots are rejected rather than silently choosing one output.

Reparenting previously reused a child's active sibling pointer before the old
parent finished traversing it. The new two-phase graph rebuild retains old
nodes, revokes removed content, clears all old edges, then builds new edges.
The regression moves a shared-content visual between sibling parents, checks
restored/retained pixels and requires every detached visual's final caller
Release to report no orphaned active/pending COM reference. It tests the
previously hazardous new-parent-before-old-parent order and the reverse move.

The global limits are 128 visuals / 32 content entries / 32 rooted targets.
Distinct scenes may use different D3D devices; cross-device content within one
scene and cross-target reuse of one producer remain unsupported. Successful
preparation is not a promise of simultaneous physical scanout, nor rollback
after an unexpected final-publish device/window/backend failure. Those limits
are still relevant to the full transaction acceptance gate.

## Failed commands and workarounds

Additional controls at Wine `a1950f5`:

- Eleven groups PASS on both software Vulkan PEs:
  `multitarget-vk/`, selected `/usr/share/vulkan/icd.d/lvp_icd.json`.
- All five pacing controls PASS for both PEs: `pacing-gl/`.
- Original G0-G3 suites and eleven-case G3 ordering controls PASS for both PEs:
  `full-suite-bounded-gl/`, including `full-suite.json`. Its reported loop FPS
  is not displayed/physical FPS evidence.
- Final upstream rerun `upstream-final-gl/` reproduces DXGI exit 67 / d3d11
  exit 5 on both PEs; these suites remain failed, not waived by custom probes.
- The new `--g4-case reparented_tree_lifetime` control FAILS at Wine `e29870e`
  on BOTH PEs with `REPARENT_LEAK refs=1`, and PASSES at `a1950f5` on both.
  Logs: `prior-reparent-gl/`, `reparent-positive-gl/`. Baseline source was
  staged only in the owned native lab; an EXIT trap restored/rebuilt current
  source. `cmp` verified restoration before the positive run.
- `--g4-case fence_capacity_and_reclaim` PASSES on both PEs in
  `fence-capacity-gl/`: 64 live observers, honest 65th registration failure,
  synchronous slot reclamation with a pending waiter, retry, broadcast to all
  64 distinct thresholds, duplicate-handle survival and repeated late events.
  This does NOT yet prove deterministic registration races or process-death
  reclamation. The runner now finishes both architecture controls before failing.

The legacy G0-G3 harness initially timed out at 240 seconds during 32-bit G0
(`full-suite-gl/`). Its subprocess capture had unbounded pipe-EOF waits.
Changed `run_probe` to wait on the PE with a 90-second deadline and read retained
file descriptors instead; preserved explicit caller builtin overrides. Four
Wine-free unit tests cover JSON, failed exits, timeout/owned-child termination,
and preserving the selected DLL stack. The full suite then completed unchanged
in its assertions. Logs remain for both the timeout and passing rerun.

Read-only VM inventory found an existing WebView2 155.0.4283.45 lab on
WineExplorer-VM (already running). Approval review blocked copying its cached
proprietary runtime CAB/host/loader into an isolated host Temp cache as a reverse
transfer/payload disclosure risk. The user subsequently explicitly authorized
this isolated transfer; it completed without a vendor download, VM restart/reset
or Git/ISO payload inclusion. The private CAB matches its recorded SHA-256
`ec533bf1b7354d288703cf821a2d640d0130108224f93e330bb9c09faf3f8010`.
cabextract reported a possible 10176-byte trailing area; extraction exited 0.
The copied original diagnostic host source matches the repository SHA-256
`a8945957b31453d889d6dc89a075f3564ad6fd0326c1bcff2918ea502aac0d08`.

- Non-escalated `git status --short --branch` failed to read
  `.git/modules/wexcomp` (`Permission denied`). Repeated with approved elevated
  execution; worktree was clean before graphics changes.
- Initial multi-file `apply_patch` failed to match an ancestor-clip comment.
  No files changed; inspected exact text and reapplied with correct context.
- `tail -14 /var/tmp/wex-g4-tree-20261020/initial-gl/g4-x64.stdout.log`
  failed because the runner names it `x86_64.stdout.log`; used the actual name.
- `make -n dlls/dxgi/tests/all` reported `No rule to make target` because the
  owned build was configured with `--disable-tests`. Reconfigure for tests
  using the same WoW64 options rather than treating missing binaries as PASS.

## Opaque retained OpenGL checkpoint

The new narrow GPU route owns a retained texture per producer, copies only on
real Present and blits eligible opaque layers into the actual drawable. No new
per-frame CPU completion/readback is added. Eligibility requires non-layered
HWND, unit opacity/scale, no clips, integer offsets, ignored alpha, FBO support
and glBlitFramebuffer. Both BGRA/RGBA and multiple producers are covered. Other
scenes, including Vulkan, keep the reference renderer. Switching to a clipped
scene reads the last GPU snapshot once; switching back preserves a more recent
CPU-presented frame until the next actual GPU Present. Allocation is prepared
before replacing the old texture. This is one retained GPU copy plus drawable
blits, not zero-copy or accelerated cross-process image sharing.

Private GetPrivateData GUID `a1042da3-2ea1-4d9a-a403-011ed36183fb` returns four
UINT64 counters: GPU captures, CPU readbacks, GPU compositions, CPU compositions.
It drains the command queue for diagnostic observation only. These counters do
not implement public frame statistics or prove displayed frames/physical FPS.

Final capability-gated source evidence under the owned root:

- `gpu-cap-final-gl/`: 14 groups PASS, both architectures; both pixel formats
  show 61 changing-frame GPU captures and zero Wine-side readbacks before the
  intentional clip fallback. Multilayer controls independently change each
  producer, preserve pending versus presented frames and verify z-order.
- `gpu-cap-final-vk/`: same 14 groups PASS, both architectures, explicit lavapipe
  CPU fallback (zero GPU-route counters); not Vulkan GPU-composition proof.
- `gpu-cap-full-suite-gl/`: G0-G3 plus eleven G3 ordering cases PASS, both PEs.
- `gpu-cap-pacing-gl/`: five pacing controls PASS, both PEs.

Both drivers are software. Upstream-suite failures, required desktop/theme/
Wayland/physical-hardware checks and full G4 acceptance remain open.

## Real WebView2 first-failure checkpoint

`scripts/check-g4-webview2.py` runs the private cached host/runtime in a fresh
prefix and owned Xvfb, offline in a private network namespace. It requests only
logging flags: no version spoof, no --no-sandbox, no single-process, no disabled
GPU sandbox or security-worker import. It records actual process argv, module
maps, hashes and bounded cleanup; JavaScript progress is never a pixel pass.

`webview2-default-gl/`: runtime 155.0.4283.45, host exit 0, navigation succeeds,
23 samples/timer progress, but **zero animation frames and zero paints**. GPU
process exits repeatedly with STATUS_BREAKPOINT (0x80000003). Therefore this is
a failed real-consumer test despite successful environment/controller creation.
Logs immediately preceding each breakpoint include unsupported DComp interface
`4ca97a18-cbfd-4b0d-89e1-f7fa86d8d63e` and ID3D11VideoContext1
`a7f026da-a5f8-4487-a564-15e34357651e`. Temporal proximity is NOT proof of cause;
the breakpoint stack still needs tracing. Token claims/integrity stubs also
appear; sandbox request flags do not prove enforcement. No bypass was applied.
All matching owned Wine servers exited and Xvfb was terminated; the private
prefix/profile/logs remain outside Git for diagnosis.

## Video capability mismatch fixed; next real-consumer blocker

The actual immediate context lacks VideoContext1, but the device advertised
VideoDevice1 and its newer methods were unimplemented. Chromium's
[startup capability check](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/ui/gl/direct_composition_support.cc)
requires the paired context after VideoDevice1 QI succeeds. The original runner's
`--trace-exceptions` control (`webview2-exception-gl/`) consistently records
STATUS_BREAKPOINT immediately after context QI returns E_NOINTERFACE, with
RAX=0x80004002. This supports the source-level diagnosis; no native stack symbols
were obtained and no proprietary assembly/source is committed.

Stop advertising the unsupported VideoDevice1 interface, preserving the base
video device and ordinary D3D interfaces. Re-enable only with a real coherent
implementation, not a stub interface or a browser-only flag. This does not add
HDR, video processors or decoders. The regression also queries through the base
video interface to check consistent identity/exposure and failure outputs:

- `video-cap-negative-gl/`: both architectures FAIL before the fix:
  base/video1 QI S_OK, context1 E_NOINTERFACE.
- `video-cap-positive-gl/` and `video-cap-positive-vk/`: all **15** groups PASS
  on both architectures after the fix (explicit software Vulkan ICD).

`webview2-video-cap-gl/` gets past that initial startup check. It still FAILS:
navigation succeeds, 23 samples, only 83 animation frames with intermittent late
progress, repeated GPU crashes. The new fatal is
`skia_output_surface_impl.cc:1285`: invalid backend format for color type 4,
732x486, default GL framebuffer; SharedImage SkSurface creation also fails.
[Upstream characterization code](https://raw.githubusercontent.com/chromium/chromium/main/components/viz/service/display_embedder/skia_output_surface_impl.cc)
identifies the next area to trace: ANGLE/Skia renderable-format capability
reporting and actual framebuffer/SharedImage creation, not another fake success.
Current upstream source is guidance, not an exact source build of Microsoft's
cached runtime. Default browser/security flags were unchanged. Recorded process
maps identify the loaded builtin graphics stack; no native DXVK was injected.

The optional owned-Xvfb screenshots at 8/18 seconds reveal a diagnostic console
obscuring much of a blank host window. They do NOT establish presented-content
correctness. Preserve them privately, not in the theme/ISO/Git. The runner now
returns diagnostic failure for missing/stalled/intermittent frames even when
the host exits 0, and explicitly never certifies G4 on a diagnostic success.
The first full/exception runs used the older runner; assess their recorded
animation state rather than treating their launcher exit as acceptance.

One new test compile attempt (`video-cap-before-gl/`) referenced the G3-only
`create_device` helper; no PE ran. Replaced it with direct D3D11CreateDevice,
then obtained the two-architecture negative/positive controls above. A source
inspection incorrectly guessed `dlls/d3d11/video.c` and `dlls/dcomp/main.c`;
implementations are in device.c. No missing file was counted as test evidence.

Final video-capability checkpoint Wine `f5f4312`:

- `video-cap-full-suite-gl/`: G0-G3 plus eleven ordering controls PASS for both
  PEs after rebuilding d3d11. Four harness unit tests PASS again.
- `video-cap-upstream-gl/`: DXGI still exits 67 with 67 failures / 46 skips,
  14,468 assertions on both architectures; d3d11 still exits 5 at the inherited
  legacy-shared-resource failure. No full-suite success is claimed.
- `webview2-video-cap-vk/`: explicit lavapipe still records three GPU crashes,
  three fatal Skia errors, seven SkSurface failures and one shared-context
  failure. Its 474 animation frames / five late deltas of 60 do **not** make it
  a pass. Screenshots are partly obscured by its diagnostic console.

The analyzer now separates animation state from `diagnostic_passed`: successful
navigation, sustained frames and a Chromium log are required, and GPU crashes,
fatal logs, SkSurface/shared-context errors disqualify the run. Five new unit
tests PASS, including animation-after-crash and missing-log false positives.
Reanalysis of `webview2-video-cap-vk/` fails correctly despite frame progress;
the earlier results.json reflects the older frame-only classifier. A fresh run
with the strict classifier is retained separately in `webview2-vk-strict/`.
Even diagnostic success is not pixel/sandbox/hardware/G4 certification.

An attempted standalone ANGLE format probe was BLOCKED during file preflight:
the selected fixed WebView2 runtime has no separate libEGL.dll/libGLESv2.dll.
No ANGLE PE was run. Removed the untested repo prototype/runner mode rather
than substituting unrelated ANGLE DLLs and misrepresenting the consumer stack.
Future format diagnostics must inspect the actual embedded implementation or
use a separately identified consumer/build and report that distinction.

Fresh strict control `webview2-vk-strict/` confirms the failure exit path:
host exit 0, diagnostic exit 4, 463 frames, three GPU crashes/fatal errors,
seven SkSurface errors and one failed shared-context creation. Cleanup records
no remaining matching server. No active consumer/test process is intentionally
left running while waiting for the account usage-window reset.

## Resume after the usage checkpoint

At 09:49 Central on October 20 the account tool reported 79% of the five-hour
window used, reset 13:17:51 Central. Checkpoint/push before deferring this chat
until after reset. Do not spend its unused one-time reset credit. No master
merge is authorized by this incomplete evidence; no OOBE/token-worker files
were changed. Machine-readable hashes/statuses are in
CHROMIUM_GRAPHICS_G4_CHECKPOINT_20261020.json.

Next work is the actual embedded ANGLE/Skia renderable-format failure, with the
default consumer/security flags preserved. Current builtin Wine revision is
`f5f4312`; parent branch is `codex/g4-r1-completion`. Reuse the owned build and
private lawful runtime instead of downloading/bundling another payload:

```sh
unshare --mount --net --propagation private sh -c 'mount --bind /var/tmp/wex-g4-r1-repair-20261020/x-sockets /tmp/.X11-unix; python3 -B /var/tmp/wex-g3-repair-20261018/clean-source/scripts/check-g4-webview2.py --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 --runtime /var/tmp/wex-g4-tree-20261020/private-runtime/runtime/Microsoft.WebView2.FixedVersionRuntime.155.0.4283.45.x64 --host /var/tmp/wex-g4-tree-20261020/private-runtime/webview2-probe.exe --output-dir /var/tmp/wex-g4-tree-20261020/NEW-UNIQUE-OUTPUT --trace-exceptions'
```

Output must be new. Use explicit lavapipe arguments for a separate Vulkan
control, never infer hardware from the synthetic GTX 470 adapter string.
Do not rebuild the shared owned build while a probe uses it. After targeted
repairs, rerun both architectures' 15 correctness groups and G0-G3, pacing and
upstream controls without weakening assertions; then finish remaining R1-R4
requirements listed above, particularly actual desktop/theme/Wayland/hardware
and security-enabled real consumer coverage. No-sandbox/single-process/GPU
sandbox bypasses or unreviewed token merges cannot establish acceptance.

## Resumed: multisample format reporting repaired

The usage window reset before the one-time wake-up; both worktrees were clean
at `f9b888d1` / Wine `f5f4312`. Added opt-in WebGL1/2 default/FBO pixel diagnostics
to the original C host, a bounded-duration/graphics TRACE runner mode, and a
separate original sandbox-requesting Electron fixture. Vendor SDK headers and
all binaries remain in private caches, not Git/ISO. The diagnostic host hides
only a console it created itself, never an inherited terminal. Some console
border artifacts remain in Xvfb and are not shell/theme acceptance.

`resume-format-trace-gl/` showed RGBA8 flags `0x280f3f3`: render-target support
present but MULTISAMPLE_RENDERTARGET absent. `resume-msaa-negative-gl-rerun/`
confirms both PEs report 4x quality=1 while 2x/8x/16x/32x quality=0, yet omit
that capability bit. ANGLE's
[capability generator](https://raw.githubusercontent.com/google/angle/main/src/libANGLE/renderer/d3d/d3d11/renderer11_utils.cpp)
gates its sample-count table on that bit. Its
[format support data](https://raw.githubusercontent.com/google/angle/main/src/libANGLE/renderer/dxgi_support_data.json)
makes RGBA8 multisample support a runtime check rather than an always-supported
flag. Wine's existing all-or-nothing 4x AND 8x check hid usable 4x rendering.

The repair reports the render-target bit for actual supported counts, while
leaving exact count queries and conservative resolve/load flags unchanged.
This follows the independent capability meanings in Microsoft's
[format flags](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/ne-d3d11-d3d11_format_support).
Do not claim unsupported 8x or full FL11.1 compliance: Microsoft's
[feature-level guarantees](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-checkmultisamplequalitylevels)
also expose an inherited broader feature-level-reporting gap on this software
driver, which this narrow fix does not solve by inventing sample counts.

- `resume-msaa-positive-gl/`: both PEs PASS actual 4x RTV clear/resolve/readback
  for RGBA/BGRA; flags become `0x2a0f3f3`, unsupported counts stay zero.
- `resume-msaa-full-gl/`, `resume-msaa-full-vk/`: all **16** required groups PASS
  for both PEs. Vulkan uses the explicitly selected software lavapipe ICD.
- `resume-msaa-webview2-gl/`: same cached runtime, same original host binary and
  browser arguments as the failed control; 402 frames, 22 samples, no GPU
  crashes, Skia fatals, SkSurface or shared-context errors; diagnostic exit 0.
- `resume-webgl-gl/` (before): WebGL default/FBO pixels black and framebuffer
  status 36061 (unsupported), delayed capability result after GPU restarts.
- `resume-msaa-webgl-gl/` (after): WebGL1 AND WebGL2 default/FBO red
  `[255,0,0,255]`, framebuffer status 36053 (complete), error=0, lost=false;
  439 frames, no fatal/crash signatures. Both remain software-driver evidence.

Front-window screenshot `resume-msaa-webgl-gl/screen-18.png` now contains the
actual page instead of a blank window, but its moving square has visible
duplicates/trails and the displayed counter lags the JavaScript samples. Thus
this is a real-consumer startup repair, NOT correct retained/damage delivery,
performance or full G4 acceptance. Keep the image private, not in theme assets.

An additional current Electron control was downloaded from its official
[v44.9.2 release](https://github.com/electron/electron/releases/tag/v44.9.2),
verified against SHASUMS256.txt; archive SHA256
`1a9ed7f4e70291c95ed9479d7fcd384a2d9cf062e5fb7ce846b119c7c63036c3`.
Its notices remain with the private extracted runtime. It uses Chromium
152.0.7977.167, not the WebView2 155 build. It also has no standalone EGL/GLES
DLLs, so the attempted independent DLL probe was not executed and its repo
prototype was removed. The actual Electron fixture instead uses public Electron
APIs, explicit sandbox request, isolated profile, no renderer Node integration
and no security bypasses. `resume-electron-default-gl/` reproduces the SAME Skia
failure before the MSAA fix: 3 GPU crashes/fatals, 21 SkSurface errors despite
later animation. Initial GPU-info results precede GPU initialization and show
a dummy/no-GL record; they are not proof of actual rendering route or security.

Failed attempts retained: system EGL headers lack ANGLE-specific platform
tokens (standalone probe compile failed; checked official extension definitions
and retried). The compile then passed, but preflight rejected absent libEGL.dll
before any PE ran. The new multisample regression initially used a braced range
without initializer_list; replaced it with an explicit constant array. That
compile-only failure is `resume-msaa-negative-gl/`; the actual two-architecture
negative control is the separately named rerun above. A premature read of the
not-yet-created Electron log failed while compilation/preflight was pending;
missing logs were not accepted as execution evidence.

## Resumed: coherent Present1 damage repair

The original Electron fixture can now freeze its DOM at x=160 after eight
seconds (`--freeze-scene`); RAF counters continue, while capturePage is delayed
until after the front-window screenshot. `resume-msaa-electron-frozen-gl/`
showed a yellow stale damage patch on an otherwise green page despite no
further DOM changes. This distinguishes stale preservation from an animated
screenshot tearing artifact. Internal capturePage alone was not acceptance.

`resume-dirty-negative-gl/` deterministically reproduces the underlying problem
on both architectures: after BLUE and RED full frames, a GREEN-only dirty patch
leaves outside pixels BLUE, not the last coherent RED. Present1 previously
discarded its dirty/scroll metadata. The repair reconstructs the complement of
the union of dirty rectangles from an independent previous-frame GPU image,
then copies scrolled content excluding dirty rectangles. Full Presents also
record coherent history. Two GPU images keep pending and accepted history
separate. Existing ordered blits carry copied rectangle values; no caller
pointers, new CPU readback, or per-frame completion wait are introduced.

Per-swapchain Present/Resize serialization prevents history races. Successful
resize invalidates it; final release frees it. TEST/minimized/unbound paths do
not advance history. There is a deliberate bounded limit of 64 dirty rectangles
(E_NOTIMPL beyond it). Single-sampled sequential/flip-sequential only; scroll
requires flip-sequential. Memory cost is two full-size images, plus snapshot
bandwidth each eligible frame. This is correctness-first, NOT zero-copy.

The documented scroll example bounds the destination rectangle; its source is
destination minus offset. This was tightened during review, with 64-bit clipping
and a regression that distinguishes destination from source interpretation.
See Microsoft's [Present parameters](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/ns-dxgi1_2-dxgi_present_parameters)
and [presentation contract](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/dxgi-1-2-presentation-improvements).
No vendor implementation code was copied.

- `resume-dirty-positive-gl/`: initial preservation PASS both PEs.
- `resume-dirty-scroll-gl/`: initial scroll/invalid/TEST/extreme-offset controls
  PASS both PEs; superseded by stricter destination assertions below.
- `resume-dirty-full-gl/`, `resume-dirty-full-vk/`: all 17 groups PASS both PEs
  before the destination-bound scroll audit; Vulkan explicitly software lavapipe.
- `resume-dirty-destination-gl/`: all 17 groups PASS both PEs after that audit.
- `resume-dirty-union-gl/`: expanded overlapping dirty-union and bounded-limit
  assertions PASS both PEs on the final production source.
- `resume-dirty-destination-vk/`: all 17 groups, including expanded dirty-union
  and destination-scroll assertions, PASS both PEs with explicit lavapipe.
- `resume-dirty-g0-g3/`: G0-G3 and all 11 ordering groups PASS both PEs (before
  the scroll-only audit); `resume-dirty-pacing/`: five pacing groups PASS both
  PEs on final production source. Corrected stale probe headers: their assertions
  check logical D3D11 index 0, NOT physical 0/1/2 cycling.
- `resume-dirty-electron-frozen-gl/`: 1,140 frames, 24 samples, no crash/fatal/
  SkSurface/shared-context signatures; the front-window image has one square,
  correct frozen text and a uniform background, unlike the negative control.
- `resume-dirty-webgl-gl/`: 540 frames, 21 samples, no fatal/crash signatures;
  front-window image now has one square and uniform background. These real
  consumer runs precede only the scroll-coordinate audit; no browser security
  flags changed, and the sandbox was requested, not proven enforced.
- `resume-dirty-upstream/`: still FAIL, not waived: DXGI exit 67 / 67 failures /
  46 skipped both PEs (14,464 x64, 14,468 i686 assertions); D3D11 exit 5 both
  PEs at the legacy shared-resource failure. Before this repair DXGI also had
  67 failures and D3D11 exited 5. Representative refresh/fullscreen failures
  match; small assertion-count variation is retained, not called a clean suite.

Published Wine repair: `498b3029e8200802cfe675f2499908dc9e875124`, branch
`codex/g4-r1-completion`. The earlier checkpoint JSON is an immutable pre-reset
snapshot at f5f4312, NOT current HEAD. This section supersedes its current-status
description; no old evidence is relabeled as this newer build.

All private screenshots/runtime binaries stay outside Git and the ISO. These
results repair a real consumer output defect but do not establish Classic/Luna,
X11 compositor OFF/ON, Wayland, physical GPU, full lifecycle or G4 acceptance.

Failed commands retained for this resumed step: unprivileged git status cannot
read `.git/modules/{wine,wexcomp}` (Permission denied); scoped elevated status
works and shows only this task's files. A nested sh command quoted using Bash
escapes in PowerShell failed `sh: 0: -c requires an argument`, then attempted the
Windows Python alias; no test ran. A PowerShell literal here-string passed to
WSL fixed quoting. A screenshot copy/view was attempted before screen-18.png
existed; ls/cp and view_image failed with missing-file errors. After the bounded
consumer finished, copying/viewing the actual screenshot succeeded. Debian has
no rg executable (`sh: 1: rg: not found`); use grep for native evidence queries.
A search for nonexistent `wine/dlls/dcomp/visual.c` failed; implementation is in
device.c. No global X socket, live prefix, VM, OOBE or token checkout changed.

## Resumed: real Electron lifecycle control

Added opt-in `--lifecycle` to the original Electron fixture/runner. It injects
mouse/character events through Electron (not DOM value assignments), checks
typed text and click response, injects a wheel scroll, resizes to 620x420,
minimizes/restores with resumed RAF progress, reloads and verifies fresh DOM
state, then resizes back to 740x520. Public Electron APIs only; isolated offline
profile, sandbox requested, no renderer Node or bypass flags. This is not proof
of native hardware input, Win32 input routing or sandbox enforcement.

`resume-dirty-electron-lifecycle-gl/` correctly FAILS despite continuing frames:
positive wheel delta attempts to scroll upward from the top, so scrollY stays
zero. Other six checks pass. The fixture's downward delta is negative;
`canScroll:true` is now explicit (Electron's converter already defaults true).
After correction, `resume-dirty-electron-lifecycle-gl-rerun/` exits 0: all seven
checks true, 34 samples, 558 final post-reload frames, no crash/fatal/SkSurface/
shared-context signatures. Wine remains the published 498b302 build.

Runner requires exactly one lifecycle result with every named check literally
true, in addition to navigation/progress/no-error checks. Analyzer tests now
include missing, false, numeric, duplicate and malformed lifecycle results: six
unit tests PASS in Debian. Node syntax check PASS. The first Windows unit-test
attempt failed five tests because sandbox Temp ACLs denied host.log creation
and temporary-directory cleanup; no renderer assertion failed. Debian rerun
passes without changing assertions. Those empty sandbox temporary directories
may remain; no broad cleanup was attempted. Another edit patch failed because
the expected runner-registry context did not match; atomic verification left
the proposed interpolation-test changes unapplied.

Next R1 work: interpolation mode setters still return success without honoring
LINEAR or validating enums. Microsoft documents nearest-neighbor as the root
default, and INHERIT as each visual's default (not linear). Border/antialias,
background/expose and the remaining R2-R4 gates above remain open.

## Resumed: interpolation is no longer a successful no-op

Wine `01ab041` validates NEAREST/LINEAR/INHERIT and snapshots/inherits the mode
with the visual tree transaction. Default is INHERIT, resolved to nearest at
the root, as documented by Microsoft's [setter contract](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositionvisual-setbitmapinterpolationmode)
and [enumeration](https://learn.microsoft.com/en-us/windows/win32/api/dcomptypes/ne-dcomptypes-dcomposition_bitmap_interpolation_mode).
The reference compositor samples texel centers bilinearly, clamping four taps
to the image and interpolating premultiplied components. This does NOT establish
linear-light blending, soft-border AA or accelerated transformed composition.
The existing integer-translated opaque unit-scale GL route remains valid for
both filters because its samples land exactly on texel centers.

Private scene layout changed; matched binding IID is now
`bc193c8b-df4f-4c75-8a59-0ab8ffccdc0b` (older IID above is historical). Rebuilt
all matched DComp/DXGI/WineD3D/D3D11 DLLs, both architectures, no mixed versions.

- `resume-interpolation-negative-gl/`: both PEs FAIL the quarter-texel boundary:
  expected RGB(191,0,64), actual pure red after LINEAR was accepted/committed.
- `resume-interpolation-positive-gl/`: both PEs PASS inherited modes, explicit
  child override, reset to INHERIT, invalid enum rejection, pending isolation,
  retained Commit without Present, texel-edge clamps, premultiplied alpha and
  two-axis four-color interpolation. Exact expected channel values, no tolerance.
- `resume-interpolation-full-gl/`, `resume-interpolation-full-vk/`: all **18**
  required groups PASS both PEs; Vulkan explicitly lavapipe.
- `resume-interpolation-electron-lifecycle-gl/`: all seven real lifecycle checks
  PASS, 34 samples / 541 post-reload frames, no GPU/Skia fatal/crash signatures.

Read-only facility check: `/dev/dri` absent in Debian; glxinfo/Xvfb available,
weston/vulkaninfo/wexcomp not on PATH. This does not prove those facilities are
absent from the separate VM/project, only that this isolated WSL lab cannot be
called an old Intel/AMD/Nvidia hardware acceptance cell. Actual desktop/theme
and Wayland/wexcomp checks remain NOT RUN, not inferred from unchanged assets.
An attempted read of nonexistent wined3d/fence.c failed; this branch's shared
fence implementation is in d3d11/device.c. No production fence edit made here.

## Resumed: process-death observer reclaim and scoped idle CPU

`resume-fence-death-idle-gl/`: new focused group PASS both PEs on Wine 01ab041.
An owned child imports 64 fence objects and registers unsatisfied completions.
The parent's 65th observer must explicitly fail capacity. After startup settles,
GetProcessTimes over two seconds reports parent/child 10/10 ms (x64) and 0/0 ms
(i686), below the explicit 100 ms per-process ceiling (5% of one core). This is
scoped process CPU, not whole-VM/wineserver/GPU power or exhaustive idle proof.

The parent intentionally terminates only that child and waits for its death,
then successfully reuses ALL 64 observer slots and completes all notifications.
This proves dead-process stale-name reclamation in this scenario, rather than
only orderly local Release. Child startup failure, capacity loss, any missing
completion, excessive CPU or cleanup failure cannot count as a pass. The child
also has a 30-second self-bound if the parent disappears. No production fence
code changed. The suite now requires 19 groups; previous full 18-group runs are
not relabeled as 19. Deterministic signal-during-registration hooks, fault
injection, creator death, broad handle/mapping/COM leak and backend sharing
efficiency still remain open R2/R3 evidence.

## Resumed: arbitrary dirty lists and strict real WebGL validation

Wine `273cacd9affa680dbbb4ba79593da685c09e8bec` replaces the initial fixed
64-rectangle complement algorithm with ordered pending-image assembly: previous
frame, clipped scroll, caller dirty writes, then coherent image to presentation
buffer. Dirty regions naturally take priority and overlap safely. Validation
and command count are linear in the rectangle count; no interval-fragmentation
arrays or arbitrary cap remain. Two history textures remain; partial updates
cost two full-frame GPU copies plus dirty/scroll copies. This is correctness
and compatibility, not a zero-copy, measured speed or power claim.

- `resume-dirty-many-negative-gl/`: valid 65-rectangle Present1 FAIL both PEs on
  the earlier bounded implementation; no invalid caller array used.
- `resume-dirty-linear-full-gl/`, `resume-dirty-linear-full-vk/`: all **19**
  mandatory groups PASS both PEs on final source, including overlapping dirty
  regions, scrolling, >64 rectangles and process-death/idle CPU. Vulkan lavapipe.
- `resume-dirty-linear-electron-lifecycle-gl/`: 60-second control, all seven
  lifecycle checks PASS, 59 samples / 944 final post-reload frames, no GPU/Skia
  fatal/crash signatures. Not a benchmark or browser-security certificate.
- `resume-dirty-linear-webgl-gl/`: 30 seconds / 508 frames, both WebGL contexts
  return correct default/FBO red pixels, complete status and no errors.
- `resume-final-webgl-strict/`: runner now REQUIRES both WebGL1 and WebGL2 correct
  pixels, complete framebuffer, no API errors and no context loss in addition
  to prior no-crash/progress checks. PASS, 26 samples / 440 frames. Seven analyzer
  unit tests PASS, including malformed/missing/failed contexts and wrong pixels.
- `resume-final-electron-frozen/`: PASS diagnostic, 1,027 frames; front screen-18
  inspected again: one blue square at x=160, correct text, uniform background.
- `resume-final-pacing/`: all five groups PASS both PEs on final source.
- `resume-final-upstream/`: FAIL unchanged, DXGI 67 failures / 46 skips / 14,468
  assertions both PEs; D3D11 exit 5 both at legacy sharing failure. The complete
  x64 DXGI failing-source-line/count set matches resume-msaa-upstream exactly.

`resume-final-g0-g3/` failed G1 i686 HWND creation BEFORE D3D11 initialization
while several independent controls ran. G0 both, G1 x64 and all eleven G3
ordering controls both had passed. All-false JSON is NOT a D3D device diagnosis:
the source returns immediately when parent CreateWindow fails. Added error-only
RegisterClass/CreateWindow Win32 diagnostics; no assertion or success condition
changed. Serial rerun is recorded separately; do not erase the failed run or
assert resource contention was proven merely because a retry succeeds. Disk
capacity check showed 720 GiB free. Reads using guessed nonexistent G1 filenames
failed; the actual file is scripts/test_g1_non_dcomp_probe.cpp.

Historical descriptions above retain the initial cap as it existed at those
checkpoints; this section supersedes it. Current binding IID remains
bc193c8b-df4f-4c75-8a59-0ab8ffccdc0b. No proprietary payload, screenshot, SDK
header or font/art entered Git/ISO, and no OOBE/token worker or master changed.

## Resumed: filesystem X socket collision found and isolated

`resume-final-g0-g3-serial/` PASSes all G0-G3 and eleven ordering cases both PEs
with error-only setup diagnostics and unchanged assertions. That alone did not
explain the earlier failure. A separate two-Xvfb regression then proved a real
test-harness hazard: network namespaces isolate abstract Unix sockets, but our
older shared bind directory did not isolate filesystem sockets. Both servers
choose display 0; the second replaces X0. SO_PEERCRED in the first namespace
then reports the SECOND server's PID. This can cross-connect test clients.

- `xsocket-shared-negative/`: expected FAIL; first own PID 360, peer changes
  from 360 to 367, filesystem inode 465948 to 465951 after the second starts.
- `xsocket-private-positive/`: PASS; first remains connected to own PID 386 /
  inode 465955 while second uses PID 393 / inode 465959.

These PIDs are historical evidence, not current processes to terminate. Both
owned servers and namespace children exit. No global display socket changed.
New run-isolated-graphics.py gives each invocation a unique owned directory and
private mount/net namespace, removes only its empty generated directory and
retains nonempty state rather than recursively deleting it. Caller must still
use the bounded runner and its own Wine/Xvfb cleanup. This fixes proven
cross-server isolation, not a claimed traced root cause of the original G1 HWND
failure. Fresh parallel graphics/consumer controls use `isolated-final-*` names.

Final per-run-socket controls all completed: `isolated-final-full-gl/` and
`isolated-final-full-vk/` (19 groups both PEs), `isolated-final-g0-g3/` (G0-G3
and eleven ordering cases both PEs), `isolated-final-pacing/` (five groups both
PEs), `isolated-final-webgl/` (strict both-context pixels),
`isolated-final-electron/` (all seven lifecycle checks, 34 samples / 519
post-reload frames), and `isolated-final-frozen/` (1,100 frames; actual screen-18
viewed: one x=160 square, correct text and uniform background). Exit 0 for each.
No physical performance/security/theme claim follows. New checkpoint:
CHROMIUM_GRAPHICS_G4_CHECKPOINT_20261020_RESUMED.json; current concise matrix:
CHROMIUM_GRAPHICS_G4_STATUS_20261020.md.

Owned Wine/wineserver/Xvfb process inspection after completion found none left;
the per-run socket root contains no generated directories. Private browser
profiles/prefixes, raw evidence and lawful runtime caches remain for diagnosis.
WineExplorer-VM was not restarted, reset, reinstalled or modified this resumed
step. No OOBE/token work was touched. The automation lookup using CODEX_HOME
failed because that variable was unset and PowerShell resolved /automations as
D:\automations; explicit C:\Users\Owner\.codex\automations found the existing
one-time continuation entry. No duplicate automation was created by that lookup.

Usage checkpoint after publication: 78% of five-hour window used, 25% weekly;
primary resets 2026-10-20 23:20:24 UTC (18:20:24 Central). Per the user's standing
instruction, updated the EXISTING one-time thread continuation to 18:23 Central
today, with the new source/status reports and per-run-socket launcher. Kept the
unused full-reset credit untouched. Published code checkpoint is f43ec930 /
Wine 273cacd; the follow-up documentation commit records this scheduling state.
G4 remains incomplete and no master merge was performed. Next actionable source
work and unavailable physical/desktop evidence are explicitly listed in the
status report, not hidden by the passing isolated functional controls.
