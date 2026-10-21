<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 frozen resize: retain the frame submitted before binding

The selected persistent-content-loss issue is fixed in scoped controls.
Wine `4315be83d75644f13500381dcec94dcdc5fa904e` is pushed on
`codex/g4-r1-completion`; compositor `94faa651` is unchanged. This supersedes
the unfixed diagnosis in FROZEN_RESIZE_CHECKPOINT_20261021, not the other G4
acceptance gaps. No master merge, sandbox bypass, OOBE/token imports, runtime
publication or proprietary source/art was involved.

## Cause and narrow repair

Chromium submits Present1 on its replacement composition swapchain **before**
attaching it through DComp Commit. DXGI returned S_OK without retaining anything
when `comp_wnd` existed but `target_hwnd` was NULL. The subsequent Commit had no
submitted frame to display. Animation eventually hid that loss by submitting
again after binding; a frozen DOM exposed it indefinitely.

Narrow temporary DXGI scene-binding traces in
`r4-frozen-scene-bindings-vulkan/wine.stderr.log` establish this ordering.
The new independent API regression reproduces the lost frame/present count
without Chromium or the private Wayland protocol. This is not a compositor
stacking workaround, extra redraw, forced DOM update or production polling loop.

DXGI now queues an internal capture-only Present for an unbound composition
swapchain. WineD3D creates the same owned retained-frame object used by bound
composition. Its GL/Vulkan command-stream paths capture the producer image and
perform normal fence/latency/buffer-rotation bookkeeping, without drawing or
marking a front drawable as updated. Commit later uses that snapshot, not an
unpresented producer backbuffer. GL uses the existing independent GPU copy;
Vulkan uses the existing reference readback path. Existing format/size bounds
apply before capture; unsupported renderers fail honestly. No new public API.
Temporary trace statements were removed before final controls.

## Regression and evidence

Evidence root `ROOT=/var/tmp/wex-g4-tree-20261020` (local, untracked).
Owned combined-WoW64 build remains
`/var/tmp/wex-g3-repair-20261018/clean-source/build/wow64`.
All runners use `run-isolated-graphics.py --socket-root ROOT/isolated-sockets`
with fresh output directories and per-run filesystem X sockets. No rebuild
occurred during any owned probe.

The new `unbound_present_retention` public-API group verifies:

1. Present before the first Commit increments the count but does not change
   the actual visible target-window pixel.
2. Rendering green into the backbuffer WITHOUT another Present cannot replace
   the submitted red/blue snapshot.
3. Commit displays exact red/blue pixels, and a later offset-only Commit still
   uses that snapshot without a fresh Present.
4. Both BGRA/RGBA formats and IGNORE/PREMULTIPLIED alpha modes pass, on both PEs.

| Run under ROOT | Result |
| --- | --- |
| r4-unbound-retention-before | Old DXGI shortcut: count0, missing red/blue and re-Commit pixels, both PEs |
| r4-frozen-resize-postmove-trace-vulkan | Previous full60sec real consumer: settled blue0, diagnostic4, host0 |
| r4-frozen-scene-bindings-vulkan | Trace-only negative real consumer, same functional loss |
| r4-unbound-retention-fixed-gl / -vulkan | Initial29-group suite PASS both PEs |
| r4-capture-final29-retry-gl / -vulkan | Final trace-free candidate: all29 groups PASS both PEs, four alpha/format combinations |
| r4-capture-final-g0-g3 | G0-G3 plus11 ordering groups PASS both PEs |
| r4-capture-final-pacing | Five pacing/latency groups PASS both PEs |
| r4-frozen-capture-only-gl / -vulkan | Real Electron60sec lifecycle + frozen held resize, host0/diagnostic0 |
| r4-frozen-capture-final-gl / -vulkan | Repeat on final binaries: same PASS, native winewayland observed, clean teardown |

