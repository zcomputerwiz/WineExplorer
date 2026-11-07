<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S5 broker records (2026-11-06)

Implemented for disabled lab evaluation; **statically reviewed only**. No build,
Wine execution, WSL, VM or authentication test was run. The standard-user path in
`identify_caller()` still returns `ERROR_NOT_SUPPORTED` (50). No production
enablement, verifier, account-map authority, credential UI or administrator-token
issuance is added. Read with [S1 contracts](E1_S1_CONTRACTS.md) and
[E1-S section 4 and package S5](E1_S_STANDARD_USERS.md).

## Files and ownership

* `src/programs/wexelevate/e1s.h`: broker-private logical records and canonical
  encoders, digest/comparison helpers and pure state operations. Everything is
  guarded by `WEXELEVATE_TEST_APPROVAL`; no native C layout is a wire format.
* `src/programs/wexelevate/main.c`: live retained-opener/consumed-writer rechecks,
  checked reversion, collision-checked request IDs, locked broker operations and
  the lab-only fixture adapter. The broker never accepts a state or authentication
  Boolean from the client. Only the numeric fixture selector crosses the lab pipe.
* `scripts/wexelevate_probe.c` and `scripts/test_sysrun_server.sh`: guarded probe
  client and the 32/64-bit matrix. Normal probe builds exclude the new modes;
  the disposable harness explicitly defines the macro. Shipping-broker checks
  also require absence of the `E1S1-REQ` domain tag, with a positive test control.

The existing filtered-admin path also gains checked `RevertToSelf`, repeated
binding checks before/after consent, before suspended create and immediately
before resume. At launch its linked token must be Primary and Full and match the
frozen caller SID, logon and session. Validation before consent and retention of
that checked token remain H2 follow-up work. Stop and
the final resume are serialized by `table_cs`. These are caller-binding
hardening changes, not standard-user enablement or completion of S19. The image
lease guard receives a separate process handle so precommit failure retains a
stable local rollback/wait handle even if the guard observes target death.

## Frozen record and comparisons

The frozen record owns a copy of the exact canonical request bytes: request ID,
caller-binding digest, held image identity and SHA-256, image path, exact command
line, absolute cwd, managed environment policy, show, flags and absolute deadline.
The SHA-256 domain tags and integer/blob/string encoding follow S1, including
the tag's terminating NUL. UTF-16 surrogate pairs are validated. Inherited
environment, invalid/nonabsolute paths, unknown flags, bad SID, zero required
identities, failed allocation/hash/RNG and expired deadlines deny. Size is bounded
by 1 MiB. Live request IDs are collision checked with bounded RNG retries.

`environment_digest` is an additional broker-local SHA-256 of
`"E1S1-ENV\0" || env_policy:u32` (policy 1 only). It freezes the environment
**policy**, not a not-yet-built administrator environment block. S1's request
digest encoding is unchanged; S15 still owns constructing and validating that
block. No caller environment is used by these records.

H2/S3 provides actual pinned process/token objects, consumed-writer comparison,
TokenId, ModifiedId, nonzero caller AuthenticationId, session, SID and process
start identity. `check_binding()` reopens the process's current primary token,
compares actual objects, checks original opener/writer versions and flags, and
compares against the initial caller snapshot. The broker thread must have no
impersonation token. No new pipe read refreshes provenance. The binding digest
uses the S1 caller-binding encoding after those checks. Its server-issued opaque
identifiers, endorsed host UID/process, prefix/route and endpoint generations
are **not supplied by H2/S3**: the lab adapter supplies clearly synthetic values.
There is no production constructor that trusts these fixture values.

Before every state step, challenge, answer, identity result, evidence consumption
and precommit step, the broker checks the live binding, deadline, cancellation,
stop, disconnect, image lease event, endpoint channel and generations. It hashes
the retained canonical bytes, re-encodes current request fields and compares the
request/binding/environment digests. Precommit also rechecks the held image/path
identity and hash, then checks the caller binding again.

Answer checks cover exact kind/version/length/capability mask, request ID, attempt,
round/index, nonce, channel and both digests. The broker channel only accepts
empty explicit consent/cancel; it never receives credential text. Duplicate
consent denies. Evidence additionally checks the selected account-incarnation
key, authoritative mapped UID, method/service, every generation, completion time
and exclusive expiry (`now >= expiry` denies). Redemption must retain exactly
the accepted completion/expiry, not merely another still-valid expiry. Expiry is
checked again after consumption and at precommit. A reference is local to the
test verifier object; it is not a bearer capability or authentication proof.

