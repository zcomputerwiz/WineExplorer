<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# S6 second-batch acceptance review, 2026-11-07

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
