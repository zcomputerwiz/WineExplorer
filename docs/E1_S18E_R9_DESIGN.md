<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S18e R9: the elevation decision in session 0 (design, system tier T6.3)

Date: 2026-11-12. Status: design for review, not implemented. Builds on: [E1_S18E_R8_SPEC.md](E1_S18E_R8_SPEC.md)
(frozen v5), R10 as built (WO-31: the verifier's receipt), [SYSTEM_TIER_T6_BROKER.md](SYSTEM_TIER_T6_BROKER.md),
the session link (T6.2) and the pipe bridge (T5.2) in [SYSTEM_TIER_WORK_ORDERS.md](SYSTEM_TIER_WORK_ORDERS.md).

## 1. Who holds the authority today

R8 with R10 has five parties: the broker B (`wexelevate`, SYSTEM, in the user's session), the dialog D
(`consent.exe`), the session's server S, the verifier V (root, outside Wine) and V's PAM worker W.

| Decision | Holder | Why it is trusted |
|---|---|---|
| the password is right | V and W | PAM, outside Wine; D's secret goes to W only, over a socket B never sees |
| D is the real consent dialog | S | creation, endorsement and pin (R8 section 3) |
| the elevated token is issued | S | only on V's RECEIPT, checked against the sealed tuple (R10); never on B's bytes |
| the elevated program is started | S and B | B asks, S checks the issued token and the request |
| elevation is wanted at all (policy), which account, how many attempts | B | B reads the policy (since T1 from session 0's registry) |

So B orchestrates and applies policy; it cannot elevate without V's receipt. A subverted B could refuse or
misdirect, show a request it was not asked for, or skip the policy (for example prompt where policy says deny),
but it cannot make S issue a token without the password of an eligible account.

## 2. What moving B to session 0 gives

* **One broker for the machine**, as AppInfo is on Windows: one policy reader, one request log, one place for
  the lockout and attempt counts across sessions (today each session's B counts alone; V's throttling, WO-37,
  is already per machine).
* **The policy decision out of the user's server**: session 0 decides whether to prompt, deny or allow,
  so a session cannot skip it.
* **Not more protection of the password or the token**: those stay with V and S (section 1).

What has to stay in the session (T6 design): D (it draws on the user's secure desktop), D's creation and
endorsement by S (D is a process of S), the token issue and the launch (tokens are objects of S).

## 3. Design

### 3.1 Parties

* **B0**, the decision broker: `wexelevate` as a service of session 0 (T5: session 0 runs services as
  LocalSystem). One per machine. Holds: policy, the request record, the attempt count, the talk with V
  (START, WORKER_READY, DELEGATED, RESULT, RELEASE), the final-account checks (R8 section 8), the challenge.
* **A**, the agent: today's `wexelevate` in the session, reduced. Holds: the request entry from programs (as
  today), D's creation, endorsement and pipes, the relay of B0 ↔ D frames, and stage 2 (redeem, issue, launch)
  on B0's order. `wexelevate` stays a session service (`is_session_service`) for A, and B0 is the same image
  started in session 0 with a different role.

### 3.2 Channels

| Channel | Carries | Trust |
|---|---|---|
| A ↔ B0: `\pipe\WexSession0\wexelevate`, bridged by S (T5.2; session 0 allows the name) | the request, B0's orders to A, D's frames relayed both ways | session 0 sees A's described token, bound to S's peer uid (T5 review); S0 sets the bridged client's session id from the session link of the peer server (T6.1/T6.2), so B0 reads it with `GetNamedPipeClientSessionId`. B0 requires A's token to be the session's SYSTEM (`wexelevate`'s service token) |
| B0 ↔ V: the S6 transport | as R8 between B and V | S0 endorses B0 to V instead of S: V accepts the configured session 0 account and system group as endorser, with the same executable checks as for a session's server |
| S → V: ENDORSED | as R8 | unchanged, plus: V's attempt record (from B0's START) names the session's server (pid and pin, learned by B0 from S0's bridge record); V requires ENDORSED from that server |
| D ↔ V, D ↔ W | DialogAttach, the secret socket | unchanged |
| B0 → S: the session link (T6.2), order `ELEVATE_AUTHORIZE` | request id, attempt, the final account key, the evidence reference | S accepts it only on its own session link (peer checked, T6.2), for a request A registered in S |

### 3.3 Flow (deltas from R8 section 9)

1. A program asks A (unchanged entry). A registers the request in S (new: S keeps the request id, caller,
   image and binding digest, as B's S3 objects today) and sends it to B0.
2. B0 applies policy (deny, prompt on the secure desktop, or the configured insecure desktop) and counts the
   attempt. For a prompt, B0 builds the pre-endorsement tuple (it learns `caller_session` and the server from S0,
   not from A) and orders A to create D.
3. A creates D suspended with the two pipes, asks S to endorse it with B0's tuple (S checks as R8 section 3, and
   additionally that the tuple names a request A registered), resumes D, and from then on copies frames between
   D's pipes and the B0 pipe without changing them. S sends ENDORSED to V.
4. B0 drives INIT .. RESULT .. Confirm as B does in R8 (through A's relay), START/RELEASE with V as B does.
5. On a valid Confirm, B0 resolves the account (R8 section 8) and sends `ELEVATE_AUTHORIZE` to S on the session
   link. S marks the request authorized for that account key and evidence reference.
6. A redeems (op 4 with V's RECEIPT, as R10) and issues; S now also requires the request's `ELEVATE_AUTHORIZE`
   from session 0. A launches as today.

### 3.4 What a subverted A can and cannot do

A sees every relayed frame. It can drop or delay them, or show nothing (denial of service, as a subverted B
today). It cannot authorize: S issues only with V's receipt (password) and B0's `ELEVATE_AUTHORIZE` (policy,
attempts, final account), which arrives on S's own link to session 0. It can forge D's Confirm (it relays
RESULT's challenge), but Confirm without the password's receipt gives nothing, and B0 counts the attempt.

## 4. Cost

New: the B0 service image role and its S6 endorsement by S0; V's acceptance of S0 as endorser and of the
server binding in ENDORSED; the request registry in S and the `ELEVATE_AUTHORIZE` order and its check at issue;
the bridge's session id; the relay in A. Changed: the lab suite's R8 cases run against B0 and A. Review: R9 is an
E1 protocol revision and needs the E1 review procedure (Codex review of the trust changes) before implementation.

## 5. Options

| Option | Content | Gain | Cost |
|---|---|---|---|
| A. Full R9 (section 3) | B0 decides, orchestrates and talks to V; A relays and launches | one broker, policy and attempts in session 0, S needs session 0's authorization to issue | all of section 4 |
| B. Policy authorization only | B stays in the session as today; before issuing, S also needs `ELEVATE_AUTHORIZE` from a small B0 that checks policy and counts attempts for the request (B0 sees the request through the bridge; V is unchanged) | policy and attempt limits out of the session's reach | B0 service, the order, S's check; no V changes, no relay |
| C. Defer | keep R8 as it is until a second concurrent session needs one broker | none | none |

Recommendation: **B** now. It moves the part a subverted session broker could abuse (skipping policy or the
attempt limit) to session 0 at a fraction of A's cost, and keeps V's interface frozen. A can follow when fast user
switching (two sessions on one display) needs one broker.
