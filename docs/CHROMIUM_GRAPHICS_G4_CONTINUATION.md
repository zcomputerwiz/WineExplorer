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
