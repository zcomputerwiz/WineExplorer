# E1-S18e R8 step 1: attempt tuple, states and messages (frozen spec v3, not implemented)

v3 adopts the corrections of docs/E1_S18E_R8_SPEC_REVIEW.md ("v2 re-review"). Design:
docs/E1_S18E_R8_DESIGN.md, docs/E1_S18E_R8_REVIEW.md. Participants: broker B (wexelevate, SYSTEM),
dialog D (consent.exe started by B), server S (wineserver), verifier V (root service, tools/e1s6),
worker W (S7 PAM worker, run by V's supervisor). The standard-user chain stays disabled; normal builds
keep ERROR_NOT_SUPPORTED; the shipping R13 behaviour is separate and unchanged.

## 1. Values

* Time: u64 ns of host `CLOCK_MONOTONIC`, current boot (WO-16). Expired when `now >= deadline`;
  clock failure, overflow or zero denies.
* Random values (nonce, channel_id, challenge): CSPRNG (WO-5); failure or all-zero denies.
* Ids nonzero. `attempt` 1..3 per request, never reused. A new attempt uses a fresh D (new process,
  new endorsement), revokes the previous attempt everywhere first, and never extends the deadline.
* Pins are Linux pidfds. Two pidfds identify the same process when both are alive and their
  `/proc/self/fdinfo` `Pid:` values are equal. Every pidfd has one named owner and is closed on every
  exit path; received pidfds are CLOEXEC.

## 2. Tuples

**Pre-endorsement tuple** (B, per attempt), field order as listed:
`request_id:u64, attempt:u32, nonce:32, channel_id:16, binding_digest:32, request_digest:32,
deadline:u64, root_gen:u64, server_gen:u64, broker_gen:u64, verifier_gen:u64, map_gen:u64,
policy_gen:u64, caller_session:u32, desktop_route:16`.
**Sealed tuple** = pre-endorsement tuple + `dialog_id:16`, assigned by S. S, V, W, B and D each keep
the sealed tuple and compare all of it wherever it appears.

## 3. Server endorsement of D

* **Creator.** `new_process` records `current->process` as the new process's `creator` (independent of
  the designated parent; a retained reference released on exit).
* **Kernel pin from D's loader.** In `new_process`, before returning success (so before B spawns D),
  for a creator that is the verified broker, S sets and checks `SO_PASSCRED` and `SO_PASSPIDFD` on its
  end of D's process socket; failure denies the creation. D's first sends on that socket are its
  loader's `reply_pipe` and `wait_fd` transfers (wine/dlls/ntdll/unix/server.c init_thread_pipe),
  before any PE code. `receive_fd` (wine/server/request.c) is extended for such processes: exactly the
  `send_fd` size, exactly one SCM_RIGHTS with one fd, one correctly sized SCM_CREDENTIALS and one
  SCM_PIDFD, no unknown, duplicate or truncated ancillary data, valid thread linkage. The first valid
  message's pid, uid and pidfd become D's **native pin**; every later message must carry the same pid
  and uid and a live pidfd for the same process, otherwise the process is killed. The thread's
  self-reported `unix_pid` (thread.c init_thread) is compared with the pin, never used as proof.
  Rejected or surplus fds and pidfds are closed.
* **Created suspended.** The endorsement requires the original first thread's CREATE_SUSPENDED proof,
  that it is still suspended, and that no other thread, entry or resume happened (the irreversible
  pattern already used for S14b, thread.c `e1s_profile_unstarted`).
* **Endorsement request** (new private operation, B → S): D's process handle (typed lookup in B's own
  handle table) and the pre-endorsement tuple. S accepts only if: the caller is the verified broker
  (R13) with no thread token and B's S6 registration is current; D's `creator` is B; the created-
  suspended proof holds; D's first executable image is machine-root `system32/consent.exe` or
  `syswow64/consent.exe` (device and inode, decided once, R13 semantics); D's primary token is SYSTEM;
  D's session equals `caller_session`; D's desktop is the object behind `desktop_route`; the native
  pin exists and is alive; the caller's retained S3 objects and versions still match. S retains D's
  process object, its primary token object with token id and modified id, the pin and the sealed
  tuple, assigns `dialog_id`, and returns it.
