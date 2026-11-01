<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Rejected observation accepted; prepare isolated retest without erasing R2

Parent accepts9f86add0 after final source/test review, independent158 tests
21.773s/no skips, and15 current/snapshot/log/history hashes. Reader binds task
FDs and retains rejected stat/cmdline plus prefix status (not raw environ).
Collector attempts persistence before exact helper reap. Exact accepted forms
and primary rejection stay intact. Tests' synthetic historical approval is
explicit; an added negative proves actual stale preparation rejects new runner.
No guest or explanation of the lost R2 bytes is inferred from these controls.

## Isolation decision

Preserve original366c/6087 native PEs and their fixed guest R1 stage/output.
Preserve failed R2's current `system-readiness-r1-20261024.qcow2` and all evidence.
Prepare a NEW D: writable layer
`system-native-identity-r3-20261024.qcow2`, backing the unchanged frozen
`system-capacity-r1-20261023.qcow2` (80GiB virtual). Existing frozen original
three layers remain; the failed readiness layer stays as a separate preserved
branch, not a backing layer of the fresh test. No in-place rollback/rebase/delete,
new PE build or broad fixed-contract path change is needed.

The historical native paths were never staged in that frozen parent by our
failed preflights, but this is NOT live absence proof. Mandatory future canonical
lexists checks of BOTH compiled guest stage/output remain before writes.

## Luna: OFFLINE integration and immutable preparation ONLY

Fresh HOST run/preparation `native-exit-calibration-r3-20261024`; original GUEST
R1 paths remain exactly as compiled. Narrowly adapt private launcher/procedure
to this explicit R3 case and new target. Launcher must reject missing/symlink/
noncanonical new target BEFORE output/socket/child creation; never fall back to
failed layer. Preserve before/after/patch/hash copies on D:. Do not start it.
Older R2 helpers/manifests/history remain untouched snapshots and unsuitable for
new execution. Actual source may select only the new reviewed fixed target.

Private source validation must accept exactly the newly parent-reviewed Python
runner/identity module hashes9f86add0 for R3, not pretend they equal the old R1
receipt. Original C helper/contract/five build sources/worker PE receipts remain
unchanged. Keep legacy old-approval rejection tests; new R3 approval is explicit.
No guessed Wine argv style accepted or production/runtime source change.

Preparation records:

- New target canonical/ABSENT; current failed layer identity/full SHA
  da698bec9326daf991f4e30b3b7edc702b7f4dc9db76d7c923c9ba3da2f8f89d and
  normal-lock old four-chain retained. Frozen parent full SHA
  c2acebb986bab6e3c6cd6bdd0c7efbe4c4d3b4f7f69ed358a7d53598ba1f9936,
  original three identities/historical lower hashes, no observed owners/open
  files, >=80GiB host reserve and pinned qemu-img. Bound hashes<=120s; no65GB
  rehash while unchanged lower identities match the accepted receipts.
- ONE FUTURE create argv `qemu-img create -f qcow2 -F qcow2 -b <exact frozen
  capacity top> <exact new target>`, NO size/-u/-U. Record only; NO execution.
  Future normal-lock four-chain should be new80 + frozen80 + old64 + Debian3GiB,
  exact canonical backings/formats/no dirty/corrupt flags. Failed old layer must
  remain identity/content unchanged before/after any later creation/boot.
- Separate future creation and boot commands/gates. No implied permission to
  create an image from preparing it; parent reviews manifest/source first, then
  ONE creation, reviews result, and only later ONE guest calibration diagnostic.
- Fresh source TAR/current16 inputs/original2PEs+pins/build receipt/source/logs,
  current8helpers/current host tests/seeds/ISO/19EXPECTED binaries, source commit
  provenance versus actual final per-file hashes. No vendor/runtime payload,
  ZIP extraction, installer/download or Windows execution in preparation.
- Durable native key0700/0600/source+seed-public match, exact prefix/nonce and
  unchanged accepted self-contained ownership PROBE+strict host parser/64KiB
  capture. Authenticated readiness beforedf/674/cap180/650/free1GiB/19actual/435,
  validated COMPLETE+empty before any stage, fixed GUEST target absence,16
  deployed hashes and unchanged normal native calibration command/held-handle
  assertions. Reject busy/UNKNOWN/malformed/nonzero/truncation with no writes.
- 900s/90s/80s launcher/shutdown/stop,285s runner/300s SSH/45s archive/55s runner
  cleanup/42-30-5-5 helper, privateLO/unique sockets/actual innerouterVirGL,
  exact identities, raw rejected observation BEFORE cleanup, strict attributed
  QMP event+ACKs/QEMU0/no fallback/children absent/Windows0. No deadline borrowing.

Meaningful tests execute actual R3 preparation/decision branches with fakes:
new fixed drive/no fallback/missing/symlink, target-existing refusal, preserved
failed-layer or frozen-parent identity/hash mismatch, actual old-runner approval
rejection versus explicit new approval, hostR3/compiledguestR1 paths, complete-
empty before writes, all current failure/capture/reap/parser negatives. Keep158
controls and original assertions; no actual QEMU/SSH/create/network/PE dispatch.

Retain exact immutable preparation/TAR/source patches and audit on D:, commit
AND push report/tests, STOP parent review. No image/VM/guest access, fsck/resize/
repair/rebase/commit/compaction/deletion, native/API/browser execution, token
changes or production Wine/compositor build.95%/99% stops/no credits.
