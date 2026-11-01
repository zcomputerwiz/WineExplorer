<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Conditional next integration/preparation: readiness + shutdown + safety overlay

NOT live/create authority. Begin only AFTER parent accepts both tracked helpers.
Read READINESS_PLAN, READINESS_IMPLEMENTATION, BOOT_SAFETY_REVIEW_20261024 and
latest parent review. Same C: source branch/D: private storage, preserve M wine,
main D:master, original chain/evidence and other workers. Luna implements, parent
reviews. No production Wine/compositor rebuild, proprietary transfer or browser.

## One OFFLINE integration/preparation handoff

Edit only owned ignored launch.py/presentation-timing-trial.py, narrowly related
tracked tests and report; preserve before/after/diff/hash on D:. No guest boot,
SSH or qemu-img create while preparing. Both helpers remain reviewed/pinned.

1. Launcher proposal uses ONLY fresh D:system-readiness-r1-20261024.qcow2 as
   writable disk. Parent-backed system-capacity-r1-20261023.qcow2 becomes frozen;
   full SHA c2acebb986bab6e3c6cd6bdd0c7efbe4c4d3b4f7f69ed358a7d53598ba1f9936.
   Inherit80GiB, existing seed readonly. No arbitrary overlay/environment bypass.
   New top MUST be absent until separate creation authorization; default missing
   new image must not fall back to writing former top. Preserve old launcher.
2. Integrate reviewed QMP observer in launcher shutdown. Retain transcript/ACKs/
   guest SHUTDOWN/errors. Mark graceful ONLY matched protocol, guest=true with
   reason=guest-shutdown, actual owned QEMU exit0, no terminate/kill fallback.
   ACK/POWERDOWN/host-signal/guest-reset/exit0 alone cannot pass. Keep fallback
   bounded and explicitly non-graceful. No QMP quit/reset or assertion weakening.
3. Keep existing900s lifetime/90s shutdown reserve. Protocol observation<=50s,
   then bounded QEMU reaping/terminate10s/kill5s/display5s inside total shutdown
   budget. Fresh manifest stopper may use80s (not old60s), still within90s.
   Preserve exact launcher/child identity guards and Windows session exit proof.
4. New READ-ONLY run name native-idle-ready-r1-20261024; do not reuse either failed
   run/preparation. Insert authenticated readiness BEFORE guest df/probe. Gate
   cap=min(180s, raw remaining-650s), refuse insufficient two-reply headroom.
   Retain readiness evidence even on failure/exception, no subsequent SSH when
   gate fails. Then enforce existing650 guard, free>=1GiB,19 actual binary pins,
   435 guard and EXACT unchanged original idle probe SHA d36b5d1d24f0964c98491263a034e6477f7f6af26a5079cc1244e2b39327aac0.
   No TCP-only readiness; no lifetime/cleanup-budget borrowing. Cloud-init/systemd
   failures are preserved, not automatically repaired or accepted degraded.
5. Preserve current readonly branch's unconditional no-stage/no-consumer/no-
   archive behavior on empty/busy/error. Capture raw bytes/returncode before
   interpretation. Fix cosmetic byte_origin label for nonexception success in
   this fresh helper pin, not old artifacts. No Wine/native helper/browser run.
6. Add executable integration negatives for ready-failure/no df, consumed budget,
   wrong/new-missing overlay/no fallback, QMP ACK-only/missing guest event/error/
   forced terminate not graceful, helper/source/pin mismatch and exact shutdown
   bounds. Existing readiness/QMP/stopper/budget/capture/readonly tests must pass.

## Fresh immutable preparation, parent review BEFORE creation

- Exact source/helper/test hashes, readonly argv, unchanged probe bytes/hash,
 19 capacity-accepted pins, credential/seed/ISO identity, launcher proposal/actual
 hash,900/90 limits and native socket containment. Separate creation and boot
 commands; do not start anything to fill missing fields.
- Canonical absent new-overlay target, full three-layer normal-lock metadata,
 source top full hash/identity, unchanged lower-layer identities, no observed
 owners/open files, D: reserve>=80GiB. qemu-img binary/hash pinned. Source freeze
 checks fail closed; no full65GB rehash needed while recorded identities match.
- ONE future create argv: qemu-img create -f qcow2 -F qcow2 -b <exact current
 80GiB absolute path> <exact new D: path>, NO size/-u/-U. Full source top hash
 before/after creation; four-layer info exact, new80GiB qcow2/expected backing/
 no corrupt flag, all old source identities/content unchanged. No overwrite,
 resize, repair, commit, rebase, compact, mount or deletion on error.
- After accepted creation parent separately reviews before ONE later readonly
 boot. Boot may replay guest journal into NEW layer only; not a filesystem-clean
 claim. Preserve all old layers. After boot, rehash frozen source top and check
 lower identities. Actual guest status,19 hashes, boot UUID and graceful shutdown
 still required; no automatic calibration/browser/retry follows observations.

Commit AND push preparation/tests/report; STOP parent source/test/manifest review.
Parent later separately authorizes creation and boot, not implicit in this plan.
95%five-hour/99%weekly stopping rules, no reset credits, heartbeat remains enabled.
