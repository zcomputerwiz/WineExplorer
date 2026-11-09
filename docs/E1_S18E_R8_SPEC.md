# E1-S18e R8 step 1: attempt tuple, states and messages (frozen spec v4, not implemented)

v4 adopts docs/E1_S18E_R8_SPEC_REVIEW.md "v3 check". Design: docs/E1_S18E_R8_DESIGN.md,
docs/E1_S18E_R8_REVIEW.md. Participants: broker B (wexelevate, SYSTEM), dialog D (consent.exe started
by B), server S (wineserver), verifier V (root service, tools/e1s6), worker W (S7 PAM worker, run by
V's supervisor). The standard-user chain stays disabled; normal builds keep ERROR_NOT_SUPPORTED; the
shipping R13 behaviour is separate and unchanged.

## 1. Values

* Time: u64 ns of host `CLOCK_MONOTONIC`, current boot (WO-16). Expired when `now >= deadline`;
  clock failure, overflow or zero denies.
* Random values (nonce, channel_id, challenge): CSPRNG (WO-5); failure or all-zero denies.
* Ids nonzero. `attempt` 1..3 per request, never reused. Each attempt uses a fresh D (new process,
  new endorsement), revokes the previous attempt everywhere first and never extends the deadline.
* Pins are Linux pidfds. Two pidfds identify the same process when both are alive and their
  `/proc/self/fdinfo` `Pid:` values are equal. Every pidfd has one named owner and is closed on every
  exit path; received pidfds are CLOEXEC.
* Limits: one standard request in flight (WO-15, whole lifetime; the one S6 transport is kept from
  registration to release, no OPEN/CLOSE rotation); V keeps at most 8 attempt records; per attempt at
  most 4 refused attach senders are tracked, then further calls from them are ignored. A refused
  sender never changes the attempt.

## 2. Tuples

**Pre-endorsement tuple** (B, per attempt), in order: `request_id:u64, attempt:u32, nonce:32,
channel_id:16, binding_digest:32, request_digest:32, deadline:u64, root_gen:u64, server_gen:u64,
broker_gen:u64, verifier_gen:u64, map_gen:u64, policy_gen:u64, caller_session:u32,
desktop_route:16, desktop_mode:u32` (1 secure desktop, 2 explicitly configured insecure desktop),
204 bytes. **Sealed tuple** = pre-endorsement tuple + `dialog_id:16`, 220 bytes, assigned by S.
The **tuple tail** is the sealed tuple without the 60 bytes that the common header already carries
(`request_id, attempt, nonce, channel_id`): `binding_digest` through `dialog_id`, 160 bytes.
S, V, W, B and D each keep the sealed tuple and compare all of it wherever it appears.

## 3. Server endorsement of D

* **Creator and protection from creation.** `new_process` records `current->process` as `creator`
  (independent of the designated parent; retained reference released on exit). If the creator is the
  verified broker (R13), the new process is protected from creation until retirement: S refuses, to
  every process other than B and the process itself, opening it or its threads, duplicating handles to
  or from it, and using existing handles to it or its threads for anything but SYNCHRONIZE and
  PROCESS_QUERY_LIMITED_INFORMATION. D's inherited pipe ends keep exactly the rights B gave them.
* **Kernel pin from D's loader.** In `new_process`, before returning success (so before B spawns D),
  for such a process S sets and checks `SO_PASSCRED` and `SO_PASSPIDFD` on its end of the process
  socket; failure denies the creation. D's first sends on that socket are its loader's `reply_pipe`
  and `wait_fd` transfers (wine/dlls/ntdll/unix/server.c init_thread_pipe), before any PE code.
  `receive_fd` (wine/server/request.c) for such processes requires exactly the `send_fd` size, one
  SCM_RIGHTS with one fd, one correctly sized SCM_CREDENTIALS and one SCM_PIDFD, no unknown, duplicate
  or truncated ancillary data, valid thread linkage. The first valid message's pid, uid and pidfd
  become D's native pin; later messages must carry the same pid and uid and a pidfd for the same live
  process, else the process is killed. The thread's self-reported `unix_pid` is compared with the pin,
  never used as proof. Rejected or surplus fds and pidfds are closed.
* **Created suspended.** Endorsement requires the original first thread's CREATE_SUSPENDED proof, that
  it is still suspended, and no other thread, entry or resume (the S14b irreversible pattern,
  thread.c `e1s_profile_unstarted`).
* **Endorsement request** (new private server request, B → S): D's process handle, looked up as a
  process handle in B's own table with PROCESS_QUERY_LIMITED_INFORMATION, and the pre-endorsement
  tuple. S accepts only if: the caller is the verified broker with no thread token and B's S6
  registration is current; D's creator is B; the created-suspended proof holds; D's first executable
  image is machine-root `system32/consent.exe` or `syswow64/consent.exe` (device and inode, decided
  once); D's primary token is SYSTEM; D's session equals `caller_session`; D's desktop is the object
  behind `desktop_route`; the native pin exists and is alive; the caller's retained S3 objects and
  versions match. S retains D's process object, its primary token object with token id and modified
  id, the pin and the sealed tuple, assigns `dialog_id`, and returns it.
* **Rechecks.** Each authorizing step asks S to recheck atomically: D's process and token objects and
  ids, session, desktop route, pin, the caller's retained S3 objects and versions, the generations.
  Steps: attach (V asks), READY (B), RESULT publication (B), Confirm acceptance (B), redemption (B).
* **S → V:** ENDORSED (section 6) on the existing control socket, with D's pidfd (SCM_RIGHTS, owned by
  V until release).

