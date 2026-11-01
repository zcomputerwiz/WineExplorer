<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Visible WebGL first milestone — implementation only

Implements the first milestone of `CHROMIUM_GRAPHICS_G4_VISIBLE_WEBGL_PLAN_20261022.md`.
No guest actions, VM boot, live capture, fault injection or Wine/compositor changes.
Return to parent review before any live arm. No G4/recovery/root-cause acceptance.

## Bounded changes

- `scripts/check-g4-webview2.py`: opt-in `--visible-webgl`, rejected outside
  Electron timing/recovery before launch; environment and `fixture_variant`
  recorded alongside the existing payload/dependency hash inventory in results.
  Both capture paths use the extended oracle; missing/disagreeing visible
  metadata fails closed. Existing nonce, process/backend, health and identity
  gates remain. No new staging dependencies: all modified runtime files are
  already covered by the existing inventory. Future deployment manifests must
  retain the variant/flags and newly computed hashes, not reuse old hashes.
- `scripts/g4-electron-probe/main.js`: two opt-in opaque 64x64 real WebGL
  surfaces, no CSS color fallback. Pre-fault WebGL1/2 are magenta/cyan;
  recovered WebGL1/2 are yellow/blue. Red default and green FBO readbacks run
  independently before final visible clears. Real DOM canvas/control geometry,
  API and stage observations accompany front/page records. Corresponding page
  observations must match front observations; the timing helper also checks
  its final observation. Existing pre-fault gate and front-before-page protocol
  remain; a valid bad post-fault front still gets its page and later stages.
- `scripts/graphics_frame_pixels.py`: trusted-stage palettes, finite positive
  in-viewport nonoverlapping geometry, protected marker/background/text/control
  rectangles, native client origin and observed scale. Each canvas's inset
  requires every sampled pixel to match; reports API/stage/rectangle, region,
  expected/actual color and sampled/matched counts. Front and page remain
  independent, combined with existing marker/background/rim checks.
- `scripts/test_g4_visible_webgl.py`, `scripts/test_g4_presentation_timing.py`,
  `scripts/test_g4_presentation_timing_fixture.js`: decoded synthetic PNG and
  actual fixture execution coverage. The actual `observeScene()` executes in
  renderer doubles and parses RGB correctly with the single-backslash digit
  regex. Optional-helper-free normal startup remains covered separately.

Recovery now suppresses generic timed front captures AND the generic 12-second
page readback. Only explicit stage captures run. Executable fixture tests advance
past 12 seconds and check all expected front/page pairs without an extra readback;
the real runner schedule function is tested for empty recovery/timing schedules.
Historical recovery runs included incidental captures: this is a schedule change,
not a matched reproduction of them. Timing offsets/readback count are unchanged.

## Verification

From the repository root, Debian WSL Linux Python:

```text
wsl -d Debian --exec bash -lc "cd /mnt/d/DeepThought/Projects/WineExplorer && python3 -m unittest discover -s scripts -p 'test_g*.py' && python3 -m unittest discover -s scripts -p 'test_webview2_analysis.py'"
```

PASS: 83 + 7 = 90 tests; no skips. Includes frame pixels, consumer/native
harness, wexcomp, launcher, presentation timing, visible WebGL and analysis.
Expected CLI rejection diagnostics are emitted by negative tests.

Windows Node commands, each PASS (exit 0):

```text
node scripts/test_g4_recovery_frame_wait.js
node scripts/test_g4_presentation_timing.js
node scripts/test_g4_electron_fixture_no_recovery.js
node scripts/test_g4_presentation_timing_fixture.js
node --check scripts/g4-electron-probe/main.js
git diff --check
```

New fixture execution checks real clear/readback operations, observed geometry,
timing's unreachable fault dispatch, failed pre-fault preventing kill, and bad
post-fault fronts preserving all four paired stages. Python tests reject each
missing/black/wrong/swapped/partially corrupted surface, stale pre-fault colors,
wrong origin/scale, clipping/overlap/nonfinite/undersized geometry and missing or
disagreeing observations. A valid 1.25-scale control has a caption/menu offset
distinct from outer HWND bounds. Existing nonce/compositor identity gates pass
controls and reject mutations.

## Limitations and next review

Synthetic pixels and Electron/renderer doubles do not prove actual GL composition,
presentation readiness, context recovery, or a historical blanking fix. Fixed
canvas placement intentionally fails closed if the real viewport/text geometry
cannot accommodate it. Inset checks do not validate antialiased edges or glyphs.
No new helper/runtime dependency was introduced; ordinary mode stays offscreen.
Parent should independently review/tests before separately authorizing the plan's
fresh-prefix visible no-fault timing arm. Any unhealthy/stale live control must
stop before a separately authorized recovery experiment.
