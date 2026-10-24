<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Rejected identity observation: offline source/tests complete

Implemented only REJECTED_IDENTITY_PLAN_20261024. No VM/SSH/Wine/Windows PE,
browser/debugger execution, PE build, private procedure edit or immutable
preparation occurred. Failed R2 guest stage/output and layer remain preserved;
this work grants no retest or overwrite authority.

`g4_edge_owned_threads.linux_identity()` now raises a typed ValueError subclass
with a bounded rejected observation. Positive return shape and the existing exact
argv-array/flat-padded matching branches, normalization, prefix and child checks
remain. No guessed command-title form is accepted. Reads use an opened root/task
directory, check start before environ/cmdline, and recheck start/directory identity
afterward. An initial start mismatch reads no environ/cmdline. Raw stat/cmdline
are retained as base64; environ is never retained, only prefix outcome/error and
read accounting. Partial/error bytes, errno/type, stages, limits, completion/
truncation possibility and monotonic timing remain evidence, never acceptance.

Actual read caps are4096/stat,16384/cmdline,65536/environ. No cap+1 read occurs:
reaching the cap without proving EOF conservatively rejects even exact-cap data.
Identity observation has at most1s and the caller's earlier deadline. FDs receive
close attempts on every path; secondary postcheck/close failures retain the
original rejection. These bounds cannot preempt a stalled kernel/filesystem
syscall and are not hard real-time guarantees.

The native collector passes its existing operation deadline minus5s reap reserve
to identity observation, stores the typed rejection/original reason and journal
presence metadata, and attempts results persistence BEFORE exact helper abort/
reap/prefix teardown. Metadata/write errors remain secondary. Initial/reap stat
reads are also bounded4096/1s within existing deadlines. Original42/30/5/5,
native binding/full-status assertions, natural-exit distinction and no-prefix-
teardown-on-reap-failure rules remain. No fresh scan or native query was added.

Validation from the owned C: checkout's scripts directory through WSL Debian:

```text
python3 -B -m unittest test_g4_rejected_identity test_g4_native_calibration_refresh test_g4_native_exit_calibration test_g4_native_exit test_g4_prefix_ownership_integration test_g4_prefix_ownership test_g4_ready_boot_integration test_g4_guest_readiness test_g4_qmp_shutdown test_g4_virgl_launcher test_g4_virgl_budget test_g4_remote_failure test_g4_idle_diagnostic test_g4_edge_owned_threads -q
Ran 158 tests in 21.417s: OK, no skips.
```

Includes all prior129 controls,15 owned-thread controls and14 new controls:
eight actual-reader tests, five collector/stat tests and one stale-preparation
guard. Tests cover exact rejected bytes/non-UTF8/quotes/NUL padding, bounded
oversize/partial reads, start/directory drift, prefix/permission/ESRCH/EIO,
deadlines/close failures, no environ leakage, valid-to-invalid collector evidence
before kill/reap, write/metadata failures and failed-reap teardown refusal. A real
owned Linux Python child validates proc transport and is terminated/reaped;
directory-replacement metadata is injected because DrvFS refuses open-directory
rename. Shared C host controls remain included, not Windows API/runtime proof.

The old immutable native preparation correctly refuses the changed runner. Its
existing positive preparation test now uses an explicitly synthetic approval for
current source, retaining its16-input/PE/command assertions; an added negative
checks the real old approval refuses dispatch. No private control was edited.
An initial test accidentally called the fixture's mocked stat function; fixed to
call the actual shared function. The failed158-test log remains alongside final.

D: evidence/snapshots:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/rejected-identity-offline-r1-20261024/`.
`evidence.json` SHA256
`1c77b2ec8240f3d69765116ed00c17e72951c020cf3577bf44dc44bc2802f0bb`
pins five source/test snapshots, both logs and unchanged historical manifest,
R2 seal and private procedure. No explanation of the missing R2 rejected bytes
or execution beyond its loader is inferred. No unchanged Node/PE builds rerun.
C: branch only; existing M wine, D: master and other workers untouched. STOPPED
for parent source/test review before any separately planned integration/retest.