* **Protection.** From endorsement to retirement, S refuses to any process other than B and D itself:
  opening D's process or threads, duplicating handles to them, and using existing handles to them for
  anything but SYNCHRONIZE and QUERY_LIMITED (a new check on the protected-dialog flag in the process
  and thread access paths). D holds only its inherited pipe ends (WO-7).
* **Rechecks.** Each authorizing step asks S to recheck, atomically for that step: D's process and
  token objects and ids, session, desktop route, pin, the caller's retained S3 objects and versions,
  and the generations. Steps: attach (V asks), READY (B asks), RESULT publication (B), Confirm
  acceptance (B), redemption (B).
* **S → V** on the existing control socket: an ENDORSED message (section 6) with the sealed tuple and
  D's pidfd as SCM_RIGHTS (V owns its copy).

## 4. Encoding

Little-endian, no padding, exact length, checked before allocation. Common header (**76 bytes**):
`magic:4 "E1R8", version:u16 = 1, type:u16, length:u32, request_id:u64, attempt:u32,
reserved:u32 = 0, nonce:32, channel_id:16`. Strings: `u32 byte length` + bytes, no NUL. Account
names: ASCII 1..63 bytes, S4 grammar (`valid_name`, wine/server/accounts.c). Display strings:
UTF-16LE, even length, at most 520 bytes, valid surrogates, no NUL or control characters. Unknown
type or status, wrong length, nonzero reserved, invalid string, duplicate, missing, late or
out-of-order frame, or trailing bytes is a protocol error (ends the attempt).

## 5. B↔D messages (two anonymous pipes)

D's read end and write end are in the explicit handle list (WO-7); B's ends are non-inheritable; all
redundant copies are closed right after creation. B's writes and reads run in cancellable I/O workers.

| Type | Name | Dir | Body after header | Total bytes |
| --- | --- | --- | --- | --- |
| 1 | INIT | B→D | binding_digest:32, request_digest:32, deadline:u64, the six generations u64, caller_session:u32, desktop_route:16, dialog_id:16, requester:str, image:str, publisher:str | 244..1804 |
| 2 | ACK | D→B | binding_digest:32, request_digest:32, dialog_id:16 | 156 |
| 3 | SELECT_OK | B→D | (none): initialization accepted, account selection allowed, no secret input | 76 |
| 4 | SELECT | D→B | account:str | 81..143 |
| 5 | READY | B→D | (none): password field may be enabled (section 7) | 76 |
| 6 | RESULT | B→D | status:u32 (1 authenticated, 2 refused), account:str (empty if refused), challenge:32 (zero if refused) | 116 refused, 117..179 authenticated |
| 7 | ANSWER | D→B | challenge:32, then one unchanged S1 kind-2 record (16-byte S1 header, 156 + payload) | 264 for Confirm/Cancel |
| 8 | DONE | B→D | status:u32 (1 completed, 2 cancelled) | 80 |

INIT is queued before D is resumed and written by B's worker after resume. Display data is only B's
frozen, validated requester/image/publisher snapshot; D also shows the S1 Wine-only storage and
host-access limits and whether the desktop is secure. D shows nothing before it has sent ACK.

ANSWER: the inner S1 record's fields equal the sealed tuple (its `nonce` is the attempt nonce, the
same as the header). Confirm: `answer_kind` 2, `round` 2, `message_index` 0, empty payload, challenge
= RESULT's challenge; accepted once, only after RESULT 1, and only while the retained final account,
account key, evidence reference and challenge are unchanged. Cancel: `answer_kind` 3, `round` 0,
`message_index` 0, empty payload, challenge zero; accepted in any non-terminal state. `answer_kind` 1
is refused on this pipe. `required_caps` must be within the negotiated mask.

