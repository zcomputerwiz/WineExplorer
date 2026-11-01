<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Antigravity: prepare real-browser shader/texture acceptance, offline only

Goal: after Chromium starts normally, prove original shaders and a tiny uploaded
texture reach both WebGL1/WebGL2 and the presented front window. This is a
complementary task, not another startup/token/VM harness. Luna currently owns
actual R6 host/guest preparation; preserve that work and its comparison fixture.

## Branch and source ownership

Create an isolated own D: root branch codex/chromium-shader-consumer-validation
from this parent work-order commit. Reuse the already reviewed original fixture
on codex/chromium-webgl-shader-fixture at0d96719a277b96847838196f0d2a167e18192f5a:
scripts/g4-shader-probe/{index.html,scene.js}, scripts/test_g4_shader_scene.js,
docs/CHROMIUM_WEBGL_SHADER_FIXTURE_20261025.md. Preserve its history by picking
its four fixture commits if clean, or record exact copied source provenance.
Parent accepted its35-negative/28-mutation synthetic tests; do not recreate GL
from scratch. Do not touch Wine, compositor, main master, parent C: checkout,
registration branches, Luna R6 checkout/build/prefix or proprietary runtimes.

Disjoint write scope: accepted fixture files above; NEW
scripts/g4_shader_consumer_evidence.py, its focused test file, and an original
report docs/CHROMIUM_SHADER_CONSUMER_VALIDATION_20261026.md. NEW helper files for
fixture-specific validation are allowed if needed. Do NOT modify R6 native
runner, native helper/contract, module-view/probe scripts, R5/R6 adapters,
private preparation, launcher, token code or existing general HTTP server.

## Task A: reusable shader evidence checker

Implement a small Python checker using existing project conventions for offline
fixture evidence. Inputs are actual externally captured browser title/record,
expected nonce, a front-window/client snapshot and explicit capture geometry /
scale metadata. It must not fabricate readiness or capture anything itself.
The eventual parent/Luna browser adapter will supply these inputs after R6.

Validate strict WEXSHADER v1 records from scene.js: matching32-lowercase-hex
nonce, exact expected two APIs, success flags, solid and texture64/64 matches,
no GL/compile/link errors/context loss, expected quadrant/corner orientation,
and bounded actual strings/data. Non-success/incomplete/malformed records fail.
Keep renderer text diagnostic; it alone is NOT acceleration or sandbox proof.
Reject stale/mismatched nonce and post-ready context-loss state. Preserve source
record/error in diagnostic output rather than replace it with a generic pass.

Separately inspect the presented image: require the marker/background, correct
four-quadrant colors/orientation in BOTH WebGL canvases, and a consistent DOM
reference. Sampling only the DOM reference must never pass. Reject blank/stale/
missing-one-canvas/flipped-quadrant fronts even if the title says passed:true.
Compare interior regions away from borders/text/antialiasing, with explicit
client-origin/scale/crop bounds. Do not use a fuzzy global search that lets the
DOM reference stand in for both canvases. If geometry/scale cannot be proven,
return unsupported/inconclusive, never success. Initial support for the actual
lab default96DPI/scale1 is enough; record other scales as TODO unless simple.

Use existing image libraries, not a new dependency/toolchain. If fixed CSS label
line-height/layout is needed for deterministic regions, make only that minimal
fixture layout change, preserve original GL/texture workloads, and document the
new exact geometry. Alternatively consume real per-canvas bounds with explicit
scale/client capture mapping; do not assume guessed desktop coordinates.
No screenshot is allowed to certify browser sandbox, GPU-process isolation or
hardware backend. Return distinct API-result and presented-pixel verdicts; combined
rendering acceptance requires both plus external actual backend/ownership proof.

## Task B: focused tests and integration handoff

Meaningful tests exercise the real checker against generated ORIGINAL diagnostic
images and real fixture records. Cover canonical two-canvas success, marker-only,
DOM-reference-only, blank, one canvas missing, reversed quadrants, wrong nonce,
truncated/malformed/duplicate/overquota record, context loss, incorrect geometry /
crop/scale, and failure primary evidence retained. Generated images are synthetic
unit controls, NOT real browser/GPU proof. Use independent expected color/layout
constants or explicit fixtures so implementation-generated success isn't its own
oracle. Run accepted Node shader suite and relevant legacy fixture tests if
changed; no broad repeated suite. Do not build a general GL emulator or exhaustive
security/conformance project. Keep distinct validation of output record vs actual
original C/JS execution; don't call handcrafted passed JSON a real browser run.

Report exact supported geometry/schema, commands/results/counts, public reusable
entry point, how to serve the unchanged two exact nonce HTTP routes via existing
g4_edge_http_fixture.FixtureServer, and the minimal next integration touch points
for parent/Luna. Include an example checker invocation reading files but NO new
VM/startup command authority. Original R6 must keep its existing startup fixture
for a controlled comparison; this new workload is a later opt-in trial.

## Deliverable and stop

Commit AND push your isolated branch/code/tests/original report. Reply codex with
commit, files, test outputs and limitations, then stop for parent review. No VM,
SSH, Wine/browser/probe execution, package downloads/installations, runtime/DLL
build, token merge, master merge or proprietary payload/art publication. All
private test image/evidence storage D: or D:-backed WSL. Source-only tests may run
locally. Historical ID/hash/version differences diagnostic; actual wrong-owner /
overwrite/function errors still stop. At95%five-hour usage finish bounded work,
checkpoint/push; weekly99%stop. Never consume reset credits. Existing local worker
delegation within your setup is allowed; avoid touching the other Claude token
instance or creating a duplicate R6 implementation.
