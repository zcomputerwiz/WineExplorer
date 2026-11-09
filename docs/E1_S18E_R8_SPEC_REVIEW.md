<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S18e R8 frozen-spec review

2026-11-09. **Not ready for mechanical step 2 (codecs) or step 4 (pipes).** The frame size is wrong,
the proposed native pin identifies the socketpair creator, and attach/readiness ordering is inconsistent.
Read TOOLING.md and CLAUDE.md first. Static only: no code changes/builds/generators/PAM/D-Bus/kernel/VM tests; E1-S stays disabled.

## Coverage of the ten findings in E1_S18E_R8_REVIEW.md

| Finding | Status | Evidence and remaining correction |
| --- | --- | --- |
| 1. Endorsement authority | Partly covered | Spec:45-59 adds creator, first image and primary object/version. Its S-to-V endorsement omits both digests, all generations, session and route; authoritative rechecks and protected process/thread/channel access remain unspecified. See C2. |
| 2. Kernel identity | Not covered | Spec:53-54 chooses the wrong endpoint identity for a spawned child. The actual-creator record is useful but does not repair the socketpair pin. See open point 1. |
| 3. Mutual authentication / FD ownership | Partly covered | Spec:83-84 states both directions; it lacks the worker-registration/reply format, D's pin check, ancillary rejection rules and proof all Python duplicates are closed before input. See C5. |
| 4. Wrong-peer attach | Partly covered | Spec:89-91 correctly leaves unauthorized attempts untouched and makes authenticated duplicates terminal. Missing dialog-owner loss, quotas, failed/lost-transfer cancellation and distinct attach reservation/completion states. See C3/C5. |
| 5. Bootstrap / deadline | Partly covered | Spec:68-70 queues INIT after resume and gates input; one admission deadline is retained. ENDORSED-only attach conflicts with ACKED, and worker readiness precedes account-dependent start. See C3. |
| 6. Selection / final confirmation | Partly covered | SELECT/RESULT now exist and PAM_USER changes deny. Exact B-to-V start/result messages and the retained final account/challenge binding are absent; refused RESULT also leads syntactically to CONFIRMED. See C4. |
| 7. S1 kinds / wire translation | Partly covered | Kind 2 and answer_kind 1/2/3 are corrected. Header arithmetic, S1 encapsulation/capabilities, prompt layout and replay/state rules are not frozen. See C1/C4. |
| 8. Normalized PAM | Covered | Spec:108-110 preserves one fixed prompt, decoys/timing, fixed service, PAM_DISALLOW_NULL_AUTHTOK, account management and redirection denial; no arbitrary PAM/MFA. Fix the separate premature-EOF defect in C6. |
| 9. Cleanup / retirement | Partly covered | Spec:98-104 adds several losses, terminal cancellation and broad owners; DONE keeps D alive. Missing success boundary, cancellation precedence, pipe/owner/token/route losses and detailed cleanup ownership. See C6/C7. |
| 10. Hygiene / inventory | Partly covered | Spec:81-82 rejects overlength UTF-8 passwords. Dump policy, clipboard/undo/history, wiping on every exit, log suppression, implementation allowlist and concrete acceptance remain absent. See C8. |

## Answers to the two open points