## 6. Control messages (same 76-byte header)

| Type | Name | Path | Body after header | Total |
| --- | --- | --- | --- | --- |
| 16 | ENDORSED | S→V control socket | sealed tuple fields after the header (as in INIT's first eight fields + dialog_id) + D's pidfd (SCM_RIGHTS) | 232 |
| 17 | START | B→V S6 data channel | account:str, account_key:16 (zero for an unknown name: decoy, can never produce evidence) | 101..163 |
| 18 | WORKER_READY | V→B | (none); W started, pinned, parked in bootstrap | 76 |
| 19 | DELEGATED | V→B | (none); W validated D's ATTACH_ACK and V closed both of its secret copies | 76 |
| 20 | RESULT | V→B | status:u32, account_key:16, canonical account:str, evidence_ref:16 (all zero/empty if refused) | 116..178 |
| 21 | CANCEL | either way on B↔V | reason:u32 | 80 |
| 22 | W_ACK | W→V supervisor control | (none); W received a valid ATTACH_ACK from D | 76 |

## 7. Attach, secret socket and readiness

After START, V's supervisor starts W (it holds W's pidfd), creates the SOCK_SEQPACKET pair, sets and
checks `SO_PASSCRED` and `SO_PASSPIDFD` on both ends before either peer sends, gives one end to W
(transfer list, CLOEXEC), keeps the other for D, and reports WORKER_READY. W stays parked in a
bootstrap state: it does not send its prompt and treats nothing as cancellation until bootstrap ends.

