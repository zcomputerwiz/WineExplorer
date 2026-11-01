<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 native cross-process render bridge — 2026-10-21

Scoped working implementation; **G4 is still not merge ready**. This supersedes
the blank-content status in CHROMIUM_GRAPHICS_G4_WEXCOMP_NATIVE_20261021.md,
which remains the original negative evidence. No master merge, OOBE/token
imports, Chromium sandbox switches or proprietary payload/art publication.

Published forks: Wine `571e907` on `codex/g4-r1-completion`; compositor
`1b9df94e` on `codex/g4-native-render-bridge` (zcomputerwiz/labwc).

## Cause and implementation

Chromium's GPU process owns the rendering child but not its browser-process
top-level. Wine cannot create an ordinary wl_subsurface across two Wayland
connections. A second failure occurred before presentation: the CPU composition
path tried to read an inaccessible foreign-process GDI backing even when an
opaque retained image completely covered it. Both failures needed repair.

The matching Wine and wexcomp protocol copies now expose private manager v11
and a render-only surface role. Producer connections use the existing random
128-bit parent-window identity, obtained through Wine's HWND property/atom
namespace, not a raw HWND/process identifier in the compositor. Existing private
endpoint and peer checks remain in force. No public-socket capability expansion.
The wexhost protocol copy stays at its compatible older subset; it does not use
the new request. Matching driver/compositor v11 are required for this route.

The compositor attaches content beneath the parent's content tree, clips it to
both requested bounds and current parent surface bounds, inherits parent tree
position/visibility, clears the rendering surface's effective input region, and
revokes/detaches on parent destruction. Producer role/surface/connection cleanup
is independently exercised. This is functional validation, not adversarial
security certification, pointer-routing acceptance or a token-security claim.

Wine publishes logical scale alongside the parent identity, converts producer
geometry, and caches attachment identity/geometry so unchanged frames do not
send a new parent/viewport request each time. The ordinary local-owner
wl_subsurface path remains. D3D composition skips GDI capture only when a valid
retained frame proves complete opaque coverage with unit/integer translation
and no layer clips or opacity reductions. Partial/translucent scenes still
require their real background; no white/background substitute was added.

## Evidence on the final candidate binaries

Evidence root: `/var/tmp/wex-g4-tree-20261020`.

| Control | Result |
| --- | --- |
| r4-bridge-final-vulkan-x86_64 / -i686 | Own and foreign full-target DComp: exactly4096 red front pixels each |
| r4-bridge-final-gl-x86_64 / -i686 | Same exact pixel acceptance on both PEs |
| r4-render-protocol-destruction-contract | All16 exact compositor-pixel/lifetime controls PASS |
| r4-bridge-regression-gl / -vulkan | All28 composition correctness groups PASS both PEs |
| r4-bridge-electron-final-gl / -vk | Real Electron44.9.2: all lifecycle checks/eight held captures complete, native winewayland observed |
| r4-bridge-webview-final-gl / -vk | Real WebView2: strict WebGL1/2 default/FBO pixels PASS; inspected real front content visible |
| r4-bridge-pacing-final | Five pacing groups PASS both PEs |
| r4-bridge-g0-g3-final | G0–G3 and11 ordering controls PASS both PEs |
| r4-bridge-native-recovery-gl / -vulkan | Real owned GPU-child replacement, context-loss/recreation, exact WebGL pixels and inspected recovered front output PASS |

The16 native controls cover attach, wrong parent, exact clipping, invalid size,
reattach, parent identity revocation, stale identity, newly issued identity,
producer role destruction/recreation, invalid surface-before-role ordering
rejection, abrupt producer disconnect/reconnect, actual parent surface
destruction while producer survives, and reuse of its dead identity. The
invalid-order test requires a protocol error, then verifies compositor survival
and no residual red pixels. It is not counted as a graceful valid request.

Four compositor harness, ten consumer harness and seven WebView2-analysis unit
tests PASS. Consumer API success is explicitly not a screenshot verdict.
Actual Electron GL/Vulkan captures were inspected: content is present through
startup/input/scroll/resize/restore/reload, and the minimized frame is absent.
One Vulkan resize_back checkpoint has an incomplete outer border; its settled
screen-18 capture is complete. GL resize_back is complete. This timing/fidelity
observation remains open for a deterministic settled-frame/repaint check; it is
not silently waived by successful lifecycle APIs or later animation.

Native compositor renderer is **headless pixman**, clients software GL or
lavapipe. The build-directory suffix acc46fe is historical: this binary contains
the new bridge, not the original pinned source. Compositor SHA256:
`7664f2f56b57018689a82723ce9479b141bf76b24b08283e891fbd2c785fbbb1`.
Wine Unix wayland driver SHA256:
`ad861f831b8dad2aed4a7e49a90cb418d9898e3a8aba3118ec7b611132f3c015`.
Both-PE wined3d hashes and all consumer binary identities are in results.json.
Synthetic GTX470 identification on the software GL route is not GPU proof.
Earlier explicit accelerated WSL D3D12/X11 controls remain separate evidence,
not acceleration proof for this native private protocol.

## Explicit limits and next work

- Partial foreign-target composition and generic foreign-HWND GDI still fail
  actual front pixels. r4-bridge-open-negative-controls intentionally returns1
  with0 pixels despite producer exit0. Fully covering opaque Chromium is fixed;
  generic foreign GDI is not. Only make these merge gates if a required real
  Chromium path uses them; do not expand into unrelated API completeness.
