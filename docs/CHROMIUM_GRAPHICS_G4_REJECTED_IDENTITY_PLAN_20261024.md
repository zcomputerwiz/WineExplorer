<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Retain rejected owned-process identity — offline work order

Resumed47c6c055 after reset0%five-hour/46%weekly. Native calibration R2 is a
preserved failure: valid saved argv arrays followed by a generic mismatch, but
rejected bytes lost. Do not guess the missing rendering or change acceptance.
Existing guest stage/output now exist; no VM/SSH/retest/overwrite authority.

## Luna implementation

Narrowly extend `g4_edge_owned_threads.linux_identity()` and the original native
calibration collector to retain the actual rejected observation before cleanup.
Keep positive return shape and ALL current exact argv-array/flat-padded match
rules, prefix/start checks, no PID-scan fallback and rejection behavior intact.
A typed ValueError subclass carrying bounded diagnostic data is suitable; do
not replace errors with successful or incomplete identity records.

Record only the owned Popen PID, expected start ticks, before/after parsed and
bounded raw stat, bounded raw cmdline, prefix-match outcome/error (NEVER raw
environ), read stages, monotonic timing, limits/truncation and original rejection
reason. No task names or unrelated process enumeration needed. Raw cmdline/stat
must be byte-preserving (e.g. base64), not shlex-reconstructed or lossy decoding.
Retain unreadable/malformed bytes/error metadata without inventing an argv style.

Bind reads to the already recorded task identity, preferably an opened proc
directory FD, so a reused numeric PID does not expose another task's cmdline.
Check start before environ/cmdline access; recheck after. If start already
differs, record only the identity mismatch and do not query/adopt another owner.
Linux [proc documentation](https://docs.kernel.org/filesystems/proc.html) explains
why an open proc task FD does not keep the process alive but cannot retarget a
reused PID. [cmdline documentation](https://man7.org/linux/man-pages/man5/proc_pid_cmdline.5.html)
also warns that processes can alter this representation; it is evidence, not a
standalone authenticity guarantee. No claim about the lost R2 bytes follows.

Bound actual reads (not merely the eventual saved slice): stat<=4096 each,
cmdline<=16384, environ<=65536 but only derived prefix status retained. Use a
small bounded budget inside the existing42s helper operation, retaining5s
kill/reap reserve; no loader/operation deadline extensions. Close FDs on all
paths. A secondary observation/write failure cannot replace the original
validation error or permit acceptance. Do not issue a fresh post-failure PID
scan or additional Wine/native query to explain it.

Collector must save rejection data into result before beginning exact owned
helper abort/reap and prefix cleanup. Preserve natural-exit vs cleanup timing,
original error, no-native-journal status and failed observer state. Reap failure
must still prevent prefix teardown; no broad signalling/adoption or retries.
Keep all original native binding/wait/full-status assertions unchanged.

## Required executable tests

- Actual identity reader: initial accepted argv then changed/rejected raw bytes;
  spaces/quotes/NUL-padding/non-UTF8/malformed/oversized representations remain
  rejection with exact bounded bytes. Accepted existing forms remain accepted.
- PID/start drift before and after reads, prefix mismatch, permission/ESRCH/EIO,
  truncation/deadline and cleanup/write errors. No unrelated PID read or raw
  environment/secret marker in serialized evidence; FDs close on every path.
- Actual native collector with fake owned process: valid-to-invalid transition,
  rejection evidence saved BEFORE helper-kill/reap and prefix teardown, primary
  error remains; no false native status or acceptance. Preserve existing failure
  order/reap controls. Add an owned native Linux child/fixture control if useful,
  bounded and fully reaped, not a Wine/Windows execution claim.
- Run existing129 regression controls plus relevant owned-thread tests and new
  tests; report overlapping suites honestly. Do not weaken assertions to pass.

Tracked source/tests/report only, private controls/evidence D:. No private
procedure integration/preparation, native PE build/execution, image/launcher
change, guest access, debugger/attach/suspend, production Wine/compositor patch
or browser run. Commit AND push verified work; STOP parent source/test review.
Then choose a separately reviewed isolation/path plan for any retest; preserve
failed R2 stage/output/layer.95%five-hour/99%weekly stops/no reset credits.