`DialogAttach(request_id, attempt, nonce, channel_id, dialog_id)` from D's wexhost on D's own
system-bus connection. V requires the bus sender's pid and uid to identify the same live process as
D's pidfd from ENDORSED, asks S to recheck D, and requires V's state `WORKER_READY`. A sender that
does not identify D is refused and the attempt is untouched. Per-attempt quota: one reservation, at
most 4 refused calls (then V ignores that sender for the attempt). A valid call atomically moves V to
`ATTACH_RESERVED`, binds D's unique bus name (watched; loss cancels), and replies with D's secret end
and W's pidfd (both SCM_RIGHTS over D-Bus). D checks that the reply comes from V's registered unique
bus owner for the current verifier generation. D sends `ATTACH_ACK` (header type 23, 76 bytes) on the
secret socket. W validates it (sender pidfd identifies D's pin), ends bootstrap and sends W_ACK to V.
V closes both of its secret copies, moves to `DELEGATED` and sends DELEGATED to B. A duplicate valid
attach, a failed or lost transfer or a missing ACK before the deadline cancels, never reassigns.

B sends READY after DELEGATED and a successful S recheck. W then sends its fixed prompt: the 72-byte
S7 header (`round` 1, `message_index` 0, echo-off style) + `channel_id:16, binding_digest:32,
request_digest:32, dialog_id:16` + the text `Password: ` (10 bytes, as pam-worker.c), 178 bytes. D
enables the password field only after both READY and a prompt whose sender pidfd identifies W's pin.
D answers with one S1 kind-2 record: `answer_kind` 1, `round` 1, `message_index` 0, UTF-8 password
(checked conversion from UTF-16, at most 4096 bytes, no NUL; longer input is refused in the UI,
never truncated). Each side checks one SCM_CREDENTIALS and one SCM_PIDFD per message against the
other's pin and rejects extra or truncated ancillary data. D keeps the secret socket open and silent
until DONE; W stays parked after its result until V releases it at retirement, so its exit is a
supervised success, not a death.

## 8. States

B: `SPAWNED` → (S endorsed) `ENDORSED` → (D resumed, INIT written) `INIT_SENT` → (valid ACK)
`ACKED` → (SELECT_OK written) `SELECTING` → (valid SELECT) `SELECTED` → (START sent) `STARTED` →
(WORKER_READY) `WORKER_READY` → (DELEGATED) `DELEGATED` → (READY written after S recheck)
`READY_SENT` → (V RESULT) `RESULT_OK` or `RESULT_REFUSED` → (`RESULT_OK` only, valid Confirm after
S recheck and fresh S4 eligibility equal to the selected key) `CONFIRMED` → `STAGE2_PENDING` (R10:
redeem, issue, load, launch prepared, not committed) → `COMMITTED` (guarded resume done) →
`COMPLETED` (DONE 1 written or writing bounded and failed). Any non-terminal state before `COMMITTED`
→ `CANCELLED`. `RESULT_REFUSED` → `CANCELLED` or a fresh attempt. After `COMMITTED`, failures follow
S1's post-commit rules (no relabel to cancelled; lost reply needs termination first).

V: `ENDORSED` → `STARTED` → `WORKER_READY` → `ATTACH_RESERVED` → `DELEGATED` → `PAM` →
`RESULT_RETAINED` (evidence kept) → `REDEEMED` → `RETIRED`; any pending state → `CANCELLED`.

Cancellation wins: every transition is a compare-and-set under the owner's lock with cancellation
checked first; late success cannot reverse it. Triggers: deadline, D death, D bus-name loss, broken
pipe, caller death or caller token/version change, desktop route change, generation change, broker
stop, image lease break, verifier or worker death, channel replacement, failed or partial transfer,
failed S recheck. Owners: B owns D's process handle, both pipes and their I/O workers; S owns D's
retained objects, the protection flag and S's pin; V owns ENDORSED's pidfd, the endorsement record,
the bus-name watch and the secret ends until delegation; the supervisor owns W and W's pidfd. Cleanup
removes watches before freeing, writes DONE with a bounded timeout, then closes and cancels pipe I/O
and joins the workers without waiting on a dead reader, and terminates and reaps only owned
processes. Successful release (after `COMPLETED` / `RETIRED`) frees the same resources in the same
order.

## 9. Kept from today

One fixed prompt, decoys for unknown accounts with fixed timing, the fixed PAM service,
`PAM_DISALLOW_NULL_AUTHTOK`, `pam_acct_mgmt`, refusal of a changed `PAM_USER`. No arbitrary PAM
prompts or MFA.

## 10. Acceptance and inventory

D: `PR_SET_DUMPABLE` 0 set and checked before the password field is enabled; no clipboard copy or
cut, no undo and no history on the password field; every owned copy (edit control, UTF-16, UTF-8,
transport buffers) wiped on Confirm, Cancel, window close, error and transport failure; no prompt or
secret logged. Files: wine/server process.c, request.c, thread.c, sysrun.c, named_pipe.c (and
protocol.def with regenerated artifacts for the endorsement request), wine/dlls/ntdll/unix only if
the loader send order must be asserted; src/dlls/wexhost (DialogAttach export, both Unix call tables,
PE wrappers), src/programs/consent/consent.c, src/programs/wexelevate/e1s18_broker.c and
cred_dialog.c, tools/e1s6/e1s6-lab.py and its D-Bus policy (tools/e1s6/org.wineexplorer.E1S6Lab.conf),
tools/e1s7/pam-worker.c, protocol.h and supervisor.py; docs/STOCK_CHANGES.md. Negative tests per
gate, at least: foreign or spoofed attach without victim change; duplicate attach; missing, extra or
truncated ancillary data; pin mismatch on a later message; normally-started-then-suspended D; second
mapped consent image; token replacement; session or route mismatch; D, W, B, V death at each state;
deadline at each state; refused RESULT followed by a Confirm attempt; Confirm before RESULT; replayed
Confirm; overlength password; clipboard and dump checks. Both ABIs built.
