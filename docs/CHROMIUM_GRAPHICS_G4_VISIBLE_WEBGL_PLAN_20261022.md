<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Visible WebGL accelerated recovery: bounded implementation plan

Parent plans/reviews; GPT-6 Luna implements on codex/g4-r1-completion.
This follows three no-fault timing collections with correct frozen fronts
(reports ed5229e8 and e1eca023, independently checked by the parent).
Those measurements did not reproduce or fix the earlier intermittent stale
front. Keep that failure open; do not attribute historical post-fault blanking
to it. No Wine/compositor production change is justified by these results.

## Value and scope

The current recovery probe creates offscreen WebGL1/2 canvases. Exact readback
therefore does not prove that either recovered canvas reaches the actual
window. Add an explicit visible-canvas variant of the existing original fixture,
with independently decoded front AND page pixels. Preserve existing defaults,
backend/identity/error gates and the rule against faulting an unhealthy control.

Implementation and executable tests are the first milestone. No guest boot,
fault injection, production patch or merge before parent review.

## Required implementation

- Add one opt-in CLI/fixture switch, permitted only with the existing Electron
  presentation-timing or GPU-recovery modes. Reject unsupported combinations
  before launching. Record the variant explicitly in results and manifests.
- Reuse the existing WebGL1/2 creation/readPixels/FBO checks. In visible mode,
  place two separate opaque canvases fully inside the observed viewport,
  away from the marker, its background sample, text, controls and frame rim.
  Use deterministic original flat-color content, not vendor art or a CSS
  imitation of a WebGL surface. Do not add canvas backgrounds that could pass
  when GPU content is absent. Set dimensions explicitly; publish observed
  getBoundingClientRect geometry, API identity and intended stage.
- Distinguish WebGL1 from WebGL2 with different exact colors. Also distinguish
  pre-fault from recovered content with a different palette, so an old retained
  pre-fault canvas cannot pass recovered checks. Select expected palettes in the
  Python oracle from the trusted diagnostic stage, not just renderer-supplied
  claimed colors. Keep the existing red default/green FBO API readbacks or
  explicitly preserve their equivalent independent checks before the final
  visible clear. Freeze visible content through each capture checkpoint.
- Assert both canvas interiors in every explicit front/page capture, alongside
  existing marker/background/frame checks. Derive screenshot coordinates from
  native client origin, observed CSS rectangles and scale; validate finite,
  positive, nonoverlapping, in-viewport geometry and sufficient sample area.
  Use inset regions, not antialiased edges. Missing one canvas, wrong/swapped
  colors, stale pre-fault colors, clipping, bad geometry or an empty region must
  not pass. Include per-canvas expected/actual/counts and geometry in evidence.
- Require consistent canvas observations across corresponding front/page
  records and final observations. Keep collection completion separate from
  pixel success: retain failed front PNGs and collect the paired page, but
  never inject a fault if the pre-fault visible check fails. After a fault,
  preserve later bounded stages even if an earlier valid image has bad pixels.
- Keep the timing mode strictly no-fault, fixed offsets and single page readback.
  Extend its geometry/protocol validation without weakening experiment health.
  Suppress incidental generic front/page captures for explicit recovery too:
  its event-aligned stages should be the only capture operations. Add an
  executable regression proving no generic 12-second readback can perturb
  recovery. Do not introduce pixel retries, arbitrary readiness sleeps,
  page-before-front ordering, forced invalidation or sandbox/backend overrides.
  Report the capture-schedule change explicitly: historical recovery runs had
  incidental captures, so results are not a matched reproduction of those runs.
- Stage/hash any new helper through the existing inventory; optional diagnostic
  dependencies must remain optional for ordinary Electron startup.

Prefer small reusable geometry/pixel helpers and existing protocols. Do not
build another orchestration system or refactor unrelated lifecycle code.

## Tests and parent review

Run actual executable Python/Node tests, not source-string-only checks:

1. Synthetic decoded front/page images: both correct, each independently
   missing/black/wrong/swapped, stale recovered palette, wrong origin,
   fractional scale with nonzero client offset, NaN/infinity/zero/outside or
   overlapping geometry and undersized sample regions.
2. Fixture execution with Electron/renderer doubles: distinct real WebGL clear
   operations and stage palettes, both API probes retained, geometry published,
   visible content stable, bad pre-fault ACK prevents process.kill, valid bad
   front still permits paired page evidence, and no incidental readback.
3. Timing/recovery integration: missing canvas metadata fails closed only in
   visible mode, front/page disagreement fails, nonce/identity/error/fault-count
   gates remain unchanged; timing mode never reaches fault dispatch.
4. Existing timing, frame pixel, consumer/native harness, analysis and fixture
   controls remain green. Ordinary controls run without optional helpers.

Commit AND push the bounded implementation with a concise report listing tests
and limitations. Parent independently inspects the patch and reruns focused
controls before authorizing live work.

## Proposed live sequence, requiring subsequent parent dispatch

Use only the owned D:-backed VirGL guest and established isolated procedure.
Prepare manifests/commands before boot; reserve 90 seconds for verified graceful
shutdown inside the 900-second launcher lifetime. No unrelated VM changes.

First one fresh-prefix visible no-fault timing control, inspect actual fronts
and page. If unhealthy/stale, preserve evidence and stop: no fault is permitted.
If reviewed healthy, separately authorize one fresh-prefix visible recovery
trial, exact owned GPU child only. Inspect actual pre-fault, recovered, 2s and
4s images and exact WebGL/backend/identity/error evidence. A failure returns to
diagnosis, not repeated attempts to obtain a pass. Independent recovery repeat
and post-fault x86_64/i686/WebView2 controls remain later milestones.

This is additional consumer coverage, not resolution of the historical blanking
or G4/full Edge/merge acceptance. Follow standing usage checkpoints: stop safely
at 95% five-hour or 99% weekly used; never consume reset credits.

## First recovery reviewed; independent repeat

After the user authorized continuation, both usage windows were verified at 0%.
Parent reviewed trial/report 50012793: exact source/helper/archive hashes matched,
all eight stage PNGs passed, and the inspected actual settled front contains the
whole scene/frame and new yellow/blue WebGL surfaces. GPU child 528 was replaced
by 912; browser/renderer/compositor identities and backend/features stayed stable,
both old contexts reported loss events and new contexts passed exact readback.
Parent independently verified all 39 archived file hashes, full recovery and
capture gates, and graceful QMP/QEMU/Windows exit 0. This is ONE scoped pass,
not a historical failure fix or full G4 acceptance.

Next authorize exactly one independent fresh-prefix/profile/output visible
recovery repeat, unique run name visible-webgl-recovery-r2-20261022, identical
reviewed source/binaries/flags. Prepare its manifest before boot, use a separate
owned boot and the existing isolation/cleanup procedure. No extra timing control,
tracing, diagnostic edits or image retries. Any unhealthy pre-fault gate must
prevent fault injection. Preserve failures and all available paired evidence,
shut down and return for parent review, even if the run fails before faulting.
Post-fault both-PE/WebView2 controls are separately dispatched after review of
the two trials; do not silently credit historical pre-fault controls.