1. **No: creator plus SO_PEERPIDFD on the existing process socket is insufficient.** In
   wine/server/process.c:1201 takes the creator-supplied inflight socket; :1234-1243 selects a designated parent separately, and :1388 passes that socket to create_process.
   Record a retained `current->process` creator there, independently of `parent`/parent_id (:722).
   wine/dlls/ntdll/unix/process.c:803-829 creates the socketpair in the creating process before spawn_process at :889; fork/exec passes its other end to the child (:419-446).
   process.c:661-669 reads creation-time SO_PEERCRED, and :698 stores that FD as process->msg_fd;
   this is not a fresh connection established by D. SO_PEERPIDFD likewise uses the saved peer PID,
   so it pins B on this path, not D. See [Linux socket implementation](https://raw.githubusercontent.com/torvalds/linux/v6.12/net/core/sock.c) (SO_PEERPIDFD case) and [unix(7)](https://man7.org/linux/man-pages/man7/unix.7.html) (socketpair credentials).
   thread.c:1755 simply copies req->unix_pid; comparing to it cannot supply independent proof.
   Correction: specify a one-shot native startup handshake on the server-retained D endpoint, with
   receiver-enabled SCM_CREDENTIALS/SCM_PIDFD, exact tuple/challenge and retained-object linkage,
   rejecting wrong/extra/truncated ancillary data and pin substitution. Acquire it during trusted
   loader startup before PE execution/resume, or explicitly split provisional creation from final
   endorsement. A fresh D-established socket is another design, but requires equally explicit linkage.
   Match D's bus identity and W's sender checks to registered live pins; unsupported kernel proof denies.
2. **Yes, for display metadata, conditionally.** INIT must use B's frozen validated requester/image/
   publisher snapshot and the authenticated inherited B-to-D endpoint; no command-line, environment,
   public bus or path fallback. Validate UTF-16 and reject unsupported/control text rather than allow
   spoofed labels. Show the Wine-only storage/host-access limits required by S1:51. Clarify that this
   restriction concerns display metadata: the one normalized password prompt is a fixed UI literal
   or authenticated W prompt, and the final account arrives through B's validated RESULT.

## Required concrete corrections before refreezing

- **C1 — framing (spec:33-38,75-84):** the declared fields total **76 bytes**, not 80. Use 76 with
  no implicit padding; freeze offsets and per-type min/max lengths before allocation. Add a private pipe type encapsulating an unchanged complete S1 Answer.
  S1:11/27 requires its 16-byte kind/version/length/required_caps header; Answer is 156+payload bytes.
  Check negotiated capabilities and duplicated fields; forbid answer_kind 1 on the broker pipe and 2/3
  on the password link. Reject duplicate/missing/late/out-of-order frames and unknown status values.
  Attach lacks the promised nonce: freeze its full signature and bound reply, or state the retained-state exception.
  Freeze the extended S7 prompt, start, result, registration and readiness layouts/signatures; specify
  round=1/index=0/echo-off for the single password challenge, exact fixed text and rejection of extras.
- **C2 — tuple/authority (spec:10-28,42-59):** distinguish admission fields from the tuple sealed after
  S assigns dialog_id; distribute the sealed tuple to D too. Endorse/store both digests, six nonzero
  generations, session/route and retained pin with PID/UID, not just numerical PID. Define deadline
  units as shared-boot monotonic nanoseconds, expiry at now>=deadline, clock/overflow failure denial,
  RNG failure denial, nonzero IDs, retry/overflow bounds; revoke old attempts without extending admission time.
  D's session must equal the tuple's caller session, not B's potentially different service session (:52).
  Retain trusted route/creator objects, check typed handle access and an irreversible never-resumed flag,
  and restrict process/thread/pipe access. Define S's atomic revalidation for token object/version, route, caller and generations
  at every use; V cannot query Wine objects by itself. Preserve S3 caller comparisons and kind 7.
- **C3 — ordering (spec:70-71,86-97):** give V its own states; its ENDORSED attach window must survive
  B's INIT_SENT/ACKED. Suggested sequence: resume/write INIT -> exact ACK -> reserve/transfer attach
  -> enable account selection -> SELECT -> authenticated PAM START -> worker/delegation readiness
  -> READY enables password only -> one answer -> PAM result. Starting W earlier must explicitly
  park it without a prompt until SELECT. B alone changes broker state on authenticated V events.
- **C4 — account/result/Confirm:** define B-to-V START and V-to-B RESULT, bound to the full attempt,
  with retained S4 account key/name and current eligibility. RESULT success must establish one final
  displayed-account challenge and connection-owned evidence reference; refusal goes to cancellation
  or a fresh attempt, never CONFIRMED. Freeze the account and accept empty Confirm only after that
  success/challenge, once; fixed round=1/index=0 requires that retained linkage. Refused RESULT must
  have empty account data and generic normalized timing, including unknown-account/decoy failures.
  UTF-16 <=64 bytes allows 32 ASCII characters; S7 accepts 1..63 (pam-worker.c:503-511). Freeze ASCII 1..63 bytes or UTF-16LE <=126 bytes and the name grammar.
- **C5 — delegation:** freeze worker PID/UID/pin registration, verifier owner/generation authentication
  on the attach reply, credentials/pidfd options before sends, exact ancillary checks in both directions,
  CLOEXEC and closure of all redundant ends. Require D's receipt ACK and worker-ready ACK plus closure
  of Python's copies of BOTH secret ends before READY; Python must never watch/recv secret FDs.
  Authenticated duplicate/failed/lost transfer cancels with no reassignment; wrong senders never mutate.
- **C6 — EOF (spec:82-83):** do not close/shutdown D's secret endpoint immediately after its answer.
  S7 quiet_dialog/wait_release and pre-success checks treat EOF as cancellation (pam-worker.c:210-215,329-342,542-562). Keep it open and silent through supervised PAM completion; close at retirement.
- **C7 — lifecycle (spec:95-104):** add explicit failed-result, transfer-pending, Stage-2-wait and
  COMPLETED/RETIRED transitions; DONE 1 follows actual Stage-2 completion, never Confirm alone.
  Serialize cancel against success; postcommit handling follows S1. Add broken pipe, D bus-owner loss,
  worker death, channel replacement, token/route change and partial-transfer failure. Name owners for pins, watches, both delegated FDs and I/O workers;
  remove watches before freeing, drain/join I/O, terminate/reap only owned children even if DONE fails.
  Retain one request-owned channel set (or lifetime serialization), with no registration OPEN/CLOSE rotation.
- **C8 — acceptance:** require checked dump suppression before input, no clipboard/cut/copy/undo/history,
  checked UTF-16-to-UTF-8 conversion, wiping every owned copy on cancel/close/error, and no secret/prompt
  logging. Carry forward item 10's inventory, normal-build NOT_SUPPORTED, separate shipping R13,
  WoW64 ABI/protocol regeneration and named negative tests per gate.

Step 2 needs C1/C2/C4 layouts refrozen; step 4 needs corrected INIT/ACK framing, C3 and C7 pipe lifetime/cancellation rules. Re-review before either starts.
