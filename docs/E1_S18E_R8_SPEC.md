# E1-S18e R8 step 1: attempt tuple, states and messages (frozen spec v2, not implemented)

v2 answers docs/E1_S18E_R8_SPEC_REVIEW.md (C1-C8). Design: docs/E1_S18E_R8_DESIGN.md and
docs/E1_S18E_R8_REVIEW.md. Participants: broker B (wexelevate, SYSTEM), dialog D (consent.exe started
by B), server S (wineserver), verifier V (root service, tools/e1s6), worker W (S7 PAM worker, run by
V's supervisor). The standard-user chain stays disabled; normal builds keep ERROR_NOT_SUPPORTED, and
the shipping R13 behaviour is separate and unchanged.

## 1. Values, clock and identifiers

* Time: u64 nanoseconds of the host `CLOCK_MONOTONIC` of the current boot (WO-16). Expired when
  `now >= deadline`. Clock failure, overflow or a zero value denies.
* Random values (nonce, channel_id, challenge) come from the CSPRNG (WO-5); failure or all-zero denies.
* All ids are nonzero. `attempt` starts at 1, at most 3 per request (`E1S1` attempts), never reused;
  a new attempt revokes the previous one everywhere first and never extends the admission deadline.

## 2. Admission record and sealed attempt tuple

B creates the **admission record** once per request: `request_id:u64`, `binding_digest:32`,
`request_digest:32`, `deadline:u64`, the six generations (`root, server, broker, verifier, map,
policy`, u64 each, all nonzero), `caller_session:u32`, `desktop_route:16` (S's validated route).

Per attempt B adds `attempt:u32`, `nonce:32`, `channel_id:16`, and, after S's endorsement (section 3),
`dialog_id:16`. The result is the **sealed tuple**; it is sent to S in the endorsement request, to V
in START (section 5), and to D in INIT. Every receiver compares every field it holds; a mismatch is a
protocol error, except the unauthorized-sender rule for attach (section 6).

## 3. Server endorsement of D

* **Creator.** `new_process` records `current->process` (the requesting process, not the designated
  parent) as `creator` in the new process, as a retained reference cleared on creator exit.
* **Kernel identity from D's own loader.** For a process whose creator is the verified broker, S
  enables `SO_PASSCRED` and `SO_PASSPIDFD` on its end of that process's message socket (`msg_fd`)
  before the process can run. The first `receive_fd` from it (sent by D's loader during first-thread
  startup, before any PE code) must carry exactly one `SCM_CREDENTIALS` and one `SCM_PIDFD`; S keeps
  that pid, uid and pidfd as D's **native pin** and rejects the process (kill) if they are missing,
  duplicated, truncated, if the pid equals the creator's, or if a later message on that socket
  carries a different pid. The socketpair's creation-time `SO_PEERCRED` is never used for this.
* **Endorsement request.** B calls a new private endorsement operation with D's process handle (typed
  lookup in B's own handle table, PROCESS_QUERY_LIMITED_INFORMATION) and the sealed tuple without
  `dialog_id`. S accepts only if: the caller is the verified broker (R13 `sysrun_process_is_broker`)
  with no thread token; D's `creator` is that broker; D has never been resumed (an irreversible
  "resumed once" flag set in `resume_thread`); D's first executable image is machine-root
  `system32/consent.exe` or `syswow64/consent.exe` (device and inode, decided once, R13 semantics);
  D's primary token is SYSTEM; D's session equals the tuple's `caller_session`; the native pin exists
  and is alive. S retains D's process object, its primary token object with token id and modified
  id, the native pin and the sealed tuple, assigns `dialog_id`, and returns it to B.
* **Access.** While endorsed, D's process and thread objects and its pipe ends accept only B and S
  (protected descriptors, as the system role does for SYSTEM processes).
* **S to V.** S sends V, on the existing endorsed control socket, the sealed tuple plus D's pin pid and
  uid. V cannot inspect Wine objects, so every later step V performs is gated by S rechecking D's
  object, token object and ids, session, route and pin, and the generations, atomically at that step
  (B asks S before READY and before Confirm is accepted; V asks S on attach).

## 4. Encoding

Little-endian, no padding, exact length, lengths checked before allocation. Common header,
**76 bytes**: `magic:4 = "E1R8"`, `version:u16 = 1`, `type:u16`, `length:u32` (whole message),
`request_id:u64`, `attempt:u32`, `reserved:u32 = 0`, `nonce:32`, `channel_id:16`.
Strings: `u32 byte length` + bytes, no NUL. Account names: ASCII, 1..63 bytes, the S4 name grammar
(`valid_name` in wine/server/accounts.c). Display strings: UTF-16LE, even length, at most 520 bytes,
no control characters, validated by D before display. Unknown type, wrong length, nonzero reserved,
an invalid string, a duplicate, missing, late or out-of-order frame, or trailing bytes is a protocol
error. S1 answers are carried unchanged as complete S1 record kind 2 (16-byte S1 header, 156 bytes plus
payload, `required_caps` within the negotiated mask) inside an ANSWER frame.

## 5. Messages

**B↔D** (two anonymous pipes, D's ends in the explicit handle list, B's ends non-inheritable, all
redundant copies closed after creation; B's writes and reads run in cancellable workers drained
concurrently with the waits of section 7):

| Type | Dir | Body after the header | Rule |
| --- | --- | --- | --- |
| 1 INIT | B→D | sealed tuple fields not in the header (binding_digest, request_digest, deadline, dialog_id), requester:string, image:string, publisher:string | queued before resume, written by the worker after resume; display data is B's frozen validated snapshot only |
| 2 ACK | D→B | binding_digest:32, request_digest:32, dialog_id:16 | D shows nothing before sending it |
| 3 SELECT_OK | B→D | (none) | attach done (section 6); D enables account selection only now |
| 4 SELECT | D→B | account:string | once per attempt |
| 5 READY | B→D | (none) | V reported worker ready and both secret ends delegated (section 6); D enables the password field only now |
| 6 RESULT | B→D | status:u32 (1 authenticated, 2 refused), account:string (empty when refused), challenge:32 (zero when refused) | refusal has generic timing and no account data |
| 7 ANSWER | D→B | one complete S1 kind-2 record | answer_kind 2 (Confirm) only once, only after RESULT 1, bound to the attempt with `nonce` = the RESULT challenge; answer_kind 3 (Cancel) any time; answer_kind 1 is forbidden here |
| 8 DONE | B→D | status:u32 (1 completed, 2 cancelled) | after Stage 2 completion or cancellation, never after Confirm alone; D exits only after it |

**B↔V** (the existing S6 data channel of the one request-owned transport, no new OPEN):

| Message | Body | Rule |
| --- | --- | --- |
| START | sealed tuple, account:string, account_key:16 from S4 | after SELECT; V starts W for that account (decoy for unknown accounts, as today) |
| WORKER_READY | worker pid:u32, uid:u32 | W started, pinned and parked; secret pair created |
| DELEGATED | (none) | D received its end (attach reply ACK) and V closed both of its own copies |
| RESULT | status:u32, account_key:16, canonical account:string, evidence_ref:16 | after PAM and account management; refusal carries zeros and no evidence |

## 6. Attach and the secret socket

V creates the SOCK_SEQPACKET pair after START, sets `SO_PASSCRED` and `SO_PASSPIDFD` on both ends
before either peer can send, hands one end to W (supervisor transfer list, CLOEXEC), and keeps the
other for D. Python never watches or reads either secret end.

`DialogAttach(request_id, attempt, nonce, channel_id, dialog_id)` from D's wexhost on D's own
system-bus connection. V checks the bus sender's Unix pid and uid against D's pin from S, asks S to
recheck D (section 3), and requires V's attempt state `ENDORSED`. A non-matching sender is refused and
the attempt is untouched. A match atomically moves V to `ATTACH_RESERVED`, binds the unique bus name
(watched; its loss cancels), and returns D's end plus W's pid, uid and pin identity, signed with V's
registration generation. D replies `ATTACH_ACK` on the secret socket; V then closes its copies and
moves to `DELEGATED`. A duplicate attach from the real D, a failed or lost transfer, or a missing ACK
cancels, with no reassignment.

On the secret socket: W sends its one fixed password prompt (S7 framing extended with `channel_id`,
both digests and `dialog_id`; `round` 1, `index` 0, echo off, the fixed text). D sends one S1 kind-2
record with `answer_kind` 1 and the UTF-8 password (checked UTF-16 to UTF-8 conversion, at most 4096
bytes, no NUL; longer input is refused in the UI, never truncated). Each side checks exactly one
`SCM_CREDENTIALS` and one `SCM_PIDFD` per message against the other's pinned identity and rejects
extra or truncated ancillary data. D keeps its end open and silent until DONE (EOF before then is a
cancellation to W).

## 7. States

B: `SPAWNED` (D suspended) → `ENDORSED` → `INIT_SENT` (resumed, INIT written) → `ACKED` →
`ATTACHED` (V reported attach) → `SELECTING` → `STARTED` (START sent) → `READY_SENT` →
`PAM_RUNNING` → `RESULT_OK` or `RESULT_REFUSED` → (`RESULT_OK` only) `CONFIRMED` → `STAGE2` (redeem
and continue, R10) → `COMPLETED`, and from every non-terminal state → `CANCELLED`.
`RESULT_REFUSED` goes to `CANCELLED` or to a fresh attempt (new tuple), never to `CONFIRMED`.

V: `ENDORSED` → `ATTACH_RESERVED` → `DELEGATED` → `WORKER_READY` → `PAM` → `RESULT_SENT` →
`RETIRED`, and → `CANCELLED`.

Cancellation wins: every transition is a compare-and-set under the owner's lock, cancellation checked
first. Triggers: deadline, D death, D bus-name loss, broken B↔D pipe, caller death or caller
token/version change, route change, generation change, broker stop, image lease break, verifier or
worker death, channel replacement, partial transfer failure. Owners: B owns D's process handle, both
pipes and their I/O workers; S owns D's retained objects and native pin; V owns the endorsement record,
the bus-name watch and the secret ends until delegation; the supervisor owns W. Cleanup removes
watches before freeing, drains and joins I/O workers, sends DONE 2 if the pipe is open, and
terminates and reaps only owned processes, also when DONE cannot be written.

## 8. Kept from today

One fixed prompt, decoys for unknown accounts with fixed timing, the fixed PAM service,
`PAM_DISALLOW_NULL_AUTHTOK`, `pam_acct_mgmt`, refusal of a changed `PAM_USER` (no relabel). No
arbitrary PAM prompts or MFA.

## 9. Acceptance (per gate)

D: `PR_SET_DUMPABLE` 0 set and checked before the password field is enabled; no clipboard copy, cut,
undo or history on the password field; every owned copy (edit control, UTF-16, UTF-8, transport
buffers) wiped on Confirm, Cancel, window close, error and transport failure; no prompt or secret ever
logged. Each gate names its negative tests; both ABIs are built; protocol artifacts are regenerated
when a server request changes; every stock-Wine change is listed in docs/STOCK_CHANGES.md.
