<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Narrow prefix-ownership probe repair — offline implementation first

Resumed at0%five-hour/31%weekly. Prior actual r2 failed on ESRCH opening
`/proc/2/environ`; readiness,19 binaries and strict shutdown passed. Original
probe/error remain immutable. No guest/SSH or new live authority in this plan.

## Research and inference

Saved guest serial identifies Debian6.12.113+deb13-amd64. In upstream stable
[6.12.113 proc code](https://raw.githubusercontent.com/gregkh/linux/v6.12.113/fs/proc/base.c),
environ uses the memory-open path; a missing memory descriptor yields ESRCH.
[Task memory code](https://raw.githubusercontent.com/gregkh/linux/v6.12.113/kernel/fork.c)
excludes PF_KTHREAD tasks from get_task_mm. Thus a live kernel task can generate
this error; ESRCH does not by itself establish that the PID exited. This fits the
observed failure but guest PID2 flags were not captured, so identity remains an
inference. Debian patches may differ from the matching upstream stable tag.

[Kernel proc documentation](https://docs.kernel.org/filesystems/proc.html)
describes process-directory lifetime and dead-descriptor ESRCH behavior.
[stat field definitions](https://man7.org/linux/man-pages/man5/proc_pid_stat.5.html)
give field9 flags and22 starttime; interpret PF_KTHREAD from the matching
[scheduler header](https://raw.githubusercontent.com/gregkh/linux/v6.12.113/include/linux/sched.h).
Use this flag, not PID2, task name, empty cmdline or blanket ESRCH suppression.
Do not depend on a newer kernel's optional status-field spelling.

## Luna deliverables

Add an original stdlib-only tracked `scripts/g4_prefix_ownership.py` and tests.
Expose a bounded scanner and self-contained remote probe source suitable for
`sudo -n python3 -B -c <source> <exact-prefix> <nonce>` without guest staging or
external imports. Keep the existing private procedure unchanged this handoff.
Do not copy Linux implementation code; public contracts inform our own scanner.

Return structured JSON with schema/version, exact requested prefix/nonce,
complete/unknown status, matching owners with PID/start ticks, and bounded
classification/error metadata. UNKNOWN must not expose an empty authoritative
owner list or return success. Complete+owners is busy, complete+empty is a
point-in-time observation only. Do not serialize other processes' environment,
cmdline secrets or raw environment values. No process mutation/signalling.

- Read numeric PID directories, parse stat robustly with parentheses/spaces in
  comm, validate PID/flags/starttime. Associate each read with the same task
  identity, preferably an opened proc directory FD; recheck identity after reads.
  Explicit PF_KTHREAD tasks can be recorded/excluded from user-prefix owners
  without reading their environment. Retain observed flag/identity metadata.
- Match exact NUL-delimited `WINEPREFIX=<prefix>` bytes, not substrings. Require
  a valid absolute expected prefix and caller nonce. Keep deadlines/byte/process/
  output quotas within8s to leave room for the existing10s remote bound.
- Vanished paths may be classified only on actual disappearance evidence;
  permission errors, stable user-task ESRCH, malformed/truncated data, identity
  replacement, EIO, quotas and uncertain lifetimes must yield UNKNOWN/nonzero.
  Never broadly catch OSError and continue. If robust disappearance handling
  would broaden scope, conservatively report UNKNOWN rather than claiming idle.
- Do not infer that zero readable bytes proves an unreadable task is harmless;
  successful empty environ on a stable user task is distinct from read failure.
- Clearly document the snapshot limitation: proc scanning is not an atomic
  barrier preventing new processes or execs after observation.

Meaningful tests: actual original scanner against controlled fixtures/injected
I/O faults for PF_KTHREAD PID2 with environ configured to raiseESRCH (must not
read it), a NONkernel PID2, matching/nonmatching/empty and near-match variables,
stable user ESRCH/permission/EIO, missing/malformed stat, unusual comm, PID reuse,
disappearance, quota/deadline and unknown-not-empty. Execute actual self-contained
probe source in a subprocess. Add a native Linux control spawning only owned
children with one unique WINEPREFIX and one near-match; detect the exact child,
reap it and verify absence. Bound/clean up children on every path. Host controls
are not guest readiness or live Windows/Wine proof. Use D: for retained artifacts.

Run focused new tests plus prior75 regression controls. Commit AND push module,
tests and concise report with actual results. STOP parent source/test review.
No VM/QEMU/SSH, guest files, image operation, browser/calibration, production
build, private procedure integration or new manifest in this task. Preserve
M wine/mainD:master/otherworkers,95%five-hour/99%weekly stops/no reset credits.
