<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# No-fault presentation timing experiment

Prepared after the user reset both usage windows and authorized continuation
on 2026-10-22. Parent 8a0b3829, codex/g4-r1-completion. This supersedes the
weekly-budget stop for the newly reset window. Retain five-hour 95%-used and
weekly 99%-used checkpoint limits. Parent plans/reviews; GPT-6 Luna implements.

## Question

In the independent repeat, two renderer rAF callbacks completed, but the front
still showed Frames:14 while capturePage showed the frozen marker at x=160.
No fault occurred. Determine whether that front becomes correct spontaneously
before page readback, only after readback, or remains stale. These observations
localize the next investigation; correlation alone does not prove causation.

## Bounded implementation

Add an explicit Electron-only --presentation-timing diagnostic mode to the
existing runner/fixture. It is mutually exclusive with GPU recovery, lifecycle,
freeze-scene and restart modes, and uses the same owned accelerated VirGL/GLES2
route. Keep recovery's existing acceptance schema and defaults unchanged.

Prepare the same WebGL1/2 contexts and frozen original scene as the recovery
pre-fault path, including the bounded two-rAF opportunity wait. Publish a fresh
run nonce, observed HWND/content/viewport/marker geometry and fixed scene color.
No process termination is reachable from this mode. Keep the scene unchanged
through the final capture.

The observer records four front captures at fixed offsets from receipt of the
timing request: immediate, 250 ms, 1 s and 3 s. Record actual monotonic start/end
times, target offsets and lateness for every capture; a slow capture must not
be disguised as an on-time sample. Do not add retries or wait for pixels to
pass. Decode each PNG and retain marker/background/rim verdicts, including
failed pixels. Reuse geometry-aware helpers and owned compositor identity
checks. Geometry changes invalidate the comparison.

Only after all four fronts are saved does the fixture call capturePage once.
Then collect an additional front sample after 250 ms, recording actual times.
Use separate nonce-bound completion ACKs, distinguishing captured from pixel
success. Missing/corrupt captures, stale ACKs, observer exceptions and bounded
timeouts fail the diagnostic; a valid stale image is retained evidence, not
grounds to abort collection. Use one documented overall deadline plus bounded
capture/IPC calls. If a target slot is late, preserve that fact.

The fixture's existing generic timed capturePage (normally at 12 seconds,
20 seconds for freeze mode) MUST be suppressed in this mode. Audit all other
capturePage call sites so there is no earlier incidental page readback. Keep
the normal metadata sampler read-only. Suppress the runner's generic timed
front screenshots in this mode too; only the documented timeline should issue
screencopy requests. No forced repaint, resize or page-first capture to make
the front pass.

Output a separate presentation_timing_diagnostic with collection validity and
per-sample pixel results. A complete collection may contain failed front pixels;
it is not rendering/recovery/G4 acceptance. Record an explicit descriptive
classification (correct before page readback, stale then correct before it,
stale until after it, still stale, or invalid/inconclusive) without assigning a
production cause. Keep acceptance=NOT_PROVEN.

Add executable tests with fake clocks/capture callbacks for fixed order,
lateness reporting, front mismatch retained through page/post-page collection,
no early page readback, missing/bad ACKs, timeout and observation failure. Test
CLI exclusions and no-fault dispatch. Avoid source-string-only assertions.
Require/hash/stage any new fixture helper only in modes that use it; preserve
normal Electron startup without optional diagnostic helpers.

## Review and live experiment

First implement and test without booting the guest; commit/push and return for
parent review. Parent independently reviews code and tests before a live run.
Then use only the D:-backed owned VirGL guest, persistent session controller,
verified deployed hashes and identity-validated SIGINT/QMP cleanup. Prepare
commands/dependency manifest before boot, reserve cleanup inside 900 seconds,
and preserve complete Wine/compositor/capture/process logs and PNGs on D:.
Run one no-fault timing experiment first; inspect actual images before deciding
whether a second controlled arm or targeted surface/commit tracing is needed.

No production Wine/compositor patch, extra sleep/retry in recovery, fault trial,
post-fault regression matrix or master merge belongs to this implementation
milestone. Visible WebGL recovery remains high priority after readiness is
understood; installed-shell/efficiency work remains recorded TODO.

## First result and bounded repeat

Parent reviewed the first live collection (report ed5229e8): experiment and
collection valid, all five fronts/page correct, first actual PNG capture started
64.986 ms after observer request receipt, all five front PNG hashes identical.
Actual first image shows the full frozen scene. Independent shutdown review
confirmed graceful QMP powerdown, both children stopped, QEMU and Windows WSL
exit 0. This does not reproduce or explain the earlier stale-front failures.

Next collect at most two additional serial fresh-prefix/profile/output timing
runs with identical reviewed code, binaries, flags and no heavy tracing. Reuse
the prepared host procedure with unique run names; prepare both commands and
dependency manifests before boot. Each experiment keeps its own compositor,
nonce and evidence. Preserve the shared guest/cache context in the report.
If either run has stale pixels or a collection/health failure, finish collecting
that experiment, stop further runs, shut down cleanly and return for parent
review. Do not retry images or change the measurement to obtain a preferred
outcome. If both are correct, report a bounded non-reproduction, not a fix.
Keep the 900-second launcher/90-second cleanup reserve and standing usage stops.
