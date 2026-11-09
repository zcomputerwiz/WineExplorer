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

## v2 re-review

2026-11-09. **Neither step 2 (codecs) nor step 4 (pipes) may start from v2 as frozen.** The revised loader pin source is sound in principle; remaining wire and state contradictions still require refreezing. Read TOOLING.md and CLAUDE.md first. Static review only; no code changes, builds, generators, kernel/PAM/D-Bus or VM tests. E1-S remains disabled.

| Original finding | v2 status | Remaining issue / correction |
| --- | --- | --- |
| 1. Endorsement authority | Partly covered | Spec:19-53 retains the tuple, creator and D token/version; INIT drops tuple fields, initial desktop-object comparison is absent, and the stated access protection is inaccurate. See below. Preserve retained caller/S3 comparisons and authenticated broker registration, not R13 image recognition alone. |
| 2. Kernel identity | Partly covered | Spec:32-38 fixes the pin source. Freeze checked option setup, complete ancillary parsing/ownership, later UID/pin checks and the pre-entry proof below. |
| 3. Mutual authentication / FD ownership | Partly covered | Spec:89-113 adds both directions and closure barriers, but V cannot observe ATTACH_ACK on a socket it must never read. Worker-pin delivery, reply authentication and prompt/readiness ordering remain underspecified. |
| 4. Wrong-peer attach | Partly covered | Spec:99-106 correctly refuses foreign senders without mutation and adds reservation, owner-loss and failed-transfer cancellation. Add bounded attach/admission quotas; distinguish an authenticated duplicate from an unauthenticated malformed call. |
| 5. Bootstrap / deadline | Partly covered | Cancellable INIT and the shared admission deadline are covered; SELECT waits for attach while the attach socket/worker waits for SELECT/START. V's state order also reverses worker creation and delegation. |
| 6. Selection / final confirmation | Partly covered | START/RESULT and the refused-result branch exist. Freeze retained final account/key/evidence/challenge linkage, authoritative eligibility rechecks and the unknown-account START exception; Confirm currently contradicts S1 nonce matching. |
| 7. S1 kinds / encoding | Partly covered | 76-byte header and complete 156+payload S1 kind 2 are correct. INIT, status-dependent exceptions, control envelopes, prompt layout, Confirm/Cancel round/index and challenge encoding still need exact definitions. |
| 8. Normalized PAM | Covered | Spec:108-114,138-140 preserves the fixed prompt, decoys/timing, fixed service, null-authtok flag, account management, redirection denial and open/silent D endpoint. Incorporate the attach ACK into W's explicit bootstrap, before its existing quiet-dialog checks. |
| 9. Cleanup / retirement | Partly covered | Owners, cancellation precedence and DONE are substantially covered. RESULT_SENT must not retire live evidence; expected W exit, retry-child lifetime, commit boundary and bounded terminal pipe cleanup remain undefined. |
| 10. Hygiene / inventory | Partly covered | Spec:144-148 covers checked dump suppression, clipboard/history, conversion bounds and owned-buffer wiping. Carry forward review item 10's exact implementation allowlist and named negative tests; “each gate names” is not that inventory or test matrix. |

