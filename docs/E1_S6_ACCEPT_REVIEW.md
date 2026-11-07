<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# S6 second-batch acceptance review, 2026-11-07

The historical investigation below is superseded on failure attribution by
the final **Round-3 import failure** section. That section records the newly
read matching logs, deterministic source cause, working-tree repair and limits.

Static review and working-tree edits only. No commit, build, WSL, Wine, native
service, kernel probe or fixture execution. The exact supplied output in
`E1_S6_ACCEPT_FAILURE.md` was read, along with E1_BATCH_REPAIR, E1_S6_TRANSPORT,
E1_S5_BROKER_RECORDS, E1_S10_TOKEN, E1_H2_CALLER_BINDING and STOCK_CHANGES's S3
entry, against the sources. The working tree already contained the earlier
denial-site instrumentation and EOF/echo fixture corrections described below;
those changes were preserved. This follow-up adds child exit-site diagnostics,
separates server handle/object/dup failures and broker I/O/image failures,
names the native verifier's generic checks and corrects the interpretation of
the supplied failure. It does **not** claim to have repaired the original
underlying transport failure.

## What the observed number proves

`wine/include/winerror.h:227,235` defines ACCESS_DENIED as **5**, INVALID_DATA as
**13**. No server NTSTATUS is directly returned as 13 by the S6 transport.
`e1s6_transport` returns Win32 5 on failed Open/child/exchange; `e1s6_probe`
discards that result and retains its own default 13 when a fixture step fails.
The probe also prints the reply error on its completion-marker assertion.
Consequently, neither a printed 13 nor E1S6 READY identifies a server denial.
READY precedes the first Open, socket credential/pidfd option checks,
endorsement, proc/executable check and child exchange.

The exact original failing boundary **cannot be established from the supplied
output**. This review does not claim a particular security check was taken or
that the original acceptance failure is repaired. Choosing an S3, pidfd or
token check and bypassing it would be unjustified. The edits expose that missing
evidence while correcting independently established fixture weaknesses.

## Every explicit 13 route for transport-accept

These anchors describe the edited working tree. All six explicit fixture-error
paths below return before publishing the private completion marker.

| Broker route | Current source / audit site | Why it returns 13 |
| --- | --- | --- |
| Binding comparator self-test unexpectedly says a changed token ID is equal | `main.c:1523`, `probe-binding-comparator` | Explicit INVALID_DATA return. |
| Canonical S5 record freeze fails | `main.c:1524`, `probe-freeze-record` | Leaves `e1s_probe`'s default INVALID_DATA; no transport call has occurred. |
| S6 Open returns any nonzero result | `main.c:1435`, `probe-open` | Leaves `e1s6_probe`'s default INVALID_DATA. Now logs the actual transport error. |
| S6 CHILD_START returns any nonzero result | `main.c:1450`, `probe-child-start` | Same default. |
| S6 EXCHANGE returns any nonzero result | `main.c:1474`, `probe-exchange` | Same default. |
| Final live S3 check fails after a successful exchange | `main.c:1490`, `probe-final-binding` | Same default, although the binding helper itself returns 5. |

All six paths lack the completion marker. There is no owner/stale/replay/cancel
branch on accept. Success explicitly sets error 0 and the marker. A missing
transport export returns 50; failed RNG returns 31; initial `check_binding`
returns 5. The S5 state machine after the transport branch is unreachable for
this mode (`main.c:1527`).

Before that dispatch, `client_thread` can propagate an I/O error;
`handle_request` can propagate `freeze_image` errors. These are the other
possible sources of a numeric 13, rather than an explicit fixture sentinel:

* `pipe_io` propagates CreateEventW, immediate ReadFile and GetOverlappedResult
  errors (`main.c:279-307`); separate `pipe-create-event`, `pipe-start-io` and
  `pipe-io-result` sites now distinguish them. Its timeout/cancel branches do
  not return 13.
* `freeze_image` propagates CreateFileW and lease-event CreateEventW errors
  (`main.c:630-640`), with `freeze-open-image` and `freeze-lease-event` sites.
* `hash_file` propagates ReadFile's error (`main.c:606`, `image-hash-read`),
  through `freeze_image`. BCrypt and seek failures are normalized to 31.
