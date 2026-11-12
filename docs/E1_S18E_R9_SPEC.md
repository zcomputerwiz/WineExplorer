<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S18e R9: broker in session 0, agent in the session (spec v3; system tier T6.3)

Decision (2026-11-12): option A of [E1_S18E_R9_DESIGN.md](E1_S18E_R9_DESIGN.md), towards fast user switching.
R9 changes only what this document names; everything else is R8 ([E1_S18E_R8_SPEC.md](E1_S18E_R8_SPEC.md), frozen
v5) with R10 as built (WO-31). The chain stays lab only and disabled in normal builds, as R8.

v2 adopts the review of v1 (Codex, 2026-11-12; section 10): the session's server no longer holds session 0's
authorization; the verifier does, as a live single-use grant from B0, and the receipt keeps R10's route.

## 1. Threat model

Trusted as in R8: the session's server S (launcher-started, not dumpable; it creates every token of the session
and nothing in R9 can restrain a subverted S), the session's system role (R8's broker B ran there; R9's agent A
does), V and W (root, outside Wine), session 0's server S0 and its LocalSystem services. R9 adds no protection
against a subverted S or a subverted system role of a session; R8 had none either.

What R9 adds: the policy decision, the attempt count across sessions and the grant to redeem are made in
session 0 (B0) and enforced by V; one broker serves every session (fast user switching). A subverted A can
still deny service, and as the session's system role it could draw on the secure desktop; it cannot obtain a
token without V's RESULT for the password of an eligible account and B0's grant for that attempt (section 6).

## 2. Parties

| Party | Where | Role |
|---|---|---|
| B0, the broker | session 0: `wexelevate` as a LocalSystem service of session 0 (T5) | policy, attempt count, the pre-endorsement tuple, every B→D frame and every check on D's frames, the talk with V from START to RELEASE, the final account (R8 section 8), the grant (section 6) |
| A, the agent | the user's session: `wexelevate` as the session's service (system role, as R8's B) | the program's request, its own S6 channel to V (for its channel id and REDEEM only), D's creation, pipes, endorsement and rechecks with S, the byte-exact relay of frames, the redemption, issue and launch |
| S | the session's server | as R8 and R10 (endorsement, rechecks, receipt check at issue) |
| S0 | session 0's server | the bridge to B0 (section 3); B0's S6 endorsement (section 6) |
| D, V, W | | as R8, with V's changes in section 6 |

Rule: B0 decides, A acts. A never builds a B→D frame, never interprets one beyond section 4's length check,
and sends V nothing but REDEEM (and RELEASE of its own channel). B0 never touches an object of S.

## 3. The A ↔ B0 channel

* **Name.** B0 serves `\pipe\wexelevate.r9` in session 0 (message mode). Session 0's bridge allows the name
  (T5.2 list: `svcctl`, `wexelevate.r9`). A opens `\pipe\WexSession0\wexelevate.r9`, one connection per request,
  held until END.
* **Descriptor**: SYSTEM full access, nothing else. Session 0 checks A's described token against it; the token
  must be bound to S's peer account or a service account (T5 review), so only a session's SYSTEM connects.
* **From S0, not from A.** For a bridged client S0 records the session id of the bridge's peer server (its
  session link, T6.2; a peer without a current session link is refused for `wexelevate.r9`). B0 reads it with
  `GetNamedPipeClientSessionId`; it becomes `caller_session`. B0 refuses a client whose impersonation token is
  not LocalSystem.
* **Session ids are not reclaimed across accounts.** S0 keeps, for every session id it gives out, the uid of the
  server it gave it to, and appends `id uid` to `session0.owners` in its directory before it answers (written
  with `O_APPEND`, read at start; at most the last 65536 entries are kept). A server that asks for an id again
  after its link closed (T6.2) gets it only if its uid is the recorded one. An id with no record (the file was
  lost) is not given back: that server keeps no session link, and R9 refuses its requests (section 3, no link).
  (Change to `session0.c`.)
* **Framing.** Each pipe message is one R8 frame: the 76-byte common header (R8 section 4) and a body; R8's
  encoding rules apply. New types 40..51.
* **Limits.** One request in flight per session, at most 8 sessions with one; a ninth is DECISION deny (reason 3).

## 4. Messages A ↔ B0 (types 40..51)