## 4. Encoding

Little-endian, no padding, exact length, all lengths checked before allocation. Common header (76
bytes): `magic:4 "E1R8", version:u16 = 1, type:u16, length:u32, request_id:u64, attempt:u32,
reserved:u32 = 0, nonce:32, channel_id:16`. Strings: `u32 byte length` + bytes, no NUL. Account
names: ASCII, 1..63 bytes, S4 grammar (`valid_name`, wine/server/accounts.c). Display strings:
UTF-16LE, even length, 0..520 bytes, valid surrogates, no NUL or control characters. Verifier owner:
ASCII D-Bus unique name `:<digits>.<digits>`, 4..255 bytes. Unknown type, unknown enum value, wrong
length, nonzero reserved, invalid string, duplicate, missing, late or out-of-order frame, or trailing
bytes ends the attempt. Fields appear in the listed order with no gaps.

## 5. B↔D messages (two anonymous pipes)

D's read end and write end are in the explicit handle list (WO-7); B's ends are non-inheritable; all
redundant copies are closed right after creation. B's writes and reads run in cancellable I/O workers.

| Type | Name | Dir | Body after the header | Total bytes |
| --- | --- | --- | --- | --- |
| 1 | INIT | B→D | tuple tail (160), verifier_owner:str, requester:str, image:str, publisher:str | 256..2067 |
| 2 | ACK | D→B | binding_digest:32, request_digest:32, dialog_id:16 | 156 |
| 3 | SELECT_OK | B→D | (none): account selection allowed, no secret input | 76 |
| 4 | SELECT | D→B | account:str | 81..143 |
| 9 | ATTACH_OK | B→D | (none): sent after V's WORKER_READY; D may call DialogAttach only after it | 76 |
| 5 | READY | B→D | (none): sent after DELEGATED and an S recheck | 76 |
| 6 | RESULT | B→D | status:u32 (1 authenticated, 2 refused), account:str (empty if refused), challenge:32 (zero if refused) | 116 refused, 117..179 authenticated |
| 7 | ANSWER | D→B | challenge:32, one unchanged S1 kind-2 record | 264 for Confirm/Cancel |
| 8 | DONE | B→D | status:u32 (1 completed, 2 cancelled) | 80 |

INIT's verifier owner, requester, image and publisher come only from B's frozen, validated snapshot
(B learned the verifier owner from its S6 registration). D shows the requester, image and publisher,
the S1 Wine-only storage and host-access limits, and, for `desktop_mode` 2, that the desktop is not
secure. D shows nothing before sending ACK.