* `freeze_image` propagates WexLeaseTake's return (`main.c:641-642`,
  `freeze-lease-take`). The current lease implementations' explicit failure
  constants do not include 13; this is a forwarding boundary, not evidence of
  such a return in this run.

Parsing and table admission do not return 13. The `request-*`/`probe-*` sites
separate pre-dispatch errors from the actual fixture. Existing S5 successes
are useful controls, not proof that any particular later call succeeded.

## Complete transport denial trace

1. `wexelevate_probe.c:e1s_mode` builds one message, opens the broker pipe and
   itself calls WriteFile. It neither duplicates that pipe nor delegates the
   request write. `handle_request` consumes that message once, identifies the
   Limited caller, parses it, admits its ID and freezes its image. The lab
   envelope does not supply caller identity. `check_binding` compares original
   opener/writer process and primary objects, captured/current IDs and version,
   impersonation flags, liveness, SID, logon and session. A failed native query,
   broker impersonation, failed query or any mismatch returns 5. Each check now
   has a gated `binding-*` site; entry failures have a `request-*` site.
2. `wexhost.c:291` constructs the same fixed 40-byte Unixlib call in both ABIs.
   It returns 50 for an unavailable Unix component. The gated dispatcher in
   `e1s6.c:421` defaults to 50 for invalid operation/channel/size/pipe or revoked
   IDs; recognized failed setup/operations return 5 and clear bounded output
   and channel after cleanup. Exhausted IDs also fail Open. Logs now distinguish
   each boundary; no shipping runtime switch enables the implementation.
3. `e1s6.c:286` Open can fail at libdbus loading/symbols, private bus connection,
   owner/PID/UID queries, socketpair, message allocation/arguments/reply/sender/
   shape/challenge length, socket peer credentials, PASSCRED/PASSPIDFD/PEERPIDFD,
   repeated owner identity, endorsement, private Open send, authenticated receive
   or role/generation/channel/session registration validation. Each site has a
   distinct `open-*` audit. `live()` separately checks bus, live service pin and
   actual unique owner; every receive checks credential PID/UID, exact one live
   SCM_PIDFD, ancillary types, size/truncation and live owner again.
4. `e1s6.c:234` imports endorsement and broker-created pin sockets into
   non-inheritable Wine handles, sends the 96-byte sideband on the **original
   request's server pipe**, and closes imported and four S3 result handles.
   `named_pipe.c:1763` can deny authority, handle access/type, disconnected pipe,
   missing consumed provenance, impersonating opener/writer, different writer
   process/primary, replaced opener primary, or changed opener/writer IDs. Partial
   result-handle allocation failures unwind all allocated handles. Its audit is
   compiled only with WEX_E1S6_TRANSPORT_TEST. The sideband checks remain equal
   to the original checks, in the original evaluation order.
5. `sysrun.c:397` returns STATUS_ACCESS_DENIED for wrong sideband size, either
   failed fd acquisition/duplication, broker thread token, missing mapped-image
   role, missing protected SYSTEM role, unprivileged sysrun connection, non-SYSTEM
   primary or missing enabled Tcb, invalid pin peer credentials, wrong native
   UID/PID, non-seqpacket control socket, RNG open/read/zero/collision, stale
   broker generation, replaced primary/changed TokenId/ModifiedId, failed group
   raise or failed direct send. These now have individual `endorse-*` sites.
   `e1s6_get_fd:372` now also distinguishes handle lookup, FD-object lookup
   and F_DUPFD_CLOEXEC failure independently for control and pin sockets,
   retaining the actual NTSTATUS/errno. The broker image is acquired only through the first actual executable view
   (`mapping.c:429-437`, `sysrun.c:316-328`). The fixture stages that exact test
   image in shared system32 (the install immediately before `BROKER_ORIG=1`).
6. `e1s6-lab.py:430` Open denies unsupported versions/caps, capacity/history
   limits, invalid socket, root broker UID, dead peer pin, or socket creator
   PID/UID differing from the actual bus sender; OS/bus/FD/serialization setup
   failures also deny. `message:331` checks deadline/owner/broker/server/child
   liveness, bounded framing/ancillary identity, actual data sender and phase.
   `endorsement:293` checks generations/broker PID, challenge, sequence/operation/
   child/reserved bytes, UID/system GID, kernel pidfd/fdinfo PID, retained proc
   directory, executable inode, generation history and subsequent registration.
   The first failed require logs a fixed site plus function/source line. Caught
   OS/bus exceptions log the innermost function/line and numeric errno, never
   packet bytes or arbitrary exception messages.