Header: `request_id` is A's request id (nonzero, never reused by A); `attempt` 0 on REQUEST and on DECISION deny,
otherwise the attempt (1..3); `nonce` and `channel_id` zero on REQUEST and DECISION deny, otherwise the attempt's
(`channel_id` is A's S6 channel, section 6).

| Type | Name | Dir | Body after the header | Total |
| --- | --- | --- | --- | --- |
| 40 | REQUEST | A→B0 | `kind:u32` (1 runas, 2 manifest), `channel_id:16` (A's S6 channel), `binding_digest:32`, `request_digest:32`, `server_gen:u64`, `desktop_route:16`, `secure_desktop:u32` (1 available, 0 not), `requester:str`, `image:str`, `publisher:str` (display strings, R8 section 4) | 200 .. 1760 |
| 41 | DECISION | B0→A | `status:u32` (1 prompt, 2 deny), then for 1: the pre-endorsement tuple (204); for 2: `reason:u32` (1 policy, 2 attempts, 3 busy) | 84 deny, 284 prompt |
| 42 | ENDORSED_LOCAL | A→B0 | `dialog_id:16`, `status:u32` (1 endorsed, 2 refused) | 96 |
| 43 | TO_D | B0→A | one complete R8 B→D frame (types 1, 3, 9, 5, 6, 8) | 152 .. 2143 |
| 44 | FROM_D | A→B0 | one complete R8 D→B frame (types 2, 4, 7) as read from D | 157 .. 340 |
| 45 | RECHECK | B0→A | `step:u32` (1 attach, 2 ready, 3 result, 4 confirm) | 80 |
| 46 | RECHECKED | A→B0 | `step:u32`, `status:u32` (1 ok, 2 refused) | 84 |
| 47 | LAUNCH | B0→A | `account:str` (canonical), `evidence_ref:16` | 97 .. 159 |
| 48 | LAUNCHED | A→B0 | `status:u32` (1 committed, 2 failed before commit, 3 failed after commit), `win32:u32` | 84 |
| 49 | CANCEL | A↔B0 | `reason:u32` (1 user, 2 D gone, 3 deadline, 4 peer gone, 5 protocol, 6 recheck) | 80 |
| 50 | END | B0→A | `outcome:u32` (1 completed, 2 cancelled, 3 denied) | 80 |
| 51 | NEXT_ATTEMPT | B0→A | the next attempt's pre-endorsement tuple (204) | 280 |

Rules:

* **REQUEST.** A registers its S6 channel with V through S before it sends REQUEST (as R8's B registers; V
  learns S's pin there) and fills the rest from S and the program's request, as R8's B does. B0 copies
  `channel_id`, the digests, `server_gen` and `desktop_route` into the tuple; S checks them at endorsement
  (R8 section 3) and V binds the attempt to the channel (section 6). `caller_session` comes from S0 (section 3).
  `secure_desktop` 0 makes B0 use `desktop_mode` 2 only where policy allows the insecure desktop, else deny.
* **DECISION 1 / NEXT_ATTEMPT.** B0 applies the policy (`ConsentPromptBehaviorAdmin`, `ConsentPromptBehaviorUser`,
  `PromptOnSecureDesktop`, `EnableStandardUserElevation`, the lockout policy; session 0's registry) and the
  attempt count (B0's, per request and per account, across sessions). A elevation without a prompt is not part
  of R9.
* **Relay.** A writes each TO_D's embedded frame to D unchanged and in order, and sends each frame read from D
  as FROM_D unchanged. A checks only that the embedded frame's header length equals the embedded bytes and lies
  within R8's bounds for its type; otherwise CANCEL 5. B0 applies every R8 check to the embedded frames.
* **RECHECK.** A asks S to recheck D (R8 section 3) at each step B0 names and returns the result; B0 sends
  ATTACH_OK, READY, RESULT and the Confirm acceptance only after a RECHECKED ok of the matching step, where R8's B
  asks S. (A is trusted for this, section 1.)
* **Retry.** After V's RESULT 2 (refused) B0 sends RESULT 2 to D through TO_D and releases V's attempt (R8
  section 9). For a further attempt (attempt + 1, at most 3, same deadline, fresh nonce) B0 sends NEXT_ATTEMPT
  first and only then DONE 2 through TO_D: on NEXT_ATTEMPT A enters `RETRYING`, in which D's exit is expected;
  it writes the DONE, waits for D's exit (2 s, else terminates it), closes D's pipes and creates the next D for
  the new tuple (`SPAWNING`). Without a further attempt B0 sends DONE 2 and then END 2.
* **LAUNCH.** The decision to elevate is B0's GRANT to V (section 6), sent after B0 accepted Confirm (R8 section
  8 checks). B0 then tells A with LAUNCH. A sends REDEEM to V on its own channel (R10), V sends RECEIPT to S on that
  channel's control socket (R10, unchanged), A asks S to issue and launches, and reports LAUNCHED. B0 then sends
  DONE through TO_D, RELEASE to V and END to A, where R8's B would. A may redeem as soon as V granted, before
  LAUNCH arrives; that changes nothing, since the grant is the decision.
* **Order.** An unexpected type in a state is CANCEL 5. A cancels at the tuple's deadline.

## 5. A's states

`IDLE` → (S6 channel registered, REQUEST sent) `REQUESTED` → (DECISION 1) `SPAWNING` → (D created suspended,
endorsed, resumed; ENDORSED_LOCAL sent) `RELAYING` → (NEXT_ATTEMPT) `RETRYING` → (D gone) `SPAWNING`, or
(LAUNCH) `LAUNCHING` →
(LAUNCHED sent) `ENDING` → (END) `IDLE`. DECISION 2 → `IDLE` after END. A cancels (terminates D, closes its pipes,
sends CANCEL, releases its S6 channel) on D's death outside a retry, a broken pipe to D or B0, the deadline, a
failed recheck, the caller's death. Cancellation wins up to V's `GRANTED` (section 6). V's redemption is the
point of no return: after it the request is committed in R8's sense, a cancellation no longer withdraws it, and
what can still stop the token are S's checks at issue (the receipt's time and generations, R10) and the launch's
own failure (R8's post-commit rules).

## 6. The grant, held by V

* **B0's channel.** B0 registers its S6 channel through S0: S0 endorses B0 as a session's server endorses the
  broker today (`sysrun_e1s6_endorse`: SCM_CREDENTIALS with S0's pid, euid and the system gid; SCM_PIDFD). V
  accepts S0 as an endorser (a wineserver of the configured `session0_user` with the system group, same
  executable inode check) and calls a channel endorsed by S0 a **B0 channel**. With `r9=1` in V's configuration,
  V accepts START, CANCEL, RELEASE and GRANT only on a B0 channel, and REDEEM only on a channel endorsed by a
  session's server.
* **Binding.** START (on B0's channel) names the attempt; the attempt's `channel_id` (header) is A's channel.
  V requires: A's channel exists, is endorsed by a session's server, and is alive; ENDORSED for the attempt comes
  from that server (its pin, R8 section 6 WO-26), so a dialog endorsed by another server is refused; DialogAttach
  as R8. B0's channel loss cancels all its attempts; A's channel loss cancels the attempt.
* **GRANT** (new, type 32, B0→V on B0's channel): header of the attempt, `account_key:16`, `evidence_ref:16`;
  total 108. It is B0's decision to elevate. V accepts it once per attempt, only in `RESULT_RETAINED`, only if
  `account_key` and `evidence_ref` are the attempt's RESULT 1 values; it answers GRANTED (type 33, 76 bytes) and
  moves to `GRANTED`.
* **REDEEM** (R10) is accepted only on the attempt's A channel and only in `GRANTED`; it is single use. A CANCEL
  from B0, B0's channel loss, the deadline or a generation change in `GRANTED` moves V to `CANCELLED` atomically
  with respect to REDEEM (V's attempt lock); a REDEEM after that is refused and no RECEIPT is sent. REDEEM is the
  point of no return (section 5): a CANCEL in `REDEEMED` changes nothing but the outcome V records, and RELEASE
  follows as in R8.
* **RECEIPT, ENDORSED, DialogAttach, W_SETUP, the secret socket, RESULT**: unchanged.

V's states (R8 section 9) become: `ENDORSED` → `STARTED` → `WORKER_READY` → `ATTACH_RESERVED` → `DELEGATED` →
`PAM` → `RESULT_RETAINED` → (GRANT) `GRANTED` → (REDEEM) `REDEEMED` → (RELEASE) `RETIRED`.

## 7. What a subverted A can do

Drop, delay or cancel; misreport a recheck or the display strings (it is trusted for them, section 1); forge D's
Confirm (it sees RESULT's challenge); once V granted, redeem and launch without waiting for LAUNCH (the grant is
the decision). It cannot redeem before that: V needs B0's GRANT, which B0 sends only after V's RESULT 1
(the password) and B0's own Confirm, policy and attempt checks. It cannot change the account (the grant names
the account key) or move an attempt to another session's server (V's binding, section 6).

## 8. Tests (lab)

* Codecs: types 40..51, GRANT and GRANTED, at minimum and maximum lengths, one byte short and long, wrong type,
  reserved, unknown enum values, strings.
* V: START on a channel not endorsed by S0; REDEEM without GRANT, twice, on B0's channel, on another A's channel;
  GRANT before RESULT 1, with another account key or evidence reference, twice; CANCEL racing REDEEM; B0's
  channel closed in `GRANTED`; ENDORSED from a server other than A's channel's.
* S0: a bridged `wexelevate.r9` client's session id; a bridge without a session link refused; a session id
  reclaimed by another uid refused.
* Chain: the lab suite's R8 cases with B0 and A; a refused password then a second attempt (NEXT_ATTEMPT); A
  killed in each state; B0 killed in each state; a frame altered by a test agent (B0 cancels); D exiting after
  DONE before NEXT_ATTEMPT is handled (retry still works); a foreign uid reclaiming a disconnected session's id
  first (refused; the owner's reclaim works); a REDEEM before LAUNCH after GRANT (allowed); GRANT withheld (no
  token although Confirm and RESULT are valid); two sessions with a request each (one B0, two A; attempts counted
  per account across both).

## 9. Work orders

| WO | Content | Who |
|---|---|---|
| R9-1 | Codecs: types 40..51, GRANT, GRANTED in `src/programs/wexelevate/e1r8_codec.h` and `tools/e1s6/e1r8.py`; vectors in `test_e1r8_codec_main.c` and the Python cross-check | Qwen |
| R9-2 | V (`tools/e1s6/e1s6-lab.py`): S0 as endorser, `r9=1`, channel kinds, the attempt's A channel binding, GRANT/GRANTED, REDEEM gating, states; tests in `tools/e1s6/test-e1r8.py` | Antigravity |
| R9-3 | S0 (`server/session0.c`, `named_pipe.c`): `wexelevate.r9` on the bridge list, the bridged client's session id, no reclaim across accounts, S0's S6 endorsement of B0 | Claude |
| R9-4 | `wexelevate`: B0 and A roles of one image, the relay, NEXT_ATTEMPT | Claude, Antigravity for the A-side relay loop |
| R9-5 | Lab suite cases (section 8) | Antigravity |
| R9-6 | Codex review of R9-3/R9-4 before the chain's cases count | Codex |

## 10. Review of v1 (Codex, 2026-11-12)

| v1 finding | v2 |
|---|---|
| critical: a subverted S could skip the authorization it was to enforce on itself | the grant is held by V, and the threat model says what R9 does not protect (section 1) |
| high: no route for the receipt when only B0 talks to V | A keeps its own S6 channel; REDEEM and RECEIPT keep R10's route on it |
| high: the authorization survived cancellation and did not depend on LAUNCH | the grant is live in V, single use, revoked by CANCEL, B0's channel loss, the deadline, a generation change |
| high: B0 trusted A's rechecks and display strings | kept, stated as trust in the session's system role (section 1); a recheck through the session link is possible later |
| high: V lost the session server's pin | the attempt is bound to A's channel, whose endorsing server's pin V already keeps; no pid crosses |
| high: reclaimed session ids not bound to their server | a reclaim needs the same uid (section 3) |
| medium: no retry flow | NEXT_ATTEMPT and the `RELAYING` → `SPAWNING` transition |

## 11. Review of v2 (Codex, 2026-11-12)

The verifier-held grant and the receipt on A's channel were confirmed as fixes for v1's placement and routing
findings. v3:

| v2 finding | v3 |
|---|---|
| high: a cancellation after redemption could not withdraw the receipt | the point of no return is stated: cancellation wins up to `GRANTED`; REDEEM commits (section 5), as R8's commit; S's existing issue checks still apply |
| high: A could redeem between GRANT and LAUNCH | GRANT is the decision to elevate; LAUNCH only tells A; the claim that withholding LAUNCH stops the token is gone, the test withholds GRANT |
| high: session id owners lost when session 0 restarts | owners persist in `session0.owners`; an id without a record is not given back |
| medium: a retry could fail when D exited after DONE before NEXT_ATTEMPT arrived | NEXT_ATTEMPT is sent before DONE; A's `RETRYING` state expects D's exit |
| GRANT used type 31, already LOCKED (WO-39) | GRANT 32, GRANTED 33 |
