<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium native-exit diagnostic after accepted calibration

This is the next implementation plan AFTER diagnostic inventory policy review.
It does not authorize a boot or browser execution. Goal: identify actual native
Windows process status/failure, rather than guess from historical Linux127.
Native R4 calibration is independently accepted; do not repeat it.

Implement a small explicit diagnostic runner using existing ordinary native parent
PE366c6928... and its reviewed fixed contract. Reuse compositor/HTTP/bounded-cleanup
helpers, not the entire readiness/graphics acceptance machinery. Preserve normal
Chromium flags and original fixture; parentage differs and must be labelled.

- Browser runtime stays at compiled fixed GUEST image path from
  g4_native_exit_contract.h; native output/profile/events are its OTHER compiled
  fixed Chromium paths, not the already-used calibration paths. New host run
  `chromium-native-exit-r1-20261024`, separate fresh source staging. No PE rebuild
  or generalization of native flags. Record actual helper/runtime/Wine versions
  and hashes; historical differences are warnings under the human's new policy.
- Reuse current owned D:-backed R4 writable disk; no new layer/chain required.
  Preserve all existing failure outputs. Runtime: reuse existing fixed-path
  extracted payload read-only if complete; otherwise stage from lawful private
  cached official ZIP into an absent fixed runtime stage. Never repair/overwrite
  an existing partial directory. No new download or installer. Record actual
  availability and required space before transfer/extraction/profile creation.
- Start exact original two-route nonce HTTP fixture inside existing privateLO
  namespace, compositor and actual inner/outerVirGL checks. Collect requests/logs.
  Launch original native parent via Wine; original image and entire argument
  contract/nonce/profile/events remain exact. Observe ONLY its returned Popen,
  PID/start/prefix and approved frontend/internal-loader forms. No HWND dependency,
  debugger/attach/suspend/token changes/security bypass or arbitrary PID lookup.
- Parse bounded flushed journal in Chromium mode. Native child status decimal32/
  hex comes ONLY from held-handle signaled exit, distinct from helper Linux status.
  Check native binding nonce/image/command/creation time/child PID. Accept valid
  arbitrary unsigned32 exit including259 AFTER signaled wait; never force an
  expected Chromium status. Preserve raw bytes on parse error. Binding failure,
  immediate-exit race, wait/query error, native45s timeout and partial/missing
  journal are diagnostic outcomes, not invented exit codes. Original helper's
  poll-before-identity rule can miss early exits: record exact phase; no live fix.
- Bound native45s plus helper startup/reap in existing285s consumer/55s cleanup
  reserve,300s SSH and900/90/80 outer containment. Reap native parent before prefix
  cleanup, preserve natural vs observer-killed distinctions and primary error.
  Any original child still alive after timeout is cleaned up only through owned
  idle-checked private prefix. Sample frame if available, but it is not required
  to obtain a native exit. No pixel/readiness success claims from this diagnostic.
- Diagnostic always returns nonacceptance1 even if observation completed and
  helper0; report `observation_complete` separately. Archive on failure, original
  stderr/nativejournal/HTTPrequests/actual versions/backend and full cleanup.
  Do not let private orchestrator confuse diagnostic1 with failed orchestration.
- Tests exercise actual runner callbacks/parser: original Chromium binding/schema,
  full0xc000007f vs127, signaled259, wrong nonce/image/command/time/child, failure/
  timeout/early-race/partial data, no journal/no raw env, parser failure preserving
  evidence, quota/budget, helper reap before prefix, exact argv/flags, reuse vs
  absent runtime and refusal to overwrite. Include historical metadata/hash drift
  warning behavior; wrong disk/PID reuse remain fatal. Keep actual native shared-C
  tests and original HTTP fixture tests; don't repeat unrelated old suites.

Deliver code/tests plus fresh PREPARED_NOT_RUN with actual current sources/helper/
runtime hashes (inventory, not historical blockers), D: disk/runtime/output paths,
exact launch/run/stop commands and limits. Parent reviews once, then authorizes
ONE diagnostic. No extra image-creation/native-calibration round. Historical
manifests/evidence remain untouched. Commit AND push; STOP for review.

After actual result choose smallest proven blocker: loader/import/status mapping,
native launch/query race or browser-specific failure. Do not jump to graphics/
security diagnosis from numeric status alone. Full Chromium/Edge/G4 remains open.
Only C:graphics source/D:private storage; no main/master/other-agent changes,
production rebuild or proprietary publication.95%/99% usage policy remains.
