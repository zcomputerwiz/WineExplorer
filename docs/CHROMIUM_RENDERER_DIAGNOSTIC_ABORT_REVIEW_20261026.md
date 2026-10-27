<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review: preserve metadata success; remove diagnostic-induced abort

Reviewed92bab165/e2e31b83 and actual private consumer results/native journal/
observer JSON/API record/front image. Do not import observer as a proven
child-exit mechanism. It classifies all chrome/wine processes globally without
owned-prefix/start-time binding, conflates unreadable/changed cmdline with
vanishing, and captures NO terminal exit status. It cannot support clean-exit,
never-crashed, exact ownership or proven-token-cause claims. No observer redesign
or conformance round is requested; its records remain partial diagnostic evidence.

Actual results.json PRIMARY ERROR is
LinuxIdentityError('Linux identity observation deadline'), helper_phase=linux_identity,
elapsed18.702718721s. The native journal has a bound record ONLY, no terminal
status. Observation window is approximately12.6s, not the previous45s native
wait. Cleanup follows this observer failure; renderer disappearance at its end
does not establish a natural/clean renderer exit. Viewed terminal-front is blank,
HTTP requests empty. No sad-tab line in the shortened run does not demonstrate
improvement in renderer survival. Cause of original killed page remains unknown.

Positive actual guest result: R6 metadata probe21checks/0failures, Linux exit0,
normal module paths, duplicate preservation/unregister/final absence, owned probe
cleanup complete. Accept that scoped API result. Do not call it token enforcement
or renderer sandbox proof. Retain all failed-run data, don't retry consumed paths.

## Antigravity next assignment: let the actual browser run

Same own D: codex/chromium-renderer-unblock, or new own D: branch from this
parent commit; preserve previous commits. Goal is actual Chromium functionality,
not a better generic observer. Stop working on ChildObserver. Disable/remove it
from the next browser path so global /proc scanning every20ms is not required.

Implement the smallest targeted diagnostic policy correction: after initial
owned Popen identity and native held-handle binding are established, a periodic
Linux identity OBSERVATION timeout/read-unavailability is recorded as partial/
unknown diagnostic evidence rather than immediately aborting the Chromium run.
Keep concrete PID/start-time reuse/wrong-owner detections and exact ownership
checks before any signaling. Never label an unread observation as successful.
Keep outer/native operation deadlines, initial binding checks and cleanup reserve;
do not inflate timeouts. Prefer a Chromium-only contract opt-in to preserve the
original native calibration contract where needed. No full observer redesign.

Run focused regressions: transient observational timeout is recorded while the
native journal can complete; concrete PID reuse still prevents signaling/adoption;
the real whole-operation deadline still ends/cleans the owned run. No repeat
accepted API matrices or broad hardening. Explicitly report remaining unknown
identity data. This implements the user's earlier diagnostic logging preference.

Then ONE fresh normal Chromium control with corrected mode/probe, original HTTP
fixture/eight args, same metadata DLLs/runtime/owned D: R4 disk. Match fresh
stage/output and output-only helper paths; no consumed-path reuse/overwrite.
Allow its existing45s native wait inside current82s operation to occur naturally
unless a real functional/ownership/whole-budget failure intervenes. Capture
native terminal journal, HTTP/readiness and actual front; partial result is
honest if it still fails. No new child observer or broad logging framework.

No speculative token/mitigation/handle-table patch during this comparison. The
existing token integration remains authorized when selected by actual consumer
evidence; current abbreviated run does not prove it necessary or sufficient.
No debugger/attach/suspend/security disabling, new image/download/reinstall,
shared build writes, master merge or unrelated workers' changes. Preserve
privateLO/unique sockets/actual backend/space/idleness and exact owned shutdown.
At95%five-hour finish bounded cleanup/checkpoint/push, weekly99%stop, nevercredits.

Commit AND push narrow code/tests and original report
docs/CHROMIUM_CHROMIUM_OBSERVATION_CONTROL_20261026.md, correcting earlier causal/
exit claims and stating actual new page outcome/next blocker. Reply codex then
stop. Do not request another paperwork approval before this bounded control.
