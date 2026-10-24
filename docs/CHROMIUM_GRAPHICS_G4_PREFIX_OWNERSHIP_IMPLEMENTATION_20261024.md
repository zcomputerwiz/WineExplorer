<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Prefix ownership scanner — OFFLINE, NOT INTEGRATED

Implements [PREFIX_OWNERSHIP_PLAN](CHROMIUM_GRAPHICS_G4_PREFIX_OWNERSHIP_PLAN_20261024.md).
No VM/QEMU/SSH/guest files, image operation, Wine/browser/calibration, private
procedure change or new manifest. M wine/mainD:master/other workers untouched.

Original stdlib library `scripts/g4_prefix_ownership.py` exposes `scan()` and
self-contained `PROBE`, suitable for future `sudo -n python3 -B -c <source>
<exact-prefix> <nonce>` WITHOUT staging/importing a guest module. Probe source
is assembled from the SAME tested implementation, not a second model. No
integration or guest dispatch occurs on import.

Versioned JSON reports exact validated prefix/nonce, complete/unknown status,
PID/start-tick owners and bounded metadata. UNKNOWN has owners=null/busy=null
and probe exit1, never an authoritative empty list. Complete+owners is busy;
complete+empty is only a point-in-time observation. All disappearance/error
cases conservatively remain UNKNOWN; actual missing-path evidence is labelled,
not silently skipped. Stable user ESRCH, EPERM/EACCES/EIO, missing/malformed/
truncated records, identity changes and quotas cannot become idle.

The scanner pins root/task directory FDs, parses stat through unusual comm
parentheses/spaces/newlines (full52-field minimum), validates field9 flags and
field22 start ticks, rechecks task/classification and directory identity. Only
explicit stable PF_KTHREAD flags exclude a task without reading its environ;
PID2/name/empty cmdline are NEVER substitutes. Exact NUL-delimited
`WINEPREFIX=<requested-prefix>` matching, successful-empty versus failed reads,
and no raw environment/comm/error-message serialization. No task signalling.

Limits: <=8s scanner deadline checks,4096 numeric tasks,1MiB/task and16MiB total
environment reads,8KiB/stat,128 detail records,64KiB output. Oversized output
becomes bounded UNKNOWN metadata. Caller still needs its10s outer timeout/reap
bound: kernel scheduling/syscall stalls are not hard-real-time preempted here.
Enumeration is not atomic and cannot prevent new processes/execs after a sample.

PF_KTHREAD value0x00200000 checked against matching
[upstream stable sched.h](https://raw.githubusercontent.com/gregkh/linux/v6.12.113/include/linux/sched.h).
[proc memory-open](https://raw.githubusercontent.com/gregkh/linux/v6.12.113/fs/proc/base.c)
and [get_task_mm](https://raw.githubusercontent.com/gregkh/linux/v6.12.113/kernel/fork.c)
explain why ESRCH alone need not mean exit. Actual guest PID2 flags remain
UNOBSERVED; no cause claim beyond the preserved r2 traceback.

## Actual tests and retained evidence

WSL Debian, owned C: checkout `scripts`:

```text
python3 -B -m unittest test_g4_prefix_ownership test_g4_ready_boot_integration test_g4_guest_readiness test_g4_qmp_shutdown test_g4_virgl_launcher test_g4_virgl_budget test_g4_remote_failure test_g4_idle_diagnostic -q
Ran 89 tests in 6.451s — OK, exit0, no skips
```

14 new methods plus prior75. Fault fixtures execute actual scanner reads/parser:
PF_KTHREAD PID2 with forbidden ESRCH environ read, nonkernel PID2, exact/near/
empty/partial environments, stable user failures, unusual/truncated stat, PID/
directory replacement, disappearance, quotas/deadline and UNKNOWN-not-empty.
Actual PROBE runs in subprocesses with fixture proc-root mapping for complete
and ESRCH/nonzero cases. Native Linux control reads REAL stat/environ/task FDs
for TWO explicitly enumerated owned original children, exact prefix versus near
match; only exact child detected, both released/reaped0 and absent. It is not a
whole-host scan, guest readiness or Windows/Wine proof. No raw secrets serialized.

Initial combined run exposed inherited integration fixture coupling to the
mutable disk: later r2 legitimately changed its real identity/hash, so intended
content/backing negatives stopped at an earlier guard. ONLY that test fixture
now supplies its already-referenced sealed r1 identity/hash. Assertions, private
code and diagnostic policy unchanged; all four distinct negatives now execute.

Private evidence:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/prefix-ownership-offline-r1-20261024/evidence.json`
SHA256 `d5406979e76af704e7beb2584bc0204db86dd10e32b8755a23375670d1e602c9`;
pins final source/test/control hashes and actual command outcome. Native control
is retained at `prefix-ownership-control-5_ycv_nv/control.json` in the same lab,
SHA256 `14b5ac580038dadeeedb3471f448cdb22f7f7bf6c43e67e10d3af738a91fda65`.
Module SHA256 b62db974d23d8f8a1bf8f38a43f8457e200725469319619d8ce385e8d8de5d39;
9825-byte PROBE SHA2562777b9d53af9ae10d7dc67f187fd65de6462838d4cae8782995ed472c8dfbf46.
Earlier local control artifacts remain preserved. STOP parent review before
integration/preparation or any live probe; no historical error is erased.