Caller logon is kept separately from administrator logon. The disabled credential
branch requires a distinct nonzero administrator AuthenticationId and equal Wine
session; its tests simulate that result. S10 must issue and independently query
the actual token/SID/logon-SID and all other required token properties.

## State machine

`queued -> prompting -> authenticating -> identity-ready -> consumed -> launch-prepared -> committed`

Every uncommitted state can become `cancelled`; cancellation is idempotent and
terminal. A committed state cannot be changed to cancelled. Only broker-private
operations advance states, under `table_cs`. The advance checkpoint and image
verification run outside the lock against an attempt snapshot; the locked step
rejects a changed attempt and rechecks stop and lease events before committing.
Receiving an answer only records
bound consent. Identity-ready needs consent and a verifier-owned reference;
consumption marks the table entry consumed and redeems evidence once under the
same lock. The ID/slot stays reserved until request cleanup. Redemption failure
cancels and cannot be retried. Launch-prepared
requires the separately validated administrator logon. Skipping or repeating a
state fails. A pending cancel/stop/death/disconnect/expiry/integrity failure wins
at a checkpoint; late success cannot reverse cancellation.

The lab models one attempt per request, with a fresh 32-byte CSPRNG nonce. It
does not implement retries, a session prompt queue or the native conversation.
Prompt/channel establishment, account selection and eligibility, token/profile
validation, guarded resume and transferred leases remain gates for later
packages. Test transitions to launch-prepared/committed simulate those gates and
**never create or resume a target**. No production call site can advance them.

## Probe matrix for Qwen

Build/run through the existing disposable sysrun harness; do not run on the host.
The syntax is `wexelevate_probe{32,64}.exe e1s <case> <image> <out>` against the
lab broker. It uses one private-envelope message containing a normal v1 request;
the existing Limited caller policy and real H2/S3 checks run before the fixture.
The frozen image is opened and leased normally. No consent child is started.

| Case | Expected fixture behavior |
| --- | --- |
| `accept` | Bound challenge/consent/evidence, single consume, distinct simulated admin logon, precommit checks and terminal commit; error 0, no target. |
| `request-id`, `nonce`, `digest`, `binding`, `sequence` | Changed answer cancels a trial attempt; changed evidence cancels the actual attempt (sequence changes answer index/evidence attempt). |
| `account` | Different account-incarnation key denies. |
| `expired`, `expiry-consume`, `expiry-commit` | Expiry before identity-ready, at consumption or at precommit denies. |
| `redemption` | Verifier's retained expiry changed after identity-ready; consumption closes the request, rejects the record and cancels. |
| `replay` | Repeat consumption denied both from a saved identity-ready copy (table gate) and the consumed state. |
| `token-replaced`, `token-precommit` | Use `NtSetInformationProcess(ProcessAccessToken)` to replace the actual caller primary token with a distinct duplicate between steps or before commit; H2 object comparison denies; restore and recheck the original object before reporting completion. No token handle goes to the client. |
| `version` | Write the caller token's existing default DACL back unchanged between steps, confirm unchanged TokenId and changed ModifiedId, then require the live security-version check to deny. DACL contents stay the same. |
| `cancel` | Repeated cancel at identity-ready is terminal; late consume cannot redeem evidence. |
| `generation`, `logon` | Changed policy generation, or simulated administrator logon equal to caller's, denies. |
| `mutated-request` | Change show field after consumption; recomputed frozen-request digest denies at precommit. |

Every completed negative fixture returns error 5 plus a private completion marker;
setup failure/early denial has no marker and fails the probe. All cases require
no process handle, no output child and no consent-stub marker. These markers are
test results, never target PIDs or authorization records. Both ABIs are wired in
section 5 of the existing broker tests.

## Still unverified

Everything requires Qwen's compile and guest run, including existing filtered-
admin controls, standard-user error 50, both client ABIs and shipping exclusion.
The actual-primary replacement probes require the lab broker's existing TCB
privilege and extra local process/token access handles; they fail the probe if
replacement, restoration or the confirming rechecks fail. The `version` probe
requires a real ModifiedId update without changing DACL contents. Concurrent
security mutation by another actor is a separate guest integration test. Failed `RevertToSelf`
fault injection, concurrent cancel/commit/stop scheduling and crash supervision
are also unrun. There is no native verifier or real evidence redemption, common
host monotonic clock/boot identity, authenticated endpoint/generation transport,
authoritative account eligibility, new administrator token, profile, session
queue, full dialog/conversation framing or real guarded credential launch here.
These fixtures do not establish any of those properties or production readiness.

Lost-reply termination belongs to S19: currently a failed completion reply
closes the caller's duplicated handle but leaves the committed process running.
S5 does not implement the decided termination rule or supervisor recovery.
