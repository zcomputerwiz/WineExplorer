<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Second-pass fix-it work order for Gemini: OOBE components

Policy update 2026-10-19: `OOBE_EMBEDDED_TERMS_WORKORDER.md` supersedes older
blanket bans on payload download before acceptance ONLY for an explicit
download-for-review action, static terms extraction, and separate subsequent
install acceptance. Its published repaired baseline supersedes this historical
starting point; unrelated protections and acceptance gates remain in force.

Date: 2026-10-18. Reviewed tip: `9370bbd9`, `gemini/oobe-components`.
Status: not merge-ready; implement and verify the remaining repairs below.

## Scope and starting point

Continue from the reviewed tip plus this documentation commit, retaining useful
first-pass fixes. Read `CLAUDE.md`, `OOBE_COMPONENTS_WORKORDER.md`,
`OOBE_COMPONENTS_FIX_WORKORDER.md`, and the acceptance report. Their requirements
remain active; this order adds the second review's concrete failures and gates.
Record actual base/tip and build/Wine/package versions. Preserve unrelated work,
including token-security documentation. Do not reset a dirty checkout.

Commit coherent fixes and tests, then publish the implementation branch for
review. No merge/push to master, force-push, unrelated VM changes, new chat/agent
dispatch, or fabricated acceptance of real vendor terms. Do not claim full
completion when an acceptance gate remains untested or blocked.

## Independently checked evidence

- All 38 current Python tests pass in Debian WSL with
  `python3 -B -W error::ResourceWarning scripts/test_runtime_repairs.py`.
  `bash -n installer/scripts/wex-oobe.sh` passes.
- A temporary native probe compiled the actual `oobe_connect`, write/read helpers
  and `oobe_install` functions from `src/dlls/wexhost/unixlib.c`, substituting only
  a local mock server and minimal type declarations. With a response delayed
  17 seconds, the bridge returned error 1603 after 15 seconds with empty error
  text. The mock backend completed later and could no longer send its result.
- With a fresh fixture directory, enabled XACT terms acquisition raised
  `FileNotFoundError` asking for manually provisioned text, without a network call.
- With external execution/protected-builtin operations mocked, the real backend
  accepted disabled `vcrun2022`, ignored deliberately incorrect `recipe_files`,
  and invoked installation twice for the same accepted request.
- The configured shader-compiler terms URL was checked on 2026-10-18:
  <https://raw.githubusercontent.com/mozilla/fxc2/master/README.md> is a two-line
  project description, not full vendor terms. Its contents fail the existing
  500-character check. No proprietary installer/payload was downloaded or run.
- The worker lifetime defect below is established by source inspection, not an
  observed live OOBE crash. Fresh ISO, visual acceptance and independent WoW64
  build/runtime verification remain pending. No VM state was changed by review.

## R1. Own worker memory until the worker actually finishes

In `OobeTermsProc`'s `WM_DESTROY`, a 500 ms wait is followed by closing the thread
handle and freeing `TermsFetchContext` and its text regardless of wait result.
`TermsFetchThread` can still write to that context during a slow request. Checking
`IsWindow` before posting does not protect freed memory or recycled HWND values.

Implement explicit lifetime/cancellation ownership. The worker must retain its
context until its final access; the UI must not free it after a timed-out wait.
Use an owned result/message protocol, references or another demonstrated safe
design. Reject stale completions for destroyed/replaced dialogs, release buffers
exactly once, and handle thread-creation failure without leaving a stuck dialog.
Do not fix this by blocking the UI indefinitely or forcibly terminating threads.

Apply the same lifecycle discipline to installation: closing the wizard must not
leave a worker accessing destroyed controls/global state or launch another install.
Marshal UI operations/results onto the UI thread; do not rely on worker-thread
MessageBox/SetDlgItemText/SendMessage calls to solve cancellation and teardown.

Required tests use the actual native controller/worker code with delayed fixture
responses: cancel within 500 ms, close during a stalled request, reopen immediately,
late completion, worker-creation failure, and close during installation. Use a
memory checker/instrumented allocator where practical, or a deterministic lifetime
test that detects any context access after release. Record which was used.

## R2. Separate transport deadlines from installer lifetime

The fixed 15-second receive timeout in `oobe_connect` applies to INSTALL, but the
coordinator sends no response until synchronous `install()` returns. Normal
downloads/installers can exceed that interval. The UI then reports failure while
privileged work is still running; retries can repeat completed work.

Implement an explicit operation lifecycle: preferably submit/acknowledge a stable
operation ID and retrieve real status/completion, or another bounded protocol
that keeps long-running work observable. An I/O timeout/disconnect is an unknown
outcome, not proof that installation failed. Reconnect/query the original
operation before allowing a retry. Do not merely increase the timeout and leave
duplicate/cancellation semantics unresolved. No fake progress percentages.

Serialize maintenance and deduplicate active/completed requests by approved
package identity. Support safe resume using durable verified results. Propagate
real launch/installer failure and final state. Keep stage revocation effective
while work is running and define how the coordinator owns/reaps subprocesses.
If an MSI cannot stop safely, explain and defer closing to a documented safe
point; never kill unrelated Wine/display processes. An aborted setup must not
hang indefinitely in `wait` while the coordinator ignores a stop flag inside an
unbounded subprocess call.

