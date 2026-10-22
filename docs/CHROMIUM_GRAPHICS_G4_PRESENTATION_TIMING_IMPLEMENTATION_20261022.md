<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# No-fault presentation timing implementation milestone

Implemented against published `e8a4a3ba` on `codex/g4-r1-completion`, following
the presentation timing plan, weekly checkpoint and independent repeat report.
This milestone is for parent code review. No VM was booted or accessed, and no
live graphics evidence was collected. Wine/compositor pins were not changed.

## Behavior and protocol

`check-g4-webview2.py --presentation-timing` requires the original Electron
fixture, at least 45 seconds, and the owned GLES2/VirGL/grim route. It rejects
recovery, lifecycle, freeze, profile/restart, resize-border and scale-transition
modes. The fixture also rejects conflicting mode environment variables before
dispatch. Its timing path does not call the GPU-termination routine.

After the existing five-second startup opportunity, it prepares the same
offscreen WebGL1/2 probes as recovery, checks their pixels/backend, freezes the
original scene at x=160 with the green background, and performs the bounded
two-renderer-rAF wait. The rAF result is logged; it remains a rendering
opportunity rather than proof of presentation. No scene mutation follows.

The fresh nonce request publishes observed outer/content/viewport/marker
geometry and background. The observer targets fronts at offsets 0, 250, 1000
and 3000 ms from its monotonic request receipt. Each sample records the target,
actual observation start/end, duration and lateness, plus the PNG capture's own
start/end/duration/offset/lateness. Geometry-query overhead is therefore visible
instead of being reported as punctual image capture. Slow slots are retained
once; they never trigger retries or pixel-dependent waits.

After all four PNGs are saved and decoded, the observer atomically publishes a
nonce-bound `fronts_complete` ACK, separately from pixel success. Only then does
the fixture issue its one bounded `capturePage()` request. It saves the PNG and
logs actual browser-main monotonic capture start/end/duration. The observer
decodes it, then targets the fifth front 250 ms after **receipt of the page
notification**. This receipt origin is explicit in the result: PNG writing,
read-only metadata IPC and observer polling can delay notification relative to
the actual page readback. The measured times must be used when interpreting
this arm. The final `page_and_post_complete` ACK permits the fixture's final
read-only geometry check and collection result.

The fixture owns a 30-second overall deadline after its startup opportunity;
the observer receives and respects its remaining budget. Browser-main bounds
cover preparation (8 s), ordinary renderer IPC (1.5/3 s), and page readback
(3 s), in addition to the existing renderer/main frame-wait deadline. Each
observer sample is bounded by 5 s including native HWND queries, grim and PNG
decode; native queries have 1.5 s subprocess bounds and grim has at most 3 s,
always capped by the remaining sample/overall budget. Synchronous decode work
is checked after return. Both clock domains are recorded separately.

Only the five explicit fronts and one page capture are enabled in this mode:
the runner's generic timed screenshots and the fixture's generic 12-second
page capture are suppressed. The normal metadata sampler remains read-only.
Native HWND scale/bounds and compositor process identity are checked around
every front; page and final scene observations must match the initial geometry.
Missing/corrupt PNGs, geometry/identity changes, malformed/stale/missing ACKs,
observer errors and expired bounds fail collection. Valid stale pixels remain
decoded evidence, including stale page pixels, and do not abort later samples.

`presentation_timing_diagnostic.collection_valid` controls this diagnostic's
exit status. It is separate from all pixel results and always retains
`acceptance=NOT_PROVEN`. Its descriptive classifications are correct before
page readback, stale then correct before page readback, stale until after page
readback, still stale, and invalid/inconclusive. Slots that overlap later
targets, inconsistent pixel transitions or a stale page make interpretation
inconclusive even if collection completed. Correlation with readback does not
assign a production cause. Existing recovery gates and schemas are unchanged.

## Files and deployment inventory

Changed production diagnostic files:

- `scripts/check-g4-webview2.py`: CLI/preflight, fixed observer timeline,
  geometry-aware pixel callbacks, atomic completion ACKs and separate result.
- `scripts/g4-electron-probe/main.js`: explicit no-fault dispatch and original
  scene preparation; suppress generic page capture only for timing mode.
- `scripts/g4-electron-probe/presentation-timing.js`: original, lazily loaded,
  dependency-injected protocol and browser-main deadlines.

Executable tests are `scripts/test_g4_presentation_timing.py`,
`scripts/test_g4_presentation_timing.js`, and
`scripts/test_g4_presentation_timing_fixture.js`. The latter runs actual main.js
with an isolated Electron/renderer test double and drives it beyond the generic
capture time, proving a single gated readback, no termination call, frozen
scene, and conflicting-mode refusal. Ordinary Electron startup is still tested
with optional diagnostic helpers absent.

For the later parent-approved guest run, stage/hash **the complete timing
payload inventory returned by `electron_payloads(..., presentation_timing=True)`**:
Electron binary, main.js, package.json, recovery-frame-wait.js,
presentation-timing.js, check-g4-webview2.py, check-g3-correctness.py,
graphics_resources.py, graphics_frame_pixels.py, graphics_wexcomp.py,
analyze-webview2-run.py, run-isolated-graphics.py and
test_g4_native_window_state.cpp. No project-local includes are present in this
native probe. Timing preflight refuses missing inventory files before launch;
results hash every payload and the compiled native probe, plus the existing
Wine stack/compositor identities. Ordinary controls do not require timing or
frame-wait JS helpers. Preserve source/deployed hashes before guest execution.

## Validation

Linux/WSL focused command (63 tests, PASS):

```sh
cd /mnt/d/DeepThought/Projects/WineExplorer/scripts
python3 -B -m unittest test_g4_presentation_timing test_graphics_frame_pixels test_g4_consumer_harness test_g4_wexcomp_harness test_webview2_analysis test_g4_virgl_launcher
```

Windows Node executable controls (all PASS; Linux Node is unavailable):

```powershell
node scripts/test_g4_presentation_timing.js
node scripts/test_g4_presentation_timing_fixture.js
node scripts/test_g4_recovery_frame_wait.js
node scripts/test_g4_electron_fixture_no_recovery.js
node --check scripts/g4-electron-probe/main.js
node --check scripts/g4-electron-probe/presentation-timing.js
```

Tests execute fixed scheduling and capture callbacks with fake clocks, actual
decoded synthetic PNGs at fractional scale and a nonzero client origin,
lateness (including native-observation overhead), retained stale front/page
evidence, descriptive classifications, no early/extra readback, duplicate or
bad events, missing/stale/bad ACKs, bounded hung preparation/readback, timeout
cleanup, observer/geometry failures, atomic ACK publication, CLI exclusions,
dependency preflight, normal startup and actual no-fault fixture dispatch.
`git diff --check` passed. No Windows shims were used for Python/Linux coverage.

## Review boundary and remaining work

Parent review precedes the first live timing run. Prepare the full manifest
and use the already reviewed persistent controller/identity stopper, fresh D:
outputs and prefixes, and the existing 900-second/cleanup reserve procedure.
The first timing run should leave `--trace-native` off; any matched heavy
trace arm needs separate parent scope after inspecting these results. Retain
all front/page PNGs and complete Wine/compositor/capture/native/process logs.
Visible WebGL recovery, independent recovery repeat and postfault regression
controls remain TODOs after the healthy presentation discrepancy is understood.