**Open point 1 — revised native pin: yes, with the following explicit implementation conditions.**
`wine/dlls/ntdll/unix/process.c:803-848` creates the pair and passes socketfd[1] on B's own connection to `new_process`; :889 spawns D with socketfd[0]. `wine/server/process.c:1201,1234-1243,1388` receives that end and separately chooses the designated parent. Retain `current->process` as creator independently of parent; creation-time `SO_PEERCRED` at :661-669 still describes B, not D.
D adopts WINESERVERSOCKET in `wine/dlls/ntdll/unix/server.c:1633-1638`, receives its request FD at :1664-1665, then calls init_thread_pipe at :1693. Its first two sends are reply_pipe[1] and wait_fd[1] (:1585-1588), using SCM_RIGHTS via wine_server_send_fd (:955-987), before init_first_thread (:1695-1720) and PE entry (:1789; loader.c:1842-1855). Thus the first incoming receive_fd on **D's process socket** is D's loader send, not B's earlier transfer on B's socket; no PE resume is needed to acquire it.
Linux's [SCM implementation](https://raw.githubusercontent.com/torvalds/linux/v6.12/include/net/scm.h) derives receive-time credentials and SCM_PIDFD from the message's retained sender identity (scm_recv_unix/scm_pidfd_recv), supporting this design inference. This is source evidence, not supported-kernel runtime acceptance.
Set/check both options on S's receiving end during new_process, before returning success and before spawn; “before the process can run” must permit native loader startup while PE execution stays suspended. Unsupported options or invalid/negative/dead pins deny. `thread.c:1755` still copies req->unix_pid: compare it with the kernel pin, never use it as the proof or an unchecked termination target.
`wine/server/request.c:375-407` currently only extracts SCM_RIGHTS and queues the FD; it neither validates nor retains credentials/pidfds. Extend it to require the exact send_fd size, one one-FD SCM_RIGHTS plus one correctly sized credential and pidfd, no unknown/duplicate/truncated ancillary data, and valid process-local thread linkage before accepting the pin/FD. Preserve the legitimate startup SCM_RIGHTS. Set CLOEXEC, retain the first pin, check subsequent PID/UID/live-pin identity, and close every later/rejected received FD/pidfd on every exit.
“Never resumed” alone does not prove created suspended: require the original first thread's CREATE_SUSPENDED proof, current suspension and no intervening thread/entry/resume; use the existing irreversible pattern at `thread.c:1692-1701,950-958` and `process.c:1566`. A normally started then suspended child must fail even if resume_thread was never called. Use a fresh protected D for each retry, or explicitly redesign the endorsement rule.

**Open point 2 — display metadata: yes, conditionally.** INIT's B-owned frozen requester/image/publisher on the inherited endpoint is the right source. Require well-formed UTF-16LE (including surrogate validation), no NUL/control text, and no command-line/environment/public-bus fallback. Add S1's Wine-only storage/host-access limits and secure/insecure desktop indication; final account comes only from B's validated RESULT, and the password prompt stays the authenticated fixed W prompt.

**Concrete field/message/state corrections required before refreezing:**

- **Tuple/INIT (spec:23-25,75):** call S's input the pre-endorsement tuple; S adds dialog_id to seal it. INIT says “fields not in the header” but omits six u64 generations, caller_session:u32 and desktop_route:16. Include them in that order before dialog_id and compare the entire tuple. With the existing display bounds, corrected INIT is 244..1804 bytes; ACK is 156, SELECT_OK/READY 76, SELECT 81..143, RESULT refused 116/success 117..179, DONE 80. Freeze offsets and per-type limits, checked before allocation.
- **S1 Answer (spec:81):** keep the inner nonce equal to the attempt nonce, matching the outer header and S1 record 2; RESULT's fresh challenge cannot replace it. One concrete encoding is a challenge:32 prefix in the private ANSWER body, followed by the unchanged S1 record (264 bytes total for empty Confirm/Cancel). Confirm echoes the retained challenge; Cancel uses zero. Freeze expected round/index (e.g. Confirm 2/0, Cancel 0/0; password 1/0), require empty Confirm/Cancel payloads, exact duplicated fields and negotiated caps, and accept Confirm only once for the frozen final account/key/evidence/challenge.
- **Other layouts (spec:84-113):** allocate control type numbers and freeze envelope/header applicability, field widths/order, total lengths, status enums and the extended S7 fixed prompt's exact bytes/encoding. Define S-to-V endorsement, worker registration, attach reply, ACK and cancellation/release messages. Spell out zero/empty exceptions for refused RESULT and a decoy START (an unknown name has no S4 key); decoys can never produce evidence. Define fresh S4 eligibility checks and equality to the selected key/name before publishing success and accepting Confirm.
- **Remove the selection/attach cycle (spec:77,88,95,118-125):** a concrete order is INIT/ACK -> SELECT_OK (initialization accepted, no secret input) -> SELECT -> START -> WORKER_READY -> attach reservation/transfer -> DELEGATED -> READY -> password/PAM -> RESULT. Change SELECT_OK's rule accordingly. V becomes ENDORSED -> STARTED -> WORKER_READY -> ATTACH_RESERVED -> DELEGATED -> PAM -> RESULT_RETAINED -> REDEEMED/RETIRED, with cancellation from pending states. Freeze the authenticated event that advances each B state; attach is allowed only in WORKER_READY.
- **ACK/readiness (spec:103-113):** W, parked in a bootstrap state, receives and validates D's secret-socket ATTACH_ACK, then reports a bound nonsecret ACK to V over supervisor control. V never reads/watches a secret end. V closes both duplicates, reports DELEGATED and releases W to send its prompt. D enables input only after both READY and validation of that prompt against the registered worker pin; attach ACK must be consumed before W's quiet_dialog checks (`pam-worker.c:210-215`).
- **Reply authority/pins (spec:50,89,103):** a generation is not a signing key. Authenticate the expected verifier unique owner/current protected registration and bind the reply to the full attempt; either specify real signature keys/verification or remove “signed.” Transfer actual live pidfds (with explicit owners and closure), or a fully specified protected pin-registration mechanism, to V/W/D as needed; PID/UID and an undefined “pin identity” are insufficient. Verify each received sender pidfd identifies the registered live sender, not merely that a registered pin remains alive.
- **Access/rechecks (spec:45-53):** initially compare D's actual retained desktop/route and session with S's validated caller route, then bind each recheck to the operation it authorizes, including result publication, confirmation and redemption; retain caller objects/version too. Protect from creation through retirement. Allow D precisely its inherited read/write ends. Stock `sysrun.c:193-235` grants SYSTEM full access and others limited query; it does not enforce “only B and S.” Specify object/connection restrictions against other SYSTEM clients, handle duplication and existing handles rather than relying on that descriptor analogy.
- **Retirement/cancel (spec:121-134):** retain evidence/endorsement/pins/watches through redemption and Stage 2 terminal ACK, not just RESULT_SENT. Either park W through DONE or explicitly distinguish its supervised successful exit from unexpected death (today W returns after RESULT at `pam-worker.c:591-598`). Split STAGE2 into uncommitted/committed outcomes per S1: cancellation cannot relabel a committed launch; lost replies require the decided termination/reporting rule. Bound DONE writes, then close/cancel/drain/join I/O without waiting indefinitely on a dead reader; define successful resource release as well as cancellation.

