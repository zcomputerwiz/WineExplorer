<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Antigravity: unblock the runtime check and get Chromium launching

LATEST HUMAN direction: get Chromium working; a perfect test harness is not the
objective. This replaces the inventory-only/repeated approval approach. Parent
review confirms R6 stopped before browser/native probe on a file-set mismatch.
Actual missing/extra filenames remain unknown; do not call this corruption or a
Chromium rendering failure. QMP ACKs/guest shutdown/QEMU0 are independently
confirmed. Nested WSL exit code is NULL; report that honestly without making an
unrelated controller-hardening project a browser prerequisite.

## Main task

Use own D: codex/chromium-runtime-unblock from this parent commit. Reuse existing
accepted Wine modules/build/runtime/VM and small existing adapter. Preserve old
R6 fixed stage/output/evidence; no other workers' changes. No new disk/download/
reinstall/full Wine build/token implementation/harness redesign.

1. Identify exact expected missing and actual extra runtime filenames. Prefer
   retained evidence, otherwise ONE owned VM session may inventory the runtime
   and continue the browser control below. Keep inventory small/read-only and
   private D:. No content dump/credentials/arbitrary filesystem inspection.
2. Make the smallest justified runtime guard fix. If all required package files
   are present and the difference is ordinary generated files, record extras as
   diagnostics rather than demanding pristine installation contents. Historical
   hash/version differences are warnings under the user's standing policy.
   Required payload absence, wrong path/owner, failed read or unsafe links are
   concrete problems; report precisely rather than deleting/repairing blindly.
   Do not blindly rewrite the manifest to fit or allow actual missing binaries.
3. Run focused local tests proving canonical runtime reuse, benign surplus
   handling and required-file failure. Existing accepted broad unit matrices
   need no repeat. If relevant files are genuinely missing, STOP and report the
   minimal proposed repair; no reinstall/extract substitute in this order.
4. If a small justified fix clears the guard, prepare fresh distinct output and
   stage names for ONE follow-on normal Chromium control, using the same modules,
   original HTTP startup fixture/eight normal args/private loopback/real VirGL.
   Reuse the existing adapter structure. Output-only native helper recompilation
   and minimal fixed-path/mode adjustment are allowed if needed to match fresh
   paths. Do not overwrite R6 evidence, Luna build/checkout or already staged
   sources. Check matched actual helper/output/profile paths with focused tests.
   No need to stop for a second offline paperwork review of these narrow changes.
5. Run that ONE browser diagnostic in the owned session after readiness/space/
   prefix-idle checks. Keep normal sandbox/features, existing time bounds and
   exact owned stop in finally. No debugging attachment, sandbox bypass, token
   patch, speculative API fix, feature disabling, second browser retry or another
   boot loop. Record actual metadata probe, native status, HTTP/readiness/front
   image and useful failure. Once the next actual Chromium failure appears,
   retain it and STOP for parent review; do not expand into another subsystem.

Use only existing D:-backed R4 disk/owned guest. All private runtime/screenshots/
logs on D: or D-backed WSL. Historical IDs/hash differences warn, actual wrong
owner/disk/overwrite and failed functional guards stop. Keep unique sockets and
private consumer namespaces/backend checks. Preserve existing900s launcher/
90s cleanup reserve/80s stopper and consumer bounds, no timeout inflation.
Before boot check usage:>=95%five-hour defer, weekly>=99%stop; never use credits.
Clean up safely after any launched run, retain observed shutdown evidence.

## Deliverables

Commit AND push narrow code/tests and docs/CHROMIUM_RUNTIME_UNBLOCK_20261026.md
with exact file difference, justified fix, actual test/run result and next useful
Chromium failure if any. Clearly distinguish static/mocked checks from actual
consumer evidence. Reply codex with commit and private evidence location, stop.
No master merge or unrelated OOBE/token/other VM edits.

## Shader side task

Parent46 Python tests and Node35negative/28mutation pass onca446842. Original
reference-alias and blank tiny-region false passes are corrected. Accept this
for the supported valid/default geometry; malformed explicit bounds fallback,
exhaustive parser cases and other unsupported caller inputs are TODOs. Record
that limitation; do not spend another implementation round on them now. No real
shader trial until normal Chromium startup works. Keep fixture history separate.
