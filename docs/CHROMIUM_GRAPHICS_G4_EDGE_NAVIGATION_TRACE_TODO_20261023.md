<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Next observability candidate: response-to-renderer navigation boundary

Research/TODO ONLY, not implementation or live-run authority. Capacity plan and
maintenance review come first. Do not rerun the completed FILE/HTTP/stack arms.

## Evidence and question

Selected-stack R1 retained a complete HTTP200/637-byte response, no scene.js
request, no sampled renderer and white client. Browser main/IO logs show other
work; one sampled message wait does not prove a deadlock. Network errors for
external hosts are expected inside the offline namespace, not an identified
local navigation failure. Server write completion does not prove the browser
received/parsed the full response or committed a document. No renderer sampled
does not prove no renderer ever launched.

Question: how far does this exact nonce-bound local navigation progress between
network response handling, response throttles, renderer selection/launch and
commit? Prefer built-in bounded startup tracing over another generic stack
sample, remote symbols, process suspension or speculative production patches.

## Primary-source guidance, checked2026-10-23

[Chromium navigation lifecycle](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/navigation.md)
separates response handling, renderer selection/commit and document loading.
Navigation throttles can defer response processing. Those possible boundaries
do not identify which boundary our Edge build reached.

[Chromium startup recording guidance](https://new.chromium.org/developers/how-tos/trace-event-profiling-tool/recording-tracing-runs/)
documents bounded startup capture and warns that traces contain private URLs,
titles and hardware identifiers. Keep all actual traces private on D:.

[Public switch implementation at a fixed upstream revision](https://chromium.googlesource.com/chromium/src/+/a0e9c1e80c1bd2ab242a969fd460af4a07cae15e/components/tracing/common/tracing_switches.cc)
documents category selection, duration/output, JSON versus proto, and tracing
ownership. The documented default format is proto: a .json filename alone
does not establish JSON. This is reference guidance, NOT an exact Edge156
source/policy/feature guarantee. Do not reuse any no-sandbox examples found
elsewhere in tracing documentation.

## Requirements before choosing this experiment

- Establish relevant event names/categories and exact supported switches from
  public Chromium sources/reference evidence, without treating upstream HEAD
  as Edge's implementation or proprietary offsets as symbols.
- Select a finite capture duration covering the observed local request plus
  response/commit opportunity; conservative ring-buffer/file/parse limits,
  no all-events/unlimited logging. Missing/incomplete/truncated capture means
  UNAVAILABLE, never proof that absent events did not occur.
- Preserve the original app URL/page/backend/security, retained installation
  and fresh profile. Diagnostic-only startup trace flags, no DevTools endpoint,
  security bypass, feature disabling, external network or extra runtime install.
- Explicit trace format, ownership, private exact output path and archive/hash
  inclusion. Bound file growth during the run and all parsing after it. Capture
  before prefix cleanup; never require forced browser death to flush evidence.
- Correlate URL/nonce/navigation IDs, actual process metadata and sampled roles;
  keep Windows and Linux identifiers separate. State trace-clock limitations.
- Unit-test missing/oversize/invalid/incomplete traces, malformed IDs, unrelated
  navigations, nominal events with no matching URL and failure-front preservation.
- Normal readiness failure and diagnostic nonacceptance stay intact. An actual
  navigation trace is not pixel/GPU/shader/lifecycle/G4 acceptance. Parent reviews
  implementation/tests/fresh preparation before ONE separately authorized run.

If startup tracing cannot produce usable bounded evidence on this build, retain
that limitation rather than repeatedly trying formats/categories. Choose a
narrow alternative only after reviewing the actual result.