Step 2 needs the corrected tuple, S1 challenge mapping and exact control/prompt layouts first. Step 4 needs the corrected INIT/ACK wire contract, usable D pipe rights, acyclic startup and bounded terminal I/O rules first. The pin-source change resolves the earlier socketpair-creator objection; it does not resolve these mechanical-step prerequisites. All kernel, WoW64, lifecycle and integration acceptance remains unrun.

## v3 check

2026-11-09. **Step 2: no. Step 4: no, against v3 as frozen.** Checked only the v2 re-review corrections; references below are to v3 spec lines. Static only; no code changes, builds, generators, PAM, D-Bus, kernel or VM tests. E1-S stays disabled.

Addressed: pre-endorsement/sealed tuple and full INIT tuple; nonce/challenge separation and Confirm/Cancel rounds; decoy START; selection-before-attach order; W consuming ATTACH_ACK and reporting W_ACK; verifier-owner authentication replacing “signed”; initial desktop/session comparison and operation-specific S rechecks; fresh retry D; parked post-result W; precommit/committed split; bounded terminal pipe cleanup. Loader option/ancillary/pre-entry conditions, validated display text, Wine-only notices, hygiene, disabled normal builds and separate R13 are carried forward. Remaining qualifications follow.

Every stated byte total recomputes correctly (FD ancillary data is outside the payload length):

| Message/layout | Recalculation, bytes |
| --- | --- |
| Common header; pre-endorsement/sealed tuple | 4+2+2+4+8+4+4+32+16 = 76; tuples = 200/216 |
| INIT | 76+(32+32+8+48+4+16+16)+3*4+(0..1560) = 244..1804 |
| ACK; SELECT_OK; READY | 76+32+32+16 = 156; 76; 76 |
| SELECT; B→D RESULT | 76+4+(1..63) = 81..143; 76+4+4+32+(0 or 1..63) = 116 or 117..179 |
| S1 Answer; pipe ANSWER; DONE | 16+8+4+4+4+4+32+32+32+16+4+payload = 156..4252; 76+32+156 = 264 for empty Confirm/Cancel; 76+4 = 80 |
| ENDORSED; START | 76+156 = 232 for the intended fixed tuple tail; 76+4+(1..63)+16 = 97..159 |
| WORKER_READY; DELEGATED; W_ACK; ATTACH_ACK | 76 each |
| V→B RESULT; CANCEL | 76+4+16+4+16+(0 or 1..63) = 116 or 117..179; 76+4 = 80 |
| S7 header; extended prompt | 8+2+2+4+8+4+4+4+4+32 = 72; 72+16+32+32+16+10 = 178; text is ASCII `Password: `, no NUL (pam-worker.c:311) |