7. CHILD_START (`e1s6.c:351`) can fail phase, socketpair/options, fork, child
   setup/hello, endorsement, private send/receive or role-3 validation. Native
   code requires the original broker registration, exact answer template/channel,
   expected child in the endorsement, actual child hello credentials/pidfd and
   exact generations. EXCHANGE re-endorses and requires the issued child record,
   bound answer/template, actual child sender and exact echoed bytes. Any failure
   closes the native transaction; the dispatcher returns 5. The broker then
   returns its fixture sentinel 13 without a completion marker.

There is a diagnostic gap in the old stub: all post-fork failures used `_exit(1)`
after stderr was redirected to `/dev/null`. A valid child-impostor test could
therefore be confused with a broken stub until the targeted native assertion
was checked. The stub now uses private exit codes 64..79, mapped to fixed
`E1S6_DENY wexhost site=child-* child_exit=N` lines by the parent's cleanup and
disconnect wait (`e1s6.c:68-91,165-186`). A compile-time assertion keeps the
exit-code/site table sizes equal. They distinguish group drop,
parent-death-signal installation, parent change, dump protection, channel dup,
CLOEXEC, failed/unavailable close_range, null-stream setup, hello send, answer
wait/size, echo send and final EOF wait/read. No stderr or extra inherited FD
is retained by the child. Each branch still fails closed. Native packet,
pidfd/proc/executable, bus-caller and answer checks now have explicit fixed site
names as well as the existing function/source-line information. Open's socket
wrapper exception now goes through that same audit before returning the fixed
D-Bus denial; its successful path is unchanged.

## Candidate findings

* S3 is intentionally strict. The native sockets being passed are not the
  caller's Wine request pipe. There is no static evidence of an opener/writer
  mismatch on this fixture. No comparison was removed or relaxed.
* S10 `token.c:e1s_broker` is reached only through the account-token request/
  installation machinery, not S6's binding/endorsement request. It cannot be
  this route's direct denial site; token.c was not changed.
* The S5 constructor runs for S6, but the synthetic evidence/consume/commit
  branches do not. An accept result of 0 remains the documented expectation.
* `F_DUPFD_CLOEXEC` duplicates a descriptor; the server uses it synchronously
  with no exec before its send/close. The native echo child also does not exec.
  No static evidence justifies undoing CLOEXEC.
