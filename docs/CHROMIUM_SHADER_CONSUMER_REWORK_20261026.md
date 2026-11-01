<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Antigravity: correct shader consumer false passes

Same own D: codex/chromium-shader-consumer-validation branch, currentlye92a8a48.
Read parent's CHROMIUM_R6_AND_SHADER_PARENT_REVIEW_20261026.md. Preserve original
fixture and prior history. This task is disjoint from R6 execution: source-only
checker/tests/report changes, NO VM/SSH/Wine/browser/probe/build/token work.
R6 execution has priority; a separate local worker may do this offline task.

Required small fixes:

1. A reference-only image must fail even when caller assigns both webgl/webgl2
   bounds to reference. Reject aliased/overlapping region mappings. Since current
   fixture is fixed64x64 at scale1, narrowly supporting those actual dimensions
   is sufficient. Do not accept arbitrary caller shapes as proven canvas geometry.
2. Blank image with all five bounds [0,0,1,1] must never pass. Validate actual
   geometry dimensions and ALL solid/quadrant sample block extents before loops;
   each required region must yield nonempty actual pixel comparisons. Invalid,
   missing, partial/malformed explicit bounds must not silently fall back to a
   different layout. Document exact default-vs-explicit mapping contract.
3. Add bounded title/record/renderer/nesting/node handling suitable for the small
   fixture. Reject overquota strings and recursive/deep malformed data as a
   structured non-success verdict, preserve bounded primary evidence. Validate
   finite coordinates before integer conversion; NaN/Infinity/invalid types must
   return fail/inconclusive rather than crash. No generic parser framework needed.

Parent reproductions (existing test helpers):
- img=_front(include=('marker','reference')); g=_geometry(); set webgl and webgl2
  bounds to copies of reference bounds: combined passed=True (incorrect).
- black420x300 image; all five bounds=[0,0,1,1]; canonical record: passed=True.
- canonical dict with webgl.renderer='x'*2000000: API status pass.
- g.client=[float('nan'),0]: validate_presented_pixels raises ValueError.

Add independent focused regressions for these exact cases and nearby overlapping,
too-small, empty-sample, partial explicit bounds, nonfinite and overquota cases.
Retain valid canonical/default-scale1 and translated-client controls, both API
and pixel verdicts and original source failure. Existing39 tests and Node35/28
suite should remain meaningful. Do not weaken assertions or call synthetic images
real rendering evidence. Review resulting record budget against actual fixture
title size; preserve public simple invocation and two exact existing HTTP routes.

Update original report with actual tests/limitations and parent-failure corrections.
Commit AND push; reply codex with exact commit/counts, then stop for re-review.
No parent/R6/launcher/build/runtime/checkouts edits. All private test images D:.