Unfinished v2 corrections and exact fixes:

- **Tuple/INIT/display (69-108):** freeze offsets and per-type bounds explicitly. ENDORSED's “first eight fields + dialog_id” is ambiguous: replace it with the explicit 156-byte tail from binding_digest through dialog_id, excluding display strings. INIT has no specified source for the secure/insecure indication. Add `desktop_mode:u32` (1 secure, 2 explicitly permitted insecure) after dialog_id, authenticated by the inherited pipe; INIT then becomes **248..1808**, ENDORSED stays 232. This changes step 4's frozen input contract.
- **Other codecs (97-142):** enumerate type-20 status as 1 authenticated/2 refused and type-21 reason values (a single generic reason=1 suffices); specify that refused RESULT zeros everything except its status/header. Define Answer equality as the fields actually present in S1, not the entire sealed tuple. Freeze S7 magic/version/type/style/length and ASCII prompt bytes, and the exact D-Bus argument/reply types, order and full-attempt binding.
- **Registration/reply/pins (64-65,118-142):** W never receives a specified D pin registration. Freeze authenticated supervisor setup carrying the sealed tuple, D PID/UID and actual pidfd; specify W PID/UID/pin delivery to V/D, protected delivery of D's expected verifier owner, and owners/closure of D's received W pin and W's received D pin. Include exact ancillary size/duplicate/unknown rejection and rejected-FD closure on these transfers. “W validates” cannot replace that registration contract.
- **ACK/readiness/bootstrap (118-138):** W ends bootstrap before V closes both secret copies, and no event tells W that READY occurred. Freeze V→W prompt-release control: after valid W_ACK, V closes both copies, sends DELEGATED and releases W to prompt; D still waits for both READY and the authenticated prompt. Explicitly forbid Python watches/receives on secret ends. Suppress only premature secret-data checks during bootstrap, never deadline/death/stop cancellation. Give D an authenticated attach-permitted notification after WORKER_READY, so it cannot race into a terminal out-of-order attach.
- **Access/rechecks (47-63):** protection begins at endorsement, not creation, leaving the prior requested protection gap. Install restrictions atomically at protected D creation, retain them through retirement, cover pipe/connection handles and existing/duplicated handles against other SYSTEM clients, and grant D precisely its inherited read/write rights. Require the typed endorsement lookup's necessary access rights explicitly.
- **Final account (99-100,112,151-152):** fresh S4 eligibility is stated only at Confirm, and only key equality is explicit. Before publishing successful RESULT and again at Confirm, resolve fresh eligibility and require the selected retained key **and canonical name** to match the evidence/final displayed account; bind the one-use challenge to that key/name/evidence reference. Decoys must never reach either success path.
- **Retirement/release (143,158-171):** RETIRED still has no authenticated triggering event or release layout. Freeze bound cancellation/release and Stage-2 terminal acknowledgement messages; retain endorsement/pins/watches through that terminal event (consume evidence once at redemption). Release/reap parked W only then; cancellation uses the same bounded cleanup. Redemption alone must not retire D's protection or watches.
- **Quotas/inventory/acceptance (126-127,184-194):** bound admission/in-flight records and refused-sender tracking, preserving foreign-call nonmutation; explicitly retain request-owned channels or WO-15 lifetime serialization with no OPEN/CLOSE rotation. Replace the wexhost directory allowance with `e1s6.c`, `wexhost.c`, `wexhost.spec`, `wexhost_unix.h`, `unixlib.c`, and name codec/test files. Carry forward named malformed/replay/caps/UTF-8 vectors and step-4 oversized/partial INIT, blocked writer, broken pipe, cancellation and unrelated-handle tests; the current negative list omits them.

Step 2 still requires the missing control/registration/release codecs and enums; step 4 requires the final INIT/display contract and creation-time pipe protection contract. Refreeze those precise inputs before issuing mechanical orders. Correct arithmetic and source-level design do not establish supported-kernel, WoW64 or lifecycle acceptance.