* The repair's retained proc-directory/pidfd checks preserve their pin/liveness
  bracketing and compare the actual executable inode. They may encounter kernel,
  procfs or permission failures at runtime; READY never tests them. These checks
  were preserved. Root UID alone does not prove permission to read a foreign
  non-dumpable process's proc exe; a procfs/ptrace permission failure would be
  reported at `check_executable:84`, but none is evidenced by the supplied log.
  [libdbus's message API](https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html)
  documents duplicated, caller-owned received Unix FDs; reply unref is not
  evidence of an FD lifetime failure here.

## Subsequent cases and independently proven fixture repairs

The harness now requires each case's new log lines to contain its actual
target boundary, as well as the existing completion marker/result. A generic
setup failure must not pass as a completed negative. All cases require a real
Open checkpoint; both client ABIs remain in the matrix.

| Case | Required boundary and review |
| --- | --- |
| Open / accept | Three role records, all endorsements and exact child echo. Wexhost now compares the received echo to the submitted answer too. |
| stale | Byte 48 changes server generation in the role-3 registration, not endorsement authority; native `exchange-registration` must reject. |
| replay | First exchange succeeds, second packet is actually sent in phase 3; native `late-replay` must reject. Revoked IDs must be zeroed on later calls. |
| late | The real owned child is killed/reaped; native `live-child-pin` must revoke it. The late/revoked call cannot reopen the ID. |
| owner | After valid child setup inject a different saved unique owner; wexhost `live-owner` must reject before the next endorsement. |
| disconnect / EOF | Close broker data socket; native `receive-eof` cancels and closes the child endpoint. **Old code accepted any waitpid exit, including signal/crash/error.** The child now treats EOF before an exchange as successful cancellation; the test requires normal status 0 before cleanup's kill fallback. |
| cancel | Exact bound kind 3 reaches native `phase=cancel`, closes without success echo, then rejects old IDs. |
| child impostor | Real same-UID child hello differs from the endorsed expected PID; native `child-identity` must reject. Generic failed child setup no longer suffices. |
| Wine pipe impostor | Positive transports run first; the ordinary process's valid own pipe reaches the server's `authority` denial. Generic Open failure no longer suffices. |
| Native bus impostor | Positive transports run first; forged endorsement must reach native `endorsement-gid`, not merely encounter EOF somewhere during setup. |

`el_up` now retains bootstrap stdout/stderr in `e1s6-wine.log` only in S6 lab
runs instead of discarding the broker/server audit. Harness failure prints its
and `e1s6-service.log`'s last 120 lines before cleanup. The probe prints actual
error, expected error and completion-marker status on mismatch, labeling 13
correctly. All C diagnostics are behind the existing test macros. Python logs
belong exclusively to the uninstalled native lab fixture. No production build,
installer, service registration, capability, protocol or authorization rule was
enabled or weakened.
The harness's binary string checks also require the new audit in lab artifacts
and its absence in normal broker/server/wexhost artifacts, with positive controls.

## Qwen handoff and unverified work

Force rebuild of **server/named_pipe.o as well as server/sysrun.o** with
WEX_E1S6_TRANSPORT_TEST for the new pipe-site assertions; force rebuild of the
S6 wexhost Unix object, relink wineserver/wexhost.so, and let the harness compile
its WEXELEVATE_TEST_APPROVAL broker/probes. Preserve strict flags; no
`-fpermissive`. Run only as root in clean wexbuild / an owned throwaway VM,
never reset shared VM B. Keep the existing shipping exclusion controls.

Collect the first failed `probe-*` site, actual transport error, and corresponding
wexhost/server/native sites from the same request. The exact section-3d output
is now available in E1_S6_ACCEPT_FAILURE; it contains none of these sites. The
matching stderr/service denial lines remain necessary to distinguish the six
explicit 13 routes and their underlying failures. The historical failure cannot
honestly be assigned to a single branch from the existing evidence. No check,
completion marker or expected accept result was weakened to manufacture a pass.

Passed static checks: separate Git Bash `bash -n` for the three S6/sysrun shell
files, root and Wine `git diff --check`. No compiled C/Python, service behavior,
kernel support, procfs permissions, actual bus FD lifetimes, complete original
failure attribution, either ABI's runtime results, signal/FD/deadline stress or
rebuilt shipping exclusions are verified by this review. These are required
rerun results, not claimed passes.

Follow-up tooling observations: the initial default-PowerShell `rg` and
`Get-Location` calls returned exit 0/empty output; none was counted as evidence.
`cmd.exe` with `login:false` and direct Node filesystem/execFile calls produced
visible results. A quoted alternation search failed with
`The filename, directory name, or volume label syntax is incorrect. (os error 123)`;
separate `rg -e` terms worked. Root `rg --files` encountered
`rg: ./unsloth-tmp: Access is denied. (os error 5)`; explicit instruction-path
reads avoided that directory. A CMD search containing single-quoted
`'view->fd'` matched nothing because CMD retains single quotes; a `view-` search
and numbered source read resolved it. Upstream dbus-bus/dbus-connection source
fetches timed out with HTTP-tool error 400; no runtime inference was made from
them. No WSL, distro, VM, service, fixture or build was started or changed.

## Round-3 import failure (2026-11-07; static repair)

### Evidence and root cause

All three files in the readable external directory
`D:/DeepThought/ChromiumLab/e1s-baseline/s6-logs-round3/` were read. The Wine log
at 404-405 shows `endorse-import-control`, then `open-endorsement`, phase and
sequence zero. The native log contains READY and an exception in
`call_blocking:696`, errno zero. There is no native Open checkpoint. The
policy-state file was collected after cleanup, shows the policy/service absent,
and uses an invalid `/org/freedesktop.DBus` object path; it is not evidence that
the live Open lacked policy. The original report excerpt was not needed.

The cause is a zero **access mask**, not attributes zero:

1. The original `e1s6.c:240-241` passed `(fd, 0, 0, &handle)` for both sockets.
   `wine/dlls/ntdll/unix/server.c:1228-1243` sends the descriptor then places
   that access and attributes verbatim in `alloc_file_handle`.
2. `wine/server/file.c:656-671` retrieves the current thread's inflight FD,
   creates a file object and calls the ordinary `alloc_handle` with that access.
   `object.h:212-220` and `file.c:88-102` show no file-specific access remapping
   that could turn zero into a nonzero mask.
3. `wine/server/handle.c:266-275` explicitly rejects mapped access zero and
   sets STATUS_ACCESS_DENIED **before** checking the object's DACL. Thus, if
   fd/object allocation succeeds, this call must fail with c0000022. The old
   run did not log its returned status, so that exact numeric status is a
   source-derived prediction, not a measured result.
4. The repair at `src/dlls/wexhost/e1s6.c:341-344` requests only
   FILE_READ_ATTRIBUTES (0x80) for both handles. Attributes remain zero;
   `handle.c:238-250` only adds inheritance for OBJ_INHERIT. Normal allocation
   and DACL checks still run. `file.c:336-338` grants Local System FILE_ALL_ACCESS
   in the mode-derived descriptor; the authorized broker's SYSTEM primary can
   obtain this metadata right. No stock Wine code or security gate changes.

The server-side helper only needs a broker-local handle carrying an FD:
`sysrun.c:372-393` looks up that process's handle with access zero, acquires its
FD object and duplicates the native FD with F_DUPFD_CLOEXEC. Granting a minimal
nonzero file right meets the allocation contract; it does not register a role
or bypass the endorsement authority checks at `sysrun.c:416-426` or the S3
checks in `named_pipe.c:1763` onward.

### Other candidate causes

| Candidate | Source evidence and conclusion |
| --- | --- |
| File import rejects sockets / cannot stat them | `file.c:114-142` requires fstat but contains no regular-file or socket-type rejection. `fd.c:2084-2097` stores the native descriptor in an anonymous FD object; `fd.c:2125-2128` returns it unchanged. A working Unix socket can be carried by this path. No new object or raw-FD sideband is necessary. Environmental fstat/allocation failures remain possible and will now expose their NTSTATUS. |
| Actual control is SOCK_STREAM | The checked-in service's `Transaction.initialize` creates both control/data pairs as AF_UNIX SOCK_SEQPACKET (`e1s6-lab.py:257-260`); `e1s6.c:425` transfers the returned control duplicate into endorse_fd. A different deployed service using SOCK_STREAM would encounter the later `sysrun.c:426` seqpacket denial, not a file-import type filter. Keep that rejection. Deployed-source parity remains a rerun check. |
| CLOEXEC/duplicate was consumed or closed | Open retains its reply through endorsement and owns the extracted FDs until cleanup (`e1s6.c:387-461`); control is assigned to endorse_fd before import and not closed on that route. No exec occurs before import. The server duplicates with F_DUPFD_CLOEXEC, then uses/closes its local copies synchronously (`sysrun.c:385,481-488`). CLOEXEC is not a close-on-import flag. No evidence supports undoing it. |
| Wrong process/thread context | `wine_server_send_fd` tags SCM_RIGHTS with GetCurrentThreadId (`ntdll/unix/server.c:975-987`). `request.c:391-406` requires that thread to belong to the sending server client process and queues it there. The failing Open import runs on the original Wine broker thread before the private native child is forked; later imports also run in that parent. Native child code never calls Wine. |
| Another queued FD is selected | `thread.c:1534-1587` indexes inflight entries by client FD in each thread, replaces a repeated key, and retrieves the requested key, draining the receive channel if needed. This is not an unkeyed FIFO. The two imports are synchronous and sequential, and neither descriptor number is reused between send and request. No extra fd-send path was added. |
| Endorsement needs a handle from another process | `alloc_file_handle` and `e1s6_get_fd` both use current->process. Wexhost's handles therefore belong to the same broker table used by get_pipe_caller_binding. The native service supplies an SCM_RIGHTS duplicate, not a Wine handle from its own table. The server verifies the separate broker-created pin's native PID/UID and performs direct kernel-credential endorsement. |

Upstream [libdbus's message API](https://dbus.freedesktop.org/doc/api/html/group__DBusMessage.html)
also documents duplication and caller ownership of received Unix FDs. This is
an API cross-check, not a measurement of the deployed libdbus or FD lifetime.

### Every working-tree change

* `src/dlls/wexhost/e1s6.c`: both imports use FILE_READ_ATTRIBUTES/attributes zero.
  All denial records carry source/status/errno. Real returned NTSTATUS is
  distinguished from locally assigned check/I/O/allocation statuses; errno is
  captured only for failed syscalls and is zero for logical mismatches, short
  sends, readiness states and timeouts. Poll/short-send records include actual
  results. D-Bus connect/call/parse errors report their error names and free
  DBusError. Wrapper sites preserve the originating status/errno through
  endorsement/open/child/exchange cleanup. Endorsement success/sequence advance
  now uses an explicit success flag.
* The same file: one anonymous shared diagnostic mapping carries each private
  child's status/errno across _exit, read only after waitpid. Existing exit-site
  codes, close_range, GID drop, non-dumpability and FD restrictions remain.
  Cleanup unmaps it. Wait status is recorded for abnormal exits. Mapping/fork
  failures deny; no extra child FD is kept. Late-child injection now checks kill,
  wait and actual SIGKILL termination instead of reporting success after an
  unsuccessful injection. Disconnect distinguishes wait errors/timeouts and
  still requires natural normal exit zero before the cleanup kill fallback.
* `tools/e1s6/e1s6-lab.py`: audit_exception records exception class, bounded text,
  D-Bus error name, originating fixture function/line and call_blocking method
  metadata. Text is JSON-escaped to one line (1024 characters before escaping);
  method fields/error names are bounded at 256. No packet/call arguments,
  authentication answers or general traceback locals are dumped. Existing
  cancellation behavior and all assertions are unchanged.
* `docs/E1_S6_TRANSPORT.md`, this review, `docs/E1_S6_ACCEPT_FAILURE.md` and
  `docs/STOCK_CHANGES.md`: record the new evidence, minimal access contract,
  diagnostic sources, case review and remaining runtime gate. GPL headers are
  retained. No scripts, broker/probe, protocol, Wine source or build flag changed;
  no commit. Pre-existing untracked files were left alone.

### Native call_blocking exception: attribution limit

The fixture had only 544 lines before this edit; `call_blocking:696` is the
innermost dbus-python library frame, not a fixture function. The
[dbus-python connection API](https://dbus.freedesktop.org/doc/dbus-python/dbus.connection.html#dbus.connection.Connection.call_blocking)
describes it as the synchronous call path. The old audit walked to the deepest
frame and discarded the exception type, text and caller, hence errno zero
does not mean success or identify the D-Bus error.

This run reached the Open reply, so initialize's broker UID/PID queries had
already succeeded. It then failed before sending E1S6OPEN. Failed Open invokes
wexhost cleanup (`e1s6.c:267`), which closes the private D-Bus connection.
The still-pending native transaction's tick/message calls `live` before receive;
`live` queries GetNameOwner(BUS) and GetConnectionUnixProcessID(owner)
(`e1s6-lab.py:302-310`). The second query can now address a vanished unique name,
raising through call_blocking before native receive can log EOF. This is an
expected consequence path, cannot cause the preceding zero-access import bug,
and does not establish an independent functional defect. The exact exception
in the saved run cannot be recovered: NameHasNoOwner is an inference, not an
observed error. Additional bus/permission/timeout trouble is not excluded until
the new exception text/name/method/origin are collected. Failures still cancel;
no bus error is ignored or accepted.

### Subsequent cases re-read against the repaired code

`scripts/test_e1s6_transport.sh` still runs the inherited root-owned fixture;
`scripts/lib/e1s6-fixture.sh` still locks, refuses foreign files, pins cleanup
by inode and exact service PID, and does not install a permanent role/service.
Its policy reload and READY checks do not prove an authenticated Open. The
case matrix in `scripts/test_sysrun_server.sh:974-1009` still requires each
case's own native Open checkpoint plus its targeted site, and both ABIs.

| Case | Re-read path / expected evidence; runtime unverified |
| --- | --- |
| Open / accept | Unique root bus owner, data peer credentials/pidfd, unchanged owner, two metadata imports, original S3 pipe endorsement, E1S6OPEN, role-1/2 registrations, re-endorsed role-3 child and exact echo. Actual Open/child/exchange stages remain required (`main.c:1434-1450,1471-1496`; Python message/endorsement). |
| Open reply / EOF | Python Open retains remote sockets until its high-priority idle release, not until transaction cancellation (`e1s6-lab.py:273,460-465`). UnixFd owns its reply duplicate. Wexhost closes its owned extracted duplicates on failed setup/cleanup. No extra broker endpoint is retained by the new diagnostic mapping. Actual serialization and EOF timing require the live rerun. |
| Child impostor | Test endorses broker PID while the actual child sends hello; native child-identity must reject its real credentials/pidfd (`e1s6.c:start_child`, Python:385). No generic import/setup failure can satisfy that site. |
| Wine pipe / native bus impostors | Own-pipe impostor must reach named_pipe authority denial, after positive controls. Native impostor must reach endorsement-gid under actual sender credentials, not merely EOF. Minimal imported file rights convey neither authority. |
| Wrong owner | After valid child setup, test substitutes the broker's unique name as saved expected owner. live resolves the actual service and rejects the mismatch at live-owner before the next endorsement. Name/pidfd checks remain intact. |
| Stale generation | main.c:1452 flips byte 48 of the issued role-3 server generation. Native endorsement is still valid; full issued registration comparison must reject at exchange-registration (Python:410). Old broker generations are independently rejected by the server renewal check. |
| Replay | The first exchange/echo succeeds; phase 3 permits actually sending a second request. Native complete state rejects at late-replay (Python:434), with no second success. Cleanup zeros the local ID; main.c:1485-1488 requires that a revoked ID cannot reopen. |
| Late message | Owned child kill/wait must actually succeed. Native live-child-pin must reject the dead pinned child, either on tick or message. Cleanup/revoked-ID checks remain; generic setup failure does not pass. |
| Disconnect-is-cancel | Wexhost closes only data first, keeping the broker bus connection live while awaiting the child's natural exit. Native receive-eof cancels, closes the child's socket; the stub accepts EOF as normal cancellation. The test requires normal exit zero before cleanup and a revoked ID; a signal/crash/timeout/wait error cannot pass. |
| Explicit cancel | Exact bound kind 3 reaches native phase=cancel, closes without success echo, and revokes the ID. Kind/nonce/digest/channel/template checks remain; no credentials or launch authority are introduced. |

### Verification and Qwen handoff

Static source/diff review only. Root and nested Wine `git diff --check` passed;
Wine's working tree has no changes from this repair. No C compilation/linking,
Python/service execution, WSL/VM, native syscall probe or fixture was attempted.
No added behavioral test is claimed. The default PowerShell runner again
returned exit zero with blank output; those initial reads were not evidence.
Visible cmd.exe/login:false reads were used instead. Patch verification refused
incorrect document contexts without changing files; final diffs were reread.

Qwen must force recompilation of the lab wexhost e1s6 Unix object and relink
wexhost.so (plus preserve/rebuild the already required gated sysrun/named_pipe
server objects as appropriate), keeping strict flags and both PE ABIs. Install
the updated native lab script through the existing fixture; do not reuse the
old installed copy. Run as root only in the owned clean wexbuild/throwaway
fixture, never shared VM B. Re-run the complete acceptance/negative matrix and
shipping exclusion controls with the existing documented wrapper. Preserve
both actual source/build versions and all logs. If an import still fails, the
new source=ntstatus/status value is required; for call_blocking preserve the
new detail JSON and origin/method alongside surrounding teardown sites.

Unverified: actual imported-handle access/DACL behavior, deployed service socket
type/version, C/Python syntax under guest toolchains, mapping/fork/errno
visibility and cleanup, FD leaks, SCM credentials/pidfd/procfs/executable checks,
all post-accept cases in both ABIs, bus-disconnect/owner-loss/deadline races,
normal shipping exclusions, and runtime acceptance of this repair. Source
review proves the zero-access contract violation; it does not prove the entire
S6 transport passes after that first blocker is removed.