- Verify fractional-scale changes, HWND movement/reparenting and input on the
  actual shell. A client wl_surface that has already had a local subsurface
  role cannot simply acquire a different immutable role; migration of an
  existing drawable between these two attachment modes still needs explicit
  handling/testing. Do not claim arbitrary cross-process reparenting works.
- Repeat settled resize/frame checks. Native GPU-process recovery is now
  verified on this bridge: GL GPU PID532→940 with renderer652 unchanged;
  Vulkan GPU PID512→920 with renderer632 unchanged. Each sees one intentional
  GPU exit, no renderer exits, context-loss events in both WebGL versions,
  correct red default/green FBO readbacks, unchanged backend/features and
  continued frames. Inspected screen-28 captures show the frozen "GPU recovery
  verified" page, original blue square and two red WebGL canvases. These are
  actual compositor images, not solely post-crash animation/API results.
- Actual installed Classic/open Luna, locked/unlocked taskbar, toolbars and
  tray chevron validation remains. Do not conflate default unthemed diagnostic
  captions with installed-theme acceptance. Qwen's OOBE work is separate.
- Native accelerated virtual-GPU acceptance and remaining Chromium-relevant
  synchronization/sharing gates are still open. No physical GPU matrix required.
  Inherited upstream DXGI67 failures / legacy-sharing exit5 remain documented;
  they are not newly introduced by this bridge or blanket waived.

## Exact owned paths / continuation

Source `/var/tmp/wex-g3-repair-20261018/clean-source/wine`;
combined-WoW64 build `/var/tmp/wex-g3-repair-20261018/clean-source/build/wow64`.
Compositor source `ROOT/wexcomp-pinned-acc46fe`, build
`ROOT/wexcomp-build-acc46fe`, binary `.../labwc`; ROOT is the evidence root.
Config `/mnt/d/DeepThought/Projects/WineExplorer/scripts/wayland`.
Private cached runtimes and their hashes are those in the prior report.
Never publish/include those payloads in Git/ISO.

Always launch runners with `scripts/run-isolated-graphics.py --socket-root
ROOT/isolated-sockets -- ...`, using fresh output directories. Never rebuild
either shared owned binary during a probe. New VM storage/snapshots must be on
host D:. Existing VM/session and other workers' branches were not changed.

The positive foreign controls use `--case own-dcomp --case foreign-full-dcomp`;
default runner still includes the open negative cases. Each positive must show
exactly4096 pixels, submitted frame and exit0, not the original loose512 threshold.
The negative GDI control also expects4096 (it draws64x64).

## Failed commands/edits retained for diagnosis

- Initial bridge alone (`r4-render-bridge-vk-first`,
  `r4-render-bridge-electron-gl-first`) remained invisible. Missing child local
  data/foreign dummy root handling was repaired, but parentdata/routing controls
  still failed. `+d3d` trace then showed `Failed to capture application
  composition background.` The narrow opaque-cover change resolved the full
  target and real consumer cases; partial/GDI controls remain negative.
- `r4-render-protocol-first` link failed on
  `zwlr_foreign_toplevel_handle_v1_interface`; explicitly linked the generated
  `wlr-foreign-toplevel-management-unstable-v1-protocol.c`. Linked test passed.
- First final protocol command used `--wine-build
  /var/tmp/wex-g3-repair-20261018/build/wow64`; failed
  `FileNotFoundError: [Errno 2] No such file or directory:
  '/var/tmp/wex-g3-repair-20261018/build'`. Read-only find located the actual
  `clean-source/build/wow64`; r4-render-protocol-final-fixedpath passed9 controls.
- r4-render-protocol-destruction initially expected graceful surface-before-role
  destruction; runner failed `RuntimeError: Unexpected stage: ` after the client
  disconnected with `[destroyed object]: error 4: surface was destroyed before
  its role object`. Corrected the test to require this invalid-order rejection;
  r4-render-protocol-destruction-contract passes16 controls without production
  changes or suppressing valid-order failures.
- `python3 -B -m unittest discover -s
  /var/tmp/wex-g3-repair-20261018/clean-source/scripts -p
  test_g4_wexcomp_harness.py` returned `Ran 0 tests` / `NO TESTS RAN` because that
  current test file had not been copied there. Running the actual file under
  `/mnt/d/DeepThought/Projects/WineExplorer/scripts/` passes4, not zero-test success.
- Guessed `test_g4_consumer_probe.py` was absent (`[Errno 2] No such file or
  directory`); `rg --files` located `test_g4_consumer_harness.py`, which passes10.
  Likewise `Get-Content scripts/g4-electron-probe/index.html` failed because the
  fixture is generated inside main.js; the actual main.js was read instead.
- Initial unprivileged `git status --short` failed submodule Git metadata
  access (`Permission denied`); approved scoped metadata access succeeded.
  Linux `/usr/include/wlr` is not a PowerShell filesystem path; WSL read was
  used after that failed lookup. `src/wex-scene.c` was absent; actual path is
  `src/protocols/wex-scene.c`.
- Final read-only process inventory used `ps -eo pid,args | rg ...` inside
  Debian; failed `bash: line 1: rg: command not found`. Repeated with available
  `grep -E`: only the inventory shell/grep themselves matched, no owned test,
  compositor, Wine server or Xvfb remained. Both forks are committed and pushed;
  no owned sessions were left running. Usage at checkpoint:32% primary remaining,
  one-time reset credit untouched; no reset wake is needed at this threshold.

These runs add no source polling loop or sandbox bypass. Do not use historical
blank-output or the API-only pass labels as the current visual acceptance state.