ANSWER: the S1 record's present fields (`request_id, attempt, nonce, binding_digest,
request_digest, channel_id`) equal the sealed tuple; its `nonce` is the attempt nonce. Confirm:
`answer_kind` 2, `round` 2, `message_index` 0, empty payload, challenge = RESULT's challenge;
accepted once, only after RESULT 1, only while the retained final account key, canonical name,
evidence reference and challenge are unchanged. Cancel: `answer_kind` 3, `round` 0, `message_index`
0, empty payload, challenge zero; accepted in any non-terminal state. `answer_kind` 1 is refused on
this pipe. `required_caps` must be within the negotiated mask.

## 6. Control messages (same header)

| Type | Name | Path | Body after the header | Total |
| --- | --- | --- | --- | --- |
| 16 | ENDORSED | S→V control socket | tuple tail; D's pidfd as SCM_RIGHTS | 236 |
| 17 | START | B→V S6 data | account:str, account_key:16 (zero for an unknown name: decoy) | 97..159 |
| 18 | WORKER_READY | V→B | (none) | 76 |
| 19 | DELEGATED | V→B | (none) | 76 |
| 20 | RESULT | V→B | status:u32 (1 authenticated, 2 refused), account_key:16, canonical account:str, evidence_ref:16; refused: all zero/empty | 116 refused, 117..179 authenticated |
| 21 | CANCEL | B↔V | reason:u32 = 1 | 80 |
| 22 | W_ACK | W→V supervisor | (none) | 76 |
| 23 | ATTACH_ACK | D→W secret socket | (none) | 76 |
| 24 | PROMPT_RELEASE | V→W supervisor | (none) | 76 |
| 25 | RELEASE | B→V S6 data | outcome:u32 (1 completed, 2 cancelled) | 80 |
| 26 | RELEASED | V→B | (none) | 76 |
| 27 | W_SETUP | supervisor→W at spawn | tuple tail; D's pid:u32, uid:u32; D's pidfd as SCM_RIGHTS | 244 |

A decoy (zero account_key) can never produce RESULT 1 or evidence.

## 7. Attach, secret socket and readiness

1. After START, V's supervisor creates the SOCK_SEQPACKET pair, sets and checks `SO_PASSCRED` and
   `SO_PASSPIDFD` on both ends, spawns W with one end (transfer list, CLOEXEC) and sends W_SETUP
   (sealed tuple, D's pid, uid and pidfd); the supervisor owns W's pidfd. W is parked in bootstrap:
   it sends nothing on the secret socket and suppresses only premature secret-data checks; deadline,
   death and stop still cancel. V reports WORKER_READY; B sends ATTACH_OK.
2. D's wexhost calls `DialogAttach(t request_id, u attempt, ay nonce[32], ay channel_id[16],
   ay dialog_id[16])` on D's own system-bus connection to the verifier owner from INIT. V requires
   the bus sender's pid and uid to identify the same live process as D's pidfd from ENDORSED, asks S
   to recheck D, and requires V's state `WORKER_READY`; otherwise the call is refused (limits in
   section 1) and the attempt is untouched. A valid call atomically moves V to `ATTACH_RESERVED`,
   binds D's unique bus name (watched; loss cancels) and replies `(h secret_end, h w_pidfd,
   u w_pid, u w_uid, ay tuple_tail[160])`. D checks the reply's sender is the verifier owner from
   INIT and the tuple tail equals its own; D's wexhost owns both received fds until DONE.
3. D sends ATTACH_ACK on the secret socket. W checks the sender's pidfd identifies D's pin from
   W_SETUP, ends bootstrap waiting and sends W_ACK to V. V closes both of its secret copies (Python
   never watches or receives on either secret end), moves to `DELEGATED`, sends DELEGATED to B and
   PROMPT_RELEASE to W. A duplicate valid attach, a failed or lost transfer, or a missing ACK before
   the deadline cancels; nothing is reassigned.
4. B asks S to recheck D and sends READY. W sends its prompt, S7 version 2 (R8 only; version 1
   stays today's lab format): the 72-byte S7 header (tools/e1s7/pam-worker.c `header()`: offset 0
   magic `"E1S7PAM\0"`, 8 version:u16 = 2, 10 type:u16 = 2 PROMPT, 12 length:u32 = 178,
   16 request_id:u64, 24 attempt:u32, 28 round:u32 = 1, 32 index:u32 = 0, 36 style:u32 = 1
   (PAM_PROMPT_ECHO_OFF), 40 nonce:32) + `channel_id:16, binding_digest:32, request_digest:32,
   dialog_id:16` from the sealed tuple + ASCII `Password: ` (10 bytes, no NUL); exactly 178 bytes. D enables the password field only after
   both READY and a prompt whose sender pidfd identifies W's pin.
5. D sends one S1 kind-2 record: `answer_kind` 1, `round` 1, `message_index` 0, UTF-8 password
   (checked conversion from UTF-16, at most 4096 bytes, no NUL; longer input is refused in the UI,
   never truncated). Each side checks exactly one SCM_CREDENTIALS and one SCM_PIDFD per message
   against the other's pin and rejects extra, unknown or truncated ancillary data, closing any
   received fd it does not expect. D keeps the socket open and silent until DONE.

## 8. Final account

Before B publishes RESULT 1 and again before accepting Confirm, B resolves the account freshly
through S4 and requires: eligible now, the key equal to the selected account's key, and the
canonical name equal to the name in V's RESULT and in B's RESULT to D. The one-use challenge is bound
in B's state to that key, name and evidence reference.

## 9. States

B: `SPAWNED` → (S endorsed) `ENDORSED` → (D resumed, INIT written) `INIT_SENT` → (valid ACK)
`ACKED` → (SELECT_OK written) `SELECTING` → (valid SELECT) `SELECTED` → (START sent) `STARTED` →
(WORKER_READY; ATTACH_OK written) `ATTACH_OPEN` → (DELEGATED) `DELEGATED` → (READY written after S
recheck) `READY_SENT` → (V RESULT) `RESULT_OK` or `RESULT_REFUSED` → (`RESULT_OK` only, valid
Confirm after S recheck and section 8) `CONFIRMED` → `STAGE2_PENDING` (R10: redeem once, issue,
load, launch prepared, not committed) → `COMMITTED` (guarded resume done) → `COMPLETED`. Any
non-terminal state before `COMMITTED` → `CANCELLED`. `RESULT_REFUSED` → `CANCELLED` or a fresh
attempt. After `COMMITTED`, failures follow S1's post-commit rules (no relabel to cancelled; a lost
reply needs termination first). On `COMPLETED` or `CANCELLED`, B writes DONE (bounded), sends
RELEASE with the outcome, and waits (bounded) for RELEASED.

V: `ENDORSED` → `STARTED` → `WORKER_READY` → `ATTACH_RESERVED` → `DELEGATED` → `PAM` →
`RESULT_RETAINED` (evidence kept; consumed once by redemption) → (RELEASE) `RETIRED`; any state
before RELEASE → `CANCELLED` on a trigger, then waits for RELEASE or the deadline. Endorsement, pins,
the bus-name watch and the parked W are kept until RELEASE (or cancellation); redemption alone does
not retire them. On RELEASE or cancellation V releases and reaps W, closes its pidfds, removes the
watch, sends RELEASED, and S drops D's protection and retained objects when B closes D's handle and D
exits.

Cancellation wins: every transition is a compare-and-set under the owner's lock with cancellation
checked first; late success cannot reverse it. Triggers: deadline, D death, D bus-name loss, broken
pipe, caller death or caller token/version change, desktop route change, generation change, broker
stop, image lease break, verifier or worker death, channel replacement, failed or partial transfer,
failed S recheck. Owners: B owns D's process handle, both pipes and their I/O workers; S owns D's
retained objects, the protection and S's pin; V owns ENDORSED's pidfd, the endorsement record and the
bus-name watch, and the secret ends until delegation; the supervisor owns W and W's pidfd; D's wexhost
owns the received secret end and W's pidfd; W owns its end and D's pidfd from W_SETUP. Cleanup removes
watches before freeing, writes DONE with a bounded timeout, closes and cancels pipe I/O and joins the
workers without waiting on a dead reader, and terminates and reaps only owned processes. Successful
release frees the same resources in the same order.

## 10. Kept from today

One fixed prompt, decoys for unknown accounts with fixed timing, the fixed PAM service,
`PAM_DISALLOW_NULL_AUTHTOK`, `pam_acct_mgmt`, refusal of a changed `PAM_USER`. No arbitrary PAM
prompts or MFA.

## 11. Acceptance, files and tests

D: `PR_SET_DUMPABLE` 0 set and checked before the password field is enabled; no clipboard copy or
cut, no undo and no history on the password field; every owned copy (edit control, UTF-16, UTF-8,
transport buffers) wiped on Confirm, Cancel, window close, error and transport failure; no prompt or
secret logged.

Files: wine/server/process.c, request.c, thread.c, sysrun.c, named_pipe.c, protocol.def (regenerated
artifacts for the endorsement request); src/dlls/wexhost/e1s6.c, wexhost.c, wexhost.spec,
wexhost_unix.h, unixlib.c (DialogAttach, both Unix call tables); src/programs/consent/consent.c;
src/programs/wexelevate/e1s18_broker.c and cred_dialog.c; new codecs
src/programs/wexelevate/e1r8_codec.h (PE and native) and tools/e1s6/e1r8.py; tools/e1s6/e1s6-lab.py and
org.wineexplorer.E1S6Lab.conf; tools/e1s7/pam-worker.c, protocol.h, supervisor.py; tests
src/programs/wexelevate/test_broker_e1s18_main.c, tools/e1s6/test-e1r8.py (new), tools/e1s6/
test-evidence.py, tools/e1s7/test-client.py, scripts/test_sysrun_server.sh; docs/STOCK_CHANGES.md.

Codec vectors (step 2): every message type at minimum and maximum length; one byte short and one
byte long; wrong magic, version, type, reserved; unknown enum values; strings with odd length,
overlength, NUL, control characters, unpaired surrogates, non-ASCII account names; S1 records with
wrong header, caps outside the mask, payload on Confirm/Cancel, `answer_kind` 1 on the broker pipe,
wrong round or index; replayed and out-of-order frames; refused RESULT with nonzero fields.

Pipe tests (step 4): oversized and partial INIT, a blocked writer, a broken pipe, cancellation at
each wait, an unrelated inherited handle, and redundant handle closure.

Gate tests (steps 3, 5, 8, 9): foreign or spoofed attach without victim change; attach before
ATTACH_OK; duplicate attach; missing, extra or truncated ancillary data; pin mismatch on a later
message; normally started then suspended D; second mapped consent image; token replacement; session
or route mismatch; access to D from another SYSTEM process; D, W, B, V death in each state; deadline
in each state; refused RESULT then Confirm; Confirm before RESULT; replayed Confirm; decoy never
succeeds; overlength password; clipboard and dump checks. Both ABIs built.
