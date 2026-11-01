<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Capacity execution preparation — PREPARED_NOT_RUN

Offline continuation of CAPACITY_STAGE_A/checkpoint; code `68dbd402`. No image
creation/resize, second GPT extraction, VM/SSH/Wine/browser execution, mount,
launcher mutation or vendor acquisition. Original failed prep and all evidence
remain immutable. Tracked work C: graphics checkout only; D: master/other workers
untouched, pre-existing `M wine` unstaged. Chromium-first applies AFTER separately
accepted capacity maintenance; no further Edge diagnostic is selected here.

## Exact reviewed preparation

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/capacity-maintenance-r1-20261023.preflight/`
contains preflight.json, PREPARED_NOT_RUN, source/helper inventory, original and
proposed launcher text/diff, before/after chain logs and offline-audit.json.
SHA256 preflight: `f5fef641fce0a7e0d34cf74c85c58f307cd58478ebb001584699c59f983fc2bf`.
New overlay and maintenance run root remain absent. Host free1,860,751,900,672 bytes.

Audit passes8 actual C: source/helpers,2 seed sources/seed.iso, qemu-img hash,
exact19 accepted binary values (INCLUDING retained Edge EXE/labwc, not21), accepted
provenance437f88ed..., retained GPT hashes and failed StageA c01f9f22... unchanged.
Both actual chain JSON reads exit0/match. Native layout replay uses retained
34,304 bytes only; root PARTUUID/type/start262144/end134217694 and BIOS/EFI extents
remain as accepted StageA. Historical ext4 UUIDfe2c0409-10e4-4cd2-949b-a61b19c5a757
is explicitly required alongside PARTUUID624cfa91-935d-4cc7-9a3f-f29b41cac2a0.

Actual full physical-file SHA256 pass completed356.567251s within600s:

| Original input | Bytes | SHA256 |
| --- | ---: | --- |
|system.qcow2|65,711,112,192|89da0f350f487d569d6eefb54c5393c6e25f4e67e7169bcbcd2d99ab5a0e8900|
|Debian image-source backing QCOW2|438,566,912|0062fb66565560d03bc67df298196d0c960f9db55bdb0d00fef67fbb895b1e62|

Original file identities remain unchanged; owner/open-file snapshots empty.
This is offline file-content provenance, not an atomic cross-file snapshot or
filesystem integrity check. Exact full hashes/identities must be checked AGAIN
before creation and after shutdown.600s hashing is OUTSIDE the900s VM lifetime.

Original launcher SHA256 `944e3eac6ba80e3d6c56b6fb7eae9235e21200936f2349f9296a8e4cf678d221`;
proposal `d96f6854f0360cfb03c24ea6092aa8b3930cc1075af5cfb8aadfdeac65efaf82`.
Only proposed change is system.qcow2 -> system-capacity-r1-20261023.qcow2; actual
launcher still matches original. Exact80GiB qcow2 create/backing/inspection argv,
required real creation argv/exit record, reviewed diff and Windows
`C:\Users\Owner\.codex\worktrees\chromium-graphics-plan\WineExplorer\scripts\g4-virgl-launcher-session.ps1`
boot argv are in manifest; none executed. No overwrite/retry/partial-create cleanup.

## Gates, not a general VM manager

New maintenance helper only prepares or reads/verifies; no create/boot dispatcher.
Future live guards reuse rawUTC650/435 and exact launcher/child PID/start records,
then compare ACTUAL QEMU drive argv to new overlay + read-only seed. Empty startup
cmdline placeholders do not prove identity. Proposed launcher/source/tool/seed
hashes required. Exact independent SIGINT/QMP stopper retained; no Ctrl-C.

Fixed stdin guest probe needs no staging/profile:9 read-only commands, each<=15s
including owned kill/reap tail, whole<=120s, SSH<=150s. Full argv/hash expectations
are inventoried. Require80GiB disk, ext4/vda1 FS UUID/PARTUUID, exact root end
167772126, unchanged starts/other extents/types/GUIDs/attributes, clean grown
filesystem, >=8GiB free, cloud-init done/no errors, services running and all19
hashes. No repair/growpart/manual-resize fallback. First writable boot automatically
growing root IS mutation, not metadata collection.

Capacity success needs BOTH validate-guest AND verify-finished exit0. The latter
explicitly requires QMP signal/graceful cleanup, QEMU0 AND Windows controller0,
matching final identity, recorded launcher/QEMU/Xvfb identities absent, plus full
original-chain post hashes. Stopper0 alone is insufficient. All failure logs and
partial overlay preserved; no deletion, rebase/downward commit or second boot.

## Actual focused tests / handoff

Executed `python3 -B -m unittest test_g4_capacity test_g4_capacity_maintenance -q`
from the explicit C: worktree's WSL scripts path:28 tests PASS/no skips in14.027s.
Included one ACTUAL Linux original Python child flushing PID then sleeping30s:
14s timeout, exact owned group SIGKILL, reaped-9, retained PID/output and no remaining
PID; no VM/Wine/native PE. Primary timeout and separate reap-failure controls pass.
After two final pure host-gate additions, focused GPT+GuestGateTests+HostGateTests:
26 tests PASS/no skips in0.011s (overlaps prior run; not claimed as another full
30-test suite). All current30 methods covered across these runs. No unrelated
controls repeated. Parent independently passed28 tests and audited actual manifest,
all8 inventory entries/19 pins/original identities/proposals/chain/no-overlay.

Negatives include wrong19 hashes, UUIDs, partition extents/state/free capacity,
unfinished cloud-init/degraded services, malformed/nonfinite elapsed, stale drive,
nonzero/boolean QEMU/Windows exits, wrong/partial creation records, changed inputs,
hash timeout and original GPT safety cases. Fakes are not mutation/growth proof.

Return for parent review/explicit ONE maintenance authorization. No live step,
capacity recovery, Chromium download/adapter or graphics acceptance is claimed.

## Independent parent review / ONE maintenance authorization

Parent read all new source/tests and final two added host gates. Independently
passed28 tests (including the actual14s Linux timeout/kill/reap child) and both
later added methods: all30 current methods covered, no skips. Independently
audited actual immutable manifest f5fef641...:8 current source/helper hashes,
2 seeds/seed/tool hashes,19 accepted pins and provenance, unchanged original
file identities, matching before/after chain, exact proposed launcher content,
correct Windows boot argv, exact80GiB new-layer create argv, absent overlay/run.
Actual original full hashes were recorded within356.567s; mandatory precreate
and postshutdown full revalidation are NOT waived by this review.

ONE exact maintenance execution is authorized using this preparation and code
68dbd402. Recheck live usage, original full hashes/inventory/owners/space and
all absent targets BEFORE create; record real creation exit and inspect chain.
Apply ONLY the reviewed private launcher change after successful creation/gate,
verify its proposed SHA before first boot. That boot may automatically enlarge
root and IS mutation of the NEW top layer only. No browser, Wine, runtime
download, manual resize/repair, second boot, overwrite/retry or evidence deletion.
Always execute exact-identity shutdown and original-chain posthash proof, even
if the guest probe fails. Preserve all failure/partial output and stop for parent
review; both validation gates must pass before capacity acceptance. This does
not authorize Chromium implementation/download/live testing until parent review
of actual maintenance evidence. No master/submodule/other-worker changes.
