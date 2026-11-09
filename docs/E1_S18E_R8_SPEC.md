# E1-S18e R8 step 1: attempt tuple, states and messages (frozen spec, not implemented)

Design for R8 after review (docs/E1_S18E_R8_DESIGN.md, docs/E1_S18E_R8_REVIEW.md). Later steps
implement exactly this; changes to it need a new review. Participants: broker B (wexelevate,
SYSTEM), dialog D (consent.exe started by B), server S (wineserver), verifier V (root service,
tools/e1s6), worker W (S7 PAM worker supervised by V). The standard-user chain stays disabled.

## 1. The attempt tuple

One attempt is one dialog, one PAM run and one Confirm. It is created by B after admission and
never changes; a new attempt gets a new tuple and invalidates the old one everywhere.

| Field | Size | Source |
| --- | --- | --- |
| request_id | u64 | B, at admission (collision-checked, nonzero) |
| attempt | u32 | B, 1 for the first attempt, +1 per retry, never reused |
| nonce | 32 | B, CSPRNG per attempt (WO-5) |
| binding_digest, request_digest | 32 each | B, S1 encoder (WO-14) |
| channel_id | 16 | B, CSPRNG per attempt (WO-5) |
| deadline | u64 | B, admission deadline on CLOCK_MONOTONIC (WO-16), never extended |
| generations | 6 × u64 | root, server, broker, verifier, map, policy, from the authorities |
| session | u32 | S, the caller's validated session |
| desktop_route | 16 | S, the validated secure-desktop route |
| dialog_id | 16 | S, opaque id of the endorsed D process object (section 3) |

B, V and W each keep their own copy; every message below carries `request_id`, `attempt`,
`nonce` and `channel_id`, and the receiver compares all four (and the digests where present) with
its copy. A mismatch is a protocol error that ends the attempt (section 5), except the
unauthorized-sender case in section 4.

## 2. Encoding

All messages: little-endian, no padding, exact length. Header `magic:4 = "E1R8"`,
`version:u16 = 1`, `type:u16`, `length:u32` (whole message), then `request_id:u64`,
`attempt:u32`, `reserved:u32 = 0`, `nonce:32`, `channel_id:16` (common header, 80 bytes).
Windows strings are `u32 byte length` + UTF-16LE, no NUL, length even and at most the stated
limit. Unknown type, wrong length, nonzero reserved, an invalid string or any trailing byte is a
protocol error. Passwords appear only in the S1 kind-2 answer on the secret socket (section 4).

## 3. Server endorsement of the dialog

B creates D suspended and calls a new endorsement operation with D's process handle. S accepts
only if all hold, and retains them for the attempt:

* the caller is the verified broker (R13 `sysrun_process_is_broker`), not impersonating;
* the handle refers to a process that B itself created (S records the creating process in
  `new_process`; designated parent does not count) and that has not been resumed;
* D's first executable image is the machine-root `system32/consent.exe` or
  `syswow64/consent.exe` (device and inode, decided once, R13 semantics);
* D's primary token is SYSTEM; S keeps D's process object, its primary token object and the
  token's id and modified id;
* D is in B's session and on the validated desktop route;
* a kernel pin for D taken from D's own server connection (`SO_PEERPIDFD` on the request socket
  S holds for that process), not from a client-reported Unix pid.

S then gives V the endorsement over the existing control socket: the attempt's `request_id`,
`attempt`, `nonce`, `channel_id`, deadline, the native pid of D from that pin, and `dialog_id`.
At every later use (attach, each answer, Confirm) S or V rechecks that D's object, token ids,
session and pin are unchanged and alive.

## 4. Messages

Broker channel: two anonymous pipes created by B, D's ends passed by the explicit handle list
(WO-7), B's ends non-inheritable. B→D and D→B messages:

| Type | Direction | Body after the common header | When |
| --- | --- | --- | --- |
| 1 INIT | B→D | binding_digest:32, request_digest:32, deadline:u64, requester:string(≤ 520 bytes), image:string(≤ 520), publisher:string(≤ 520) | queued by a cancellable writer, written after D is resumed |
| 2 ACK | D→B | binding_digest:32, request_digest:32 | D read INIT; D shows nothing before this |
| 3 READY | B→D | (none) | V reported attach and worker ready (below); D enables input only now |
| 4 SELECT | D→B | account:string(≤ 64 bytes, ASCII account name) | user chose the account; B forwards it to V in the PAM start packet |
| 5 RESULT | B→D | status:u32 (1 authenticated, 2 refused), account:string(≤ 64) | after V's result; account is the canonical S4 name |
| 6 DONE | B→D | status:u32 (1 completed, 2 cancelled) | terminal; D exits only after this |

D's Confirm and Cancel are S1 record kind 2 (docs/E1_S1_CONTRACTS.md record 2) on the D→B pipe,
`answer_kind` 2 (Confirm, sent only after RESULT authenticated) or 3 (Cancel, any time), empty
payload, all S1 fields bound to the tuple, `round` 1, `message_index` 0.

Secret socket (D↔W, delegated by V, section 3 of the review): W sends its single fixed password
prompt (the existing S7 framing, extended with `channel_id`, both digests and `dialog_id`); D sends
one S1 record kind 2 with `answer_kind` 1 and the UTF-8 password as payload (at most 4096 bytes, no
NUL, well-formed; longer input is refused in the UI, never truncated). One prompt, one answer, then
EOF. W checks exactly one SCM_CREDENTIALS and pin per message against D's endorsed identity; D
checks W's sender credentials against the worker identity V registered for the attempt.

Attach (D's wexhost → V over D's own system-bus connection): `DialogAttach(request_id, attempt,
channel_id)`. V requires the bus sender's Unix pid and UID to match D's endorsed pin, the pin to be
alive, and the attempt to be in state ENDORSED; it then atomically moves to ATTACHED, binds the
unique bus name, and returns D's end of the secret socket. A sender that does not match is refused
and the attempt is left untouched (no cancellation of the real prompt). A second attach from the
real D, or an attach after the attempt left ENDORSED, ends the attempt.

## 5. States (owned by B; V and W mirror their part)

`SPAWNED` (D suspended) → `ENDORSED` (S accepted section 3) → `INIT_SENT` (D resumed, INIT
written) → `ACKED` → `ATTACHED` (V: attach done and worker ready) → `READY_SENT` → `SELECTED` →
`PAM_RUNNING` → `RESULT_SENT` → `CONFIRMED` → hand-off to Stage 2 (redemption), and from any state
to `CANCELLED`. Every wait is bounded by the attempt deadline and also ends on D death, caller
death, broker stop, image lease break, verifier owner loss or generation change; any of these
moves to `CANCELLED`, which is terminal: B sends DONE 2 if the pipe is open, V closes the secret
socket and W is reaped by its supervisor, and nothing later can turn the attempt back into
success. No wait happens while D is suspended. Exactly one owner per resource: B owns D's process
handle and the pipes; V owns the secret socket ends until delegation and the endorsement record;
the supervisor owns W.

## 6. Limits kept from today

The S7 flow stays as it is: one fixed prompt, decoy runs for unknown accounts, fixed timing,
`PAM_DISALLOW_NULL_AUTHTOK`, `pam_acct_mgmt`, the fixed service name. A changed `PAM_USER` is
refused (no relabel). Arbitrary PAM prompts and MFA are out of scope.

## 7. Open points for review

* The creator record in `new_process` and the `SO_PEERPIDFD` pin on the request socket are new
  server state; are they enough to satisfy "process-bound handshake"?
* INIT carries display strings chosen by B; D must not show anything it received from elsewhere.
