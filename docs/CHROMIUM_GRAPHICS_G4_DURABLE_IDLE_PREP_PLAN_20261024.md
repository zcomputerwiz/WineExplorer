<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Durable credential accepted; prepare r2 offline

Parent accepts `fc97746f`: reviewed final source/tests and host-only observer,
independently ran all 72 focused tests (4.729s, OK, no skips) and verified all
three sealed credential evidence files. Existing authorized key matches the seed
and has restrictive native ownership/modes in two observed invocations. No SSH
or guest readiness result follows. The precise old temporary-key loss remains
unknown; observed `/tmp` tmpfs is not causal proof. Guide/README are published.

## Luna: ONE fresh preparation, no guest execution

Prepare `native-idle-ready-r2-20261024.preflight/` for a later reviewed diagnostic.
Keep control mode `native-idle-ready` and the exact original idle probe. Narrowly
adapt only the owned private procedure's preparation code to the explicit r2
name, durable key path and the EXISTING accepted safety layer. Preserve before/
after source and patch/hashes on D:. Do not change the launcher, guest builds,
seeds, runtime, existing helpers or original failed preparations/evidence.

The intended disk remains `system-readiness-r1-20261024.qcow2`, legitimately
written by the completed r1 boot. Its accepted post-run SHA is
`393308d2dcf343df1dac1d6a2f47a4f4f27d70e26a88596d0d3f97eacf14999c`;
identity/27,590,656 bytes are in sealed r1 `.host-audit-r2/after.json`. Do not
require an absent disk or mark it never-booted. No new image is needed. Inspect
normal-lock exact four-layer chain, canonical backing paths/sizes/flags; require
current safety-top identity/hash and frozen original three identities, repeated
former-top full SHA c2acebb9... within120s and historical lower identities.
Require idle/open-file snapshots and >=80GiB host reserve. Record actual state;
NO create/resize/repair/rebase/commit/mount/delete or creation command/authority.

Set BOTH SSH identity argv and recorded credential path to
`/var/tmp/wex-g4-lab-credentials/vm_key`; validate secure canonical native path,
UID/modes/readability/source-content and seed-public match before preparation.
Store metadata/public fingerprint only, never private bytes or content hashes.
Pin current readiness helper `5232f407...`, all seven helpers, sources/tests,
seed/ISO/expected 19 binaries, exact original probe, current overlay and reviewed
credential observer source if used by future host gates. Expected binaries are
not live observed hashes.

Retain 900s/90s lifetime/cleanup, 80s stopper, authenticated two-reply same-boot
readiness with raw674/cap180/650/435 guards, guest minimum1GiB, unconditional
read-only no-stage/no-archive path, and strict QMP/owned-QEMU/controller shutdown
requirements. Exact future launch/run/stop commands use the fresh r2 run name
and current C: worktree; no actual QEMU, SSH, guest or consumer invocation.

Add narrow preparation negatives for existing reviewed top missing/changed/wrong
backing and for stale credential path; prove preparation can only inspect/hash,
not create a disk or connect. Keep prior 72 controls passing. Fresh inventory
must pin final actual source/test hashes; source_commit may precede the report
commit and must not substitute for those hashes. Retain patches/evidence on D:.

Commit AND push preparation/tests/report and STOP for parent source/test/manifest
review. No boot authority in this handoff and no silent retry of old r1 runs.
95%/99% usage stops apply; leave a bounded checkpoint if needed.