Actual compositor PNGs were inspected, not just API results or RAF counters:
the frozen green page, blue square, menu, caption and full frame are present.
All four real runs have exactly1600 blue pixels and complete left/right/bottom
rims at BOTH settled2/4-second samples. Later saved screen-18 images also have
1600 blue pixels in both final runs. Lawfully cached Electron44.9.2/Chromium152
and the original fixture remain private; identities and DLL hashes are in each
results.json. Compositor is headless pixman; clients software GL/lavapipe.
This is native private-endpoint functional evidence, not installed-shell or
accelerated-native acceptance and not a sandbox-security proof.

### Timing limitation deliberately retained

Early samples remain negative. Initial Vulkan run briefly has blue0 at1sec;
final GL run has560 at1sec. Other early samples show incomplete outer rims.
All settle to complete exact content by2sec and retain it at4sec. This fixes
persistent disappearance, **not atomic/flicker-free resize**. The pre-existing
oracle still records these failures and requires BOTH content and complete
rims in its settled samples. No assertion was weakened to obtain acceptance.

## Diagnostic corrections / failed commands

- The first baseline probe also assumed the window's black GDI backing was
  already visible. It was actually white at that point. Changed only this
  invisibility assertion to compare real front pixels before/after Present,
  rather than assume backing delivery. The baseline count0/missing retained
  red/blue failures are independent of that correction. The expanded final
  four-combination test was not run unchanged against the old binary.
- `r4-capture-final29-gl` and `-vulkan` initially failed compilation:
  `deducing from brace-enclosed initializer list requires '#include <initializer_list>'`.
  Replaced brace-list range expressions with typed arrays; fresh `-retry-`
  runs pass both PEs. Production code/build was not affected.
- Unescalated `git status --short` / `git -C wine status --short` failed because
  `.git/modules/wexcomp` and `.git/modules/wine` were denied by the sandbox.
  Approved same-workspace read access allowed inspection; no checkout/reset.
- Searching for an existing capture export returned no match (rg exit1).
  Used a private WineD3D Present flag instead of inventing a new exported ABI.
- `cd /var/tmp/wex-g3-repair-20261018/clean-source/scripts && python3 -B -m
  unittest test_graphics_frame_pixels test_g4_consumer_harness` failed with
  `ModuleNotFoundError: No module named 'test_graphics_frame_pixels'`: that
  staging directory has older unit-test copies. Repeating from
  `/mnt/d/DeepThought/Projects/WineExplorer/scripts` ran the current3 oracle
  and11 consumer harness tests: all14 PASS. Runner/fixture sources used in the
  real probes were already current; this affected only the unit-test command.
- One first display of the copied GL PNG appeared black; reopening the final
  completed file displayed correct content. Both copied/final source files
  had SHA256 `ba0600d92d8c623cb5f5e95832923daad3f93ed364c06e61e0460252944fcdf8`.
  Do not treat that display anomaly as a second renderer failure.

## Handoff / remaining scope

Selected issue complete; do not automatically expand into unrelated gaps.
Still no G4 merge-ready claim. The native accelerated virtual-GPU and actual
installed Classic/open Luna/taskbar/toolbars/tray acceptance in NATIVE_GEOMETRY
and NATIVE_BRIDGE remain separate. Generic foreign GDI/role migration are only
gates if a required Chromium path uses them. No physical GPU matrix required.
Other workers' OOBE/token branches, VM sessions and builds were untouched.

Debian relocation authorization remains active. The earlier111GiB cleanup is
complete; no further old-prefix copies were recreated and no additional cleanup
was needed. Fresh run prefixes/evidence remain owned and retained for diagnosis.
All probes exited; final owned-process environment inventory is empty and each
real consumer records `remaining_servers=[]`. Current usage23% consumed in the
five-hour window; no reset credit used. Selected issue is complete, so the
one-time continuation automation was removed rather than scheduled again.
OpenAI Docs skill guided only that automation housekeeping, after checking
[official scheduled-task documentation](https://developers.openai.com/training/walkthroughs/scheduled-tasks).
All test processes must finish before a subsequent rebuild. Never publish the
private runtimes or diagnostic consumer payloads; any new VM storage stays D:.
