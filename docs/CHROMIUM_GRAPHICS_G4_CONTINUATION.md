<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 completion progress — 2026-10-20

Branch in both repositories: `codex/g4-r1-completion`. G4 is **in progress**;
this report supersedes only the explicitly repaired limitations in
CHROMIUM_GRAPHICS_G4_R1_REPAIR_REVIEW.md, not its remaining acceptance gates.

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
