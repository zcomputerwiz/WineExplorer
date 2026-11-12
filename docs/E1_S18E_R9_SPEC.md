<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S18e R9: broker in session 0, agent in the session (spec v1, for review; system tier T6.3)

Decision (2026-11-12): option A of [E1_S18E_R9_DESIGN.md](E1_S18E_R9_DESIGN.md), towards fast user switching.
R9 changes only what this document names; everything else is R8 ([E1_S18E_R8_SPEC.md](E1_S18E_R8_SPEC.md), frozen
v5) with R10 as built (WO-31). The chain stays lab only and disabled in normal builds, as R8.

## 1. Parties

| Party | Where | Role in R9 |
|---|---|---|
| B0, the broker | session 0, `wexelevate` as a LocalSystem service of session 0 (T5) | everything R8's B decides: policy, the attempt count, the pre-endorsement tuple, every B↔D frame's content and every check on D's frames, the talk with V (START .. RELEASE), the final account (R8 section 8), the challenge |
| A, the agent | the user's session, `wexelevate` as the session's service (system role, as R8's B) | the program's request (as today), D's creation, pipes, endorsement and rechecks with S, the byte-exact relay of B↔D frames, stage 2 (redeem, issue, launch) on B0's order |
| S | the session's server | as R8 and R10, plus the authorization from session 0 (section 5) |
| S0 | session 0's server | the bridge to B0 (section 2); the S6 endorsement of B0 to V (section 6) |
| D, V, W | | as R8, plus V's server binding (section 6) |

Rule: B0 decides, A acts. A never builds a frame for D, never interprets one beyond the length check of
section 3, and never talks to V. B0 never touches an object of S.

## 2. The A ↔ B0 channel

* **Name.** B0 serves `\pipe\wexelevate.r9` in session 0 (message mode). Session 0's bridge allows the name
  (T5.2 list: `svcctl`, `wexelevate.r9`). A opens `\pipe\WexSession0\wexelevate.r9`: one connection per request,
  kept until the request ends.
* **The pipe's descriptor** (set by B0): SYSTEM full access, nothing else. Session 0 checks it against A's
  described token (T5.2), which must be bound to S's peer account or a service account (T5 review); so only
  SYSTEM of some session connects.
* **What B0 learns from S0, not from A.** For a bridged client, S0 records the session id of the link's peer
  (the session link of T6.2 whose peer pid is the bridge request's peer pid) and that server's pid. B0 reads them
  with `GetNamedPipeClientSessionId` and `GetNamedPipeClientProcessId` (S0 answers these for a bridged client
  with the session id and the session's server pid). A bridge whose peer has no session link is refused for
  `wexelevate.r9` (`STATUS_ACCESS_DENIED`). B0 refuses a client whose impersonation token is not LocalSystem.
* **Framing.** Each pipe message is one R8 frame: the 76-byte common header of R8 section 4 (`magic "E1R8"`,
  version 1, type, length, request_id, attempt, reserved 0, nonce, channel_id) and a body; R8's encoding rules
  apply (exact length, no padding, refused on any deviation). New types 40..51 (section 3).
* **Limits.** B0: one request in flight per session (as WO-15 per broker today), at most 8 sessions with a
  request in flight; a ninth is answered DECISION deny.

## 3. Messages A ↔ B0 (types 40..51)

Header fields: `request_id` is A's request id (nonzero), `attempt` 0 on REQUEST and DECISION deny, else R8's
attempt; `nonce` and `channel_id` zero on REQUEST, else the attempt's.

| Type | Name | Dir | Body after the header | Total |
| --- | --- | --- | --- | --- |
| 40 | REQUEST | A→B0 | `kind:u32` (1 runas, 2 manifest), `binding_digest:32`, `request_digest:32`, `server_gen:u64`, `desktop_route:16`, `secure_desktop:u32` (1 the session has its secure desktop, 0 not), `requester:str`, `image:str`, `publisher:str` (display strings, R8 section 4) | 184 .. 1744 |
| 41 | DECISION | B0→A | `status:u32` (1 prompt, 2 deny, 3 elevate without prompt), then for 1 and 3: the pre-endorsement tuple (204); for 2: `reason:u32` | 84 deny, 284 otherwise |
| 42 | ENDORSED_LOCAL | A→B0 | `dialog_id:16` (S's), `status:u32` (1 endorsed, 2 refused) | 96 |
| 43 | TO_D | B0→A | one complete R8 B→D frame (types 1, 3, 9, 5, 6, 8), as bytes | 152 .. 2143 |
| 44 | FROM_D | A→B0 | one complete R8 D→B frame (types 2, 4, 7) as read from D's pipe | 157 .. 340 |
| 45 | RECHECK | B0→A | `step:u32` (1 attach, 2 ready, 3 result, 4 confirm, 5 redeem) | 80 |
| 46 | RECHECKED | A→B0 | `step:u32`, `status:u32` (1 ok, 2 refused) | 84 |
| 47 | LAUNCH | B0→A | `account:str` (canonical), `evidence_ref:16` | 97 .. 159 |
| 48 | LAUNCHED | A→B0 | `status:u32` (1 committed, 2 failed before commit, 3 failed after commit), `win32:u32` | 84 |
| 49 | CANCEL | A↔B0 | `reason:u32` (1 user, 2 D gone, 3 deadline, 4 peer gone, 5 protocol) | 80 |
| 50 | END | B0→A | `outcome:u32` (1 completed, 2 cancelled, 3 denied) | 80 |

Rules:

* **REQUEST.** A fills it from S (the caller's pipe binding, R8's S3 query) and from the program's request, as
  B does today. `desktop_route` and `server_gen` are checked by S at endorsement (R8 section 3); B0 does not
  trust them, it copies them into the tuple. `caller_session` in the tuple is the session id S0 reported for
  the connection, not a field of REQUEST.
* **DECISION.** B0 applies the policy (`ConsentPromptBehaviorAdmin`, `ConsentPromptBehaviorUser`,
  `PromptOnSecureDesktop`, `EnableStandardUserElevation`, the lockout policy), all from session 0's registry, and
  the attempt count. Status 3 (no prompt) carries a tuple too: S's authorization (section 5) needs it; no D is
  created and the flow goes to LAUNCH after B0's own final-account step with the caller's own account.
* **Relay.** For each TO_D, A writes the embedded frame to D's pipe unchanged and in order; for each frame read
  from D, A sends FROM_D unchanged. A checks only that the embedded frame's header length equals the embedded
  bytes and is within R8's bounds for its type; anything else is CANCEL 5. A never holds a frame back to reorder
  it. B0 applies every R8 check to the embedded frames as R8's B does (they carry the sealed tuple).
* **RECHECK.** A asks S to recheck D (R8 section 3 "Rechecks") at each step B0 names and answers with the
  result. B0 sends READY, RESULT's publication, the Confirm acceptance and LAUNCH only after a RECHECKED ok of
  the matching step, exactly where R8's B asks S.
* **LAUNCH.** Sent only after B0 accepted Confirm (or decided status 3) and after S acknowledged the
  authorization (section 5). A then redeems as R10 (op 4 with V's RECEIPT), issues and launches; it reports
  LAUNCHED. B0 sends RELEASE to V and END to A when R8's B would.
* **Order and timing.** Every message is answered in the order of R8 section 9's states; an unexpected type in a
  state is CANCEL 5. B0 holds the attempt's deadline; A cancels on the deadline B0 put in the tuple.

## 4. A's states

`IDLE` → (REQUEST sent) `REQUESTED` → (DECISION 1) `SPAWNING` → (D created suspended, endorsed, resumed;
ENDORSED_LOCAL sent) `RELAYING` → (LAUNCH) `LAUNCHING` → (LAUNCHED sent) `ENDING` → (END) `IDLE`.
DECISION 2 → END path; DECISION 3 → `LAUNCHING` directly after S's authorization. A cancels (kills D, closes the
pipes, sends CANCEL) on: D's death, a broken pipe to D or B0, the deadline, a failed RECHECK it was asked for,
the caller's death. After LAUNCHED 1 (committed) A follows R8's post-commit rules. Cancellation wins as in R8.

## 5. Session 0's authorization of the token (S)

* **Order** `WEX_SESSION0_ORDER_ELEVATE` (session link, T6.2; `arg` 0), with a body: `request_id:u64,
  attempt:u32, dialog_id:16, account_key:16, evidence_ref:16`. The session link's order message gets a body
  for this order: `struct wex_session0_order` followed by those 60 bytes; the answer's `value` is 1 on success.
* **S accepts it** only on its own session link (T6.2), only for a dialog it endorsed in this session (the
  retained sealed tuple with that `request_id`, `attempt` and `dialog_id`), while that dialog's endorsement is
  current (R8 section 3 retained objects), at most once per dialog. It records the authorization with the
  sealed tuple.
* **At issue** (R10 op 4 and ISSUE), S also requires the authorization: its `account_key` equals the receipt's
  account key, its `evidence_ref` equals the receipt's, its tuple is the tuple of the redemption. Without it, or
  on any difference, the issue is refused and the context is burned (as a receipt failure today).
* **Status 3 (no prompt)**: B0 sends the order with `dialog_id` zero and a zero `evidence_ref`; S accepts it for a
  request A registered for elevation without a prompt (A's NEGOTIATE names the request and the caller's own
  account, R10); the issue then needs no receipt. Whether the no-prompt path exists in the lab chain is decided
  by the policy work orders; until then B0 never sends status 3.
* B0 sends the order after Confirm and before LAUNCH, and LAUNCH only after the order's answer is 1.

## 6. V and S0

* **S6 endorsement of B0.** S0 endorses B0 for the S6 transport as a session's server endorses the broker today
  (`sysrun_e1s6_endorse`): explicit SCM_CREDENTIALS with S0's pid, euid and the system gid, SCM_PIDFD. V accepts
  as endorser a wineserver of the configured `session0_user` account with the system group (in addition to the
  sessions' servers), with the same executable inode check. A broker endorsed by S0 is "B0"; V allows R8's
  broker messages only from B0 once R9 is configured (`r9=1` in V's configuration).
* **The session's server in V's record.** START gains a field after `account_key`: `server_pid:u32` (the session's
  server pid as S0 reported it for the connection, section 2). V keeps it in the attempt record and requires
  ENDORSED for that attempt to come from that pid (the sender check of R8 section 6 WO-26 amendment, with this
  pid in place of "the session's server pid" that V learned from the broker's own endorsement). START's length
  becomes 101..163.
* **ENDORSED, DialogAttach, W_SETUP, the secret socket, RESULT, RECEIPT**: unchanged.

## 7. What a subverted A can do

Drop, delay or cancel (denial of service, as a subverted B today); forge D's Confirm (it sees RESULT's
challenge), which gives nothing without V's receipt for the password and B0's authorization. It cannot change a
frame without B0 noticing (B0 checks the sealed tuple and the challenge in every D frame as R8's B does), cannot
choose the account or skip the policy or the attempt count (B0's), and cannot make S issue (section 5).

## 8. Tests (lab)

* Codec vectors for types 40..50 as R8 section 11 (minimum and maximum lengths, one byte short and long, wrong
  type, reserved, enum values, strings), and START with `server_pid`.
* Server: a bridged `wexelevate.r9` client reports the session id and the session's server pid; a bridge
  without a session link is refused; ELEVATE on a foreign link, for an unknown or retired dialog, twice, or with
  a different account key or evidence reference than the receipt's: refused, no token.
* Chain: every R8 lab case run with B0 and A (the lab suite's r8 cases); plus: A killed in RELAYING, B0 killed in
  each state, a frame altered by a test agent (B0 cancels), LAUNCH withheld (no token although Confirm and
  receipt are valid), two sessions with a request each (two A, one B0; the attempt counts are B0's).

## 9. Work orders (proposed)

| WO | Content | Who |
|---|---|---|
| R9-1 | Codecs: types 40..50 and START's `server_pid` in `e1r8_codec.h` and `tools/e1s6/e1r8.py`, vectors in `test_e1r8_codec_main.c` and the Python cross-check | Qwen |
| R9-2 | V: S0 as endorser, `r9=1`, `server_pid` in the attempt record and the ENDORSED sender check, lab tests | Antigravity |
| R9-3 | S0 and S: `wexelevate.r9` on the bridge list, the bridged client's session id and server pid, the ELEVATE order and its check at issue | Claude |
| R9-4 | B0 and A in `wexelevate` (one image, two roles), the relay | Claude, with Antigravity for the A-side relay loop |
| R9-5 | Lab suite cases (section 8) | Antigravity |
| R9-6 | Codex review of R9-3/R9-4 before the chain's cases count | Codex |
