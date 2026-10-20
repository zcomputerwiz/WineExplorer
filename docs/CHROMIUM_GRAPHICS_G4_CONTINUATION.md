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