Tests: a successful mock install lasting over 15 seconds; disconnect followed by
status query; duplicate request while running and after completion; failure after
acknowledgement; cancel/runner signal; restart/resume. Assert one installer launch
and the correct final UI/receipt state. No real vendor consent is needed for these
synthetic tests. Retain bounded framing and complete-response checks.

## R3. Obtain applicable terms or honestly disable the component

Removing embedded substitute agreements was correct, but the default-image
terms workflow remains incomplete:

- XACT is enabled with no acquisition URL and still requires a manually provisioned
  file. Implement authentic full-term acquisition without downloading the
  redistributable before consent, or disable it with that precise reason.
- d3dcompiler_47 is enabled with a project README as its terms URL. Replace it with
  full applicable vendor terms paired to the actual payload, or disable it.
- The corefonts/Tahoma entries share a third-party core-fonts EULA URL. Establish
  applicability and provenance for each actual package; do not infer that an IE
  language pack is covered merely because it contains a Microsoft font.

The disabled route is acceptable as a fail-closed repair, not evidence that all
six component installations are complete. Show reasons in both OOBE and the
post-install launcher. Do not enable an entry just because an arbitrary root-owned
text exceeds 500 characters. Package/terms applicability needs trusted metadata.
Do not commit proprietary documents; preserve official-source provenance and
extracted-document completeness evidence. Do not provide legal permission claims.

Test real source format/extraction where available without redistributable
downloads, plus synthetic unavailable/malformed/partial/incorrect-document cases.
Report enabled, disabled and unresolved components separately.

## R4. Enforce package identity and availability at the actual backend

`recipe_files` remains descriptive metadata: `install()` invokes ambient winetricks
without checking those identities. Removing `-f` is useful, but does not bind
reviewed terms to this project's approved recipe or all of its dependencies.
Furthermore, `install()` does not reject disabled components; the ordinary
frontend lists them and can reach the backend when cached terms already exist.

Centralize availability and identity validation at the privileged boundary, for
both OOBE and post-install paths. Reject disabled/unsupported versions before any
prefix mutation or payload request, even with old cached terms. Validate the
installed winetricks recipe/version and complete download/dependency set, both
architectures as applicable, against approved identities. Reject unknown changes;
do not silently accept whatever a new recipe downloads. A secure pinned execution
recipe is also an option if distribution/license review and maintenance are clear.

Persist package/recipe/version/architecture identity with the terms snapshot and
outcome. Any changed identity requires preparation/review/acceptance again. Receipt
file existence or a terms-only hash is not a verified current installation.
Reuse this policy for the operation-ID/idempotency work in R2.

Tests must call the actual `install()`/policy path while mocking only external
execution: disabled verb with valid cached text, altered/missing recipe, changed
payload/dependency hash, architecture mismatch, outdated receipt and duplicate
request. Assert no mutation/download/installer call on rejected requests. Test
post-install behavior as an ordinary administrator and limited-user denial.

## R5. Exercise production native code and correct evidence claims

`CatalogParserTests` currently checks Python dictionary keys/row construction,
not `ParseCatalogBuffer` or its controller. Add a native test seam that compiles
and executes the production parser/controller, rather than copying its logic
into a separate passing implementation. Cover six rows, empty fields, malformed
and oversized rows, UTF-8, checkbox selection and disabled entries. Check Win32
conversion failures; never consume an uninitialized conversion buffer or silently
present corrupted terms as the full readable agreement.

Retain tests for candidate/accepted digest separation, changed document between
size-query/data-read calls and cancellation. The new worker must not obscure those
semantics. A dialog cancel must not accept new terms using old approval.

Correct `OOBE_COMPONENTS_ACCEPTANCE.md`'s blanket F1-F7 completion claims. Keep
actual passing unit and author-reported build results, identifying their scope
and log locations. Update `FONTS_RUNTIMES.md`/handoff claims to match current
limitations. Report native controller tests separately from Python metadata tests.

## Completion and review handoff

Deliver coherent implementation/test commits and an updated acceptance matrix
with each R1-R5 and original F1-F7 marked fixed, open or blocked, plus evidence.
An unavailable resource/consent gate is pending, never implicitly passed.

Required before merge-readiness:

- Combined WoW64 build, actual 32/64 PE bridge/controller tests and a 32-bit
  regression. Preserve the Wine pin and unrelated graphics/theme architecture.
- Native UI cancellation, retry/resume, separate terms decisions and truthful
  summaries; Classic/Open Luna layout/keyboard checks at 800x600 and normal setup
  resolution. Preserve taskbar/toolbar/tray appearance.
- A fresh installed ISO on an explicitly designated disposable VM: offline skip,
  later launch from an empty cache, authority cleanup, and runtime checks in
  administrator/limited split prefixes on X11 and Wayland. Real proprietary
  installation only after actual human terms review/acceptance; otherwise report
  that test blocked. Do not reset/reinstall an existing VM without authorization.
- No privileged worker/endpoint left after safe completion or abort; trusted
  metadata and accurate recoverable outcomes persist. Record test commands,
  exact versions/tip SHA, ISO size delta, logs and remaining VM/process state.

Push the repaired branch for a third review; do not merge to master. If only a
subset can be completed, list it honestly and leave unresolved gates visible.
