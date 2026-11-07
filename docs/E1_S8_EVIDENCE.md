<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S8 native evidence and quotas

Implemented for **static review only**, 2026-11-06. No build, test execution,
WSL, VM, PAM or system-bus validation was performed. Read [S1](E1_S1_CONTRACTS.md),
[S5](E1_S5_BROKER_RECORDS.md), [S6](E1_S6_TRANSPORT.md),
[S7](E1_S7_PAM_WORKER.md) and [tooling](TOOLING.md).

S8 lives beside the native S6 service in `tools/e1s6/evidence.py`. It has no
listener, installer, command-line enable switch, environment override, disk
state, Wine export or launch operation. `Service` defaults to disabled; only
explicit root provisioning of this isolated lab component may set `lab=True`.
Existing S6/S7 startup and their metadata/PAM branches are unchanged. **S18 must
call the interface below:** those old branches cannot supply an authoritative
S5 binding and do not create S8 evidence. No readiness capability is advertised.
This package does not complete S18 wiring or enable standard-user elevation.

## Records and states

One `Service` instance belongs to the root native service process, shared by
every broker connection. One `Connection` wraps the actual retained S6
`Transaction`, authenticated process/socket pins, unique bus owner and server
endorsements. Neither a numeric ID nor serialized registration creates one.
The store compares Python object identity for owning connections. A reference
copied to another connection cannot redeem or cancel the owner's evidence.

Immutable `Binding` contains request ID, exact request/binding SHA-256 digests,
32-byte nonce, canonical S4 account-incarnation key/name/UID, root, server,
broker, verifier, map and policy generations, original creation time and exact
absolute expiry. Request/caller digests are S5's S1 encodings: they cover the
held image/hash, command/cwd/environment policy and complete retained caller,
prefix, session/route and token binding. S8 does not invent another digest or
interpret an environment block. S18 must establish these authoritative bytes;
the older S5/S6 synthetic binding fixture is insufficient.

An initial unselected binding may have key=16 zero bytes, UID=0 and empty name,
so the service admits the request **before** private account resolution. It
can never produce evidence or an authentication START in that state. A native
failed resolution uses `reject_selection` to run a failure-only S7 DECOY with
the same request/caller quotas and active slot. Unknown
names get bounded private SHA-256 throttle keys; known accounts always use their
16-byte incarnation key, including when selected through aliases. Candidate
names, hashes and account keys are never written to S8 audit.

Service states are `pending -> running -> ready -> terminal`. A failed S7
authentication returns to pending, with its attempt already spent. Success
never returns to pending. Cancel, disconnect, expiry, authority loss, malformed
worker result or redemption ends the request; late success cannot revive it.
All service operations hold one reentrant lock; native event callbacks must
also run serially on S6's event loop. Queued broker control during authentication
is cancellation, including a malformed packet. The broker's own serialized
cancel/consume/commit gates remain mandatory.

`begin` assigns the attempt number and constructs the exact S7 START. It checks
the returned worker's private binding, identity and deadline against that START.
`collect` alone creates evidence, after S7 private RESULT validation, both PAM
calls/final S4 checks, verified natural worker exit, one-shot `take_seed`, cleanup,
fresh authority/deadline/liveness gates and a successful audit write. The seed
is wiped and discarded; it is never the evidence reference. S8 allocates a fresh
nonzero 128-bit `getrandom` reference, collision-checking all retained evidence.
RNG failure/short reads/zero/collision deny without retrying authentication.

The evidence record is **exactly S1 kind 3, 220 bytes**, LE with caps=0 on this
disabled lab link. It adds service-assigned attempt, completion time and fixed
method/service IDs 1/1 to the selected binding. The expiry is the original
request deadline, never a new 120-second interval. The broker receives the
reference and exact record on its existing private channel and preserves the
accepted completion/expiry for redemption. S7 currently uses the isolated fixed
`wex-e1s-test` PAM stack; these lab IDs do not advertise availability of the
production `wineexplorer-elevate` stack or EVIDENCE_V1 readiness.

Redemption finds the reference only inside the owning connection and burns it
**before** comparing all 220 expected bytes, including header, attempt, UID,
completion/expiry, every generation, method and service. Mismatch consumes it;
no retry with corrected bytes. Wrong connections cannot burn another owner's
record. Successful redemption returns the same record once. Native evidence
consumption does not authorize or commit a Wine launch.

Cancel is idempotent. Request tombstones and spent attempts remain service-owned
until original expiry, including after broker disconnection/re-registration;
the same requester/request ID cannot reopen with reset counters or a new nonce.
Root/server/broker/verifier generation changes, process/owner/connection death,
map/policy changes and caller/route/image integrity loss invalidate pending or
ready records through the live native validator. Native-service restart destroys
all evidence and connection objects; new S6 root/verifier generations and pins
make old references unusable. No evidence or monotonic timestamp is persisted.
Account/caller failure tables survive broker restarts, **not native-service or
host restart**; persistent distro PAM lockout policy remains release work.

## Limits and failure handling

`Limits` is frozen and supplied once by root provisioning to the shared service.
No request/channel field can override it. S7's three-attempt/120-second ceilings
cannot be increased here. Production provisioning and policy remain disabled.

| Limit | Lab default / behavior |
| --- | --- |
| Submissions | 3 per request, assigned by service, including worker setup/crash/cancel and unknown selections. |
| Total time | 120 seconds from original broker admission, including queue and all attempts; exact monotonic `now >= expiry` denies. |
| Active authentication | 1 per `(requester host UID, Wine session)` and also 1 per requester host UID across sessions/connections; global cap 4. |
| Requests/tombstones | 256 service-wide; full table denies new requests. Terminal records cannot be evicted before expiry. |
| Failure windows | 3 failures/account and 6 failures/requester host UID in a fixed 120-second window. |
| Throttle memory | Two tables of at most 256 entries each. Only expired rows may be evicted. Full unexpired tables deny missing keys; no LRU loss of live failure history. |
| Audit | Root-owned, single-link, append-only regular 0600 FD, no access ACL; protected ancestors checked by S7 `protected(..., audit=True)`. Maximum 16 MiB and 64 lines/second, then fail closed. |

Each admitted attempt pessimistically reserves a failure in caller then account
tables **before spawning**. A verified successful result refunds those exact
window reservations after final checks/audit. Crashes, cancellation, unavailable
workers and disconnects retain the charge. A later admission failure may retain
the earlier caller charge: conservative denial is intentional. Zero-count slots
also remain until window expiry. Table saturation can deny service; it never
erases a failure or permits another password guess. Host UID is taken from S6's
endorsed actual broker process, not a request digest/nonce or candidate name.
The account bucket is the authoritative S4 incarnation, not its display name.

Workers are closed/reaped through unchanged S7 supervision before their active
slot is released. Unconfirmed cleanup (`cleanup_pending`) or cleanup exception
poisons the service against further admissions until native restart; a pending
reaper is not reported as clean rollback. S19 still owns crash-proof cgroup and
service-stop containment. The broker/S17 must serialize **all prompts**, including
click consent; S8's authentication slots alone do not implement that UI queue.

Only two public statuses exist: `OK=0` and `DENIED=1`. All failures, including
unknown/wrong/locked/expired/non-admin accounts, malformed requests, quotas,
cancel, timeout, replay, unavailable worker and audit failure use DENIED and:
“Administrator credentials could not be verified. Nothing was started.”
Every failed fixed reply zeroes reference and record. No worker exception/PAM
status/stage is forwarded. Fixed audit reasons distinguish native protocol,
worker failure, authority, deadline, quotas, pressure and lifecycle operations;
S7's existing root-only log supplies PAM stage/status detail. No candidate name,
raw diagnostic, prompt/response, password length, seed, nonce, command or
environment is logged. Audit I/O/rate/storage failure revokes all work and
disables admissions. Logging pressure is deliberate denial, not dropped audit.
The repaired default S7 lab policy supplies one fixed masked prompt and a
four-second post-answer release for unknown, wrong-password and ineligible
selections; see S7's 250 ms acceptance tolerance and residual operational limits.
This remains unverified with real PAM and is not a constant-time NSS claim.

## Small native interface for S18

There is no public operation accepting `authenticated=true`, seed or identity
success. These are **root-service in-process calls**, not a Wine client ABI.

1. Load `evidence.py` only from a root-protected module path, with no writable
   Python import ancestors. Pin/protect paths as S6/S7 do. At native startup,
   construct exactly one `Service(root_gen, verifier_gen, Audit(audit_fd),
   validate, lab=True, limits=Limits())`. Use S6's freshly allocated nonzero
   generations, a common-host `CLOCK_MONOTONIC` and protected root provisioning.
   Do not expose `clock`, validator or policy constructor parameters over IPC.
2. `validate(connection, binding)` is a mandatory **trusted, bounded/nonblocking
   native authority** callback. Check current root/server/broker/verifier,
   map/policy generations; S5's full retained caller/token/version, request/image
   lease, nonce, session/desktop route and cancellation/stop/session gates. Use
   protected authoritative snapshots; account incarnation/UID/name must agree
   when selected. Do not do potentially hanging NSS/PAM inside the service lock;
   S7 owns isolated NSS/PAM work. The existing S6 template/registration alone
   cannot implement this validator. Missing authority denies.
3. After S6's real broker/server/child registrations, create
   `Connection(service, tx, s6.receive)`. Retain this exact object for the entire
   request and use the endorsed `tx.uid`, session, generations and actual pins.
   Call `service.open(connection, binding)` once, after request freeze/admission
   and before account lookup. `created`/`expiry` must be original S5 admission
   and digest-covered deadline, never recomputed after queue/reconnect.
   S6's current stub still has a five-second setup deadline. After successful
   full-binding admission, S18 may explicitly set `tx.deadline = binding.expiry`
   for the registered request channel; setup must not already have expired.
   This replaces setup's wait with the original request deadline, never with
   `now + 120s`. Existing S6 startup does not perform that transition.
4. The root service privately resolves the selection through S4. For unknown
   or ineligible selections, call
   `service.reject_selection(connection, request_id, bounded_name_bytes, factory,
   account_key=known_s4_key_or_None)`. Known ineligible accounts MUST supply their
   authoritative incarnation, so aliases cannot evade the account bucket.
   This returns `(attempt, raw_start)` like `begin` after starting an S7 DECOY
   with the same quotas/active slot/deadline. **Do not reply early:** wait for
   worker readiness and call `connection.ready(request_id)` for its fixed
   failure reply. Admission/setup failures still raise `Denied`. For eligible
   selections, set the canonical key/name/
   UID and use `service.begin(connection, request_id, selected_binding, factory)`.
   On retry only nonce and selected key/name/UID may change; a new random nonce
   is mandatory. Changed policy/map or frozen request requires cancellation and
   a new request. No retry can extend the deadline or resurrect ready evidence.
5. `factory(raw_start)` constructs `supervisor.Worker(exe_fd,
   audit_fd, tx.child, tx.child_pin, pinned_child_pid, tx.uid, raw_start)` using
   root-pinned provisioning and S6's actual child pin. It must return that owned
   worker using S7's separately pinned root-only worker audit descriptor.
   Close a partially constructed worker on exception as S7 already
   does. **Do not use S7 `LabTransaction`**: its old callback deliberately closes
   S6 rather than issuing evidence. No raw START or worker result from the
   broker can bypass service assignment/validation.
6. On private S7 control readiness, call `connection.ready(request_id)`. This
   invokes `Service.collect`, then sends the fixed 240-byte reply below. For a
   failed authentication the request remains pending until attempts/deadline
   run out; any further attempt must go through step 4. S18 must use the same
   `connection.denied()` for failures of open/begin/selection too, and never marshal
   exception text. It must not send a second response for `ready` or `serve`.
7. After explicit bound consent and all fresh eligibility/binding checks, the
   broker atomically consumes its S5 request and sends REDEEM over **the same
   S6 data channel**, with the next actual server endorsement. Route that FD's
   readiness to `connection.serve()`. It calls S6's exact `receive`, validates
   kernel sender UID/PID/pidfd and `tx.endorsement(operation)` with S6's next
   sequence. A same-UID FD delegate cannot become the broker. Keep S6's channel
   alive through identity-ready; do not run its old metadata completion branch.
8. Schedule `service.tick()` at most every 50ms, including during module waits.
   Wire every S6 cancellation/owner loss/EOF/generation break to
   `connection.close()` **before** closing S6 pins/sockets. Revoke on caller,
   dialog, session or image loss too. At native shutdown call `service.close()`
   before releasing audit/pins. Retain existing S7 reapers until cleanup ends.
9. Redemption returns identity evidence only. S18 independently resolves current
   eligibility, verifies exact bytes against S5, installs the consumed server
   record and performs S1 token/profile/launch gates. Missing capabilities deny.
   No token request, profile lease, target execution or production fallback is
   implemented by this package.

S8 REDEEM/CANCEL are purpose-specific framing on the **existing private S6
seqpacket data FD**, not new S1 record kinds or public D-Bus methods:

| Bytes | Encoding |
| --- | --- |
| Control header, 16 | `E1S8CTL` + NUL (8), version u16=1, operation u16, exact total length u32, all integers LE. |
| REDEEM operation 4, total 252 | Header + evidence_ref[16] + exact accepted S1 evidence[220]. Requires next S6 endorsement with operation=4, child_pid=0. |
| CANCEL operation 5, total 24 | Header + request_id u64. Requires next S6 endorsement with operation=5, child_pid=0. Idempotent, returns DENIED. |
| Reply, always 240 | status u32 + evidence_ref[16] + S1 evidence[220]. READY and successful REDEEM use OK; all failures/cancel zero every remaining byte. |

No FDs, optional fields, trailing bytes, unknown version/operation or client
quota parameters are accepted. S6 enforces per-packet credentials, pin liveness,
sequence and one-connection endorsement; S8 compares full stored record bytes.
These operation numbers are private native framing, not the existing
`WexE1s6Transport` PE operation namespace (whose CLOSE is already 4). S18 must
allocate separate PE dispatch values while preserving CLOSE; the existing
server's disabled sideband forwarding already carries the endorsed u32 value.
Tests simulate server endorsement because they do not start Wine. S18 must
extend the broker/wexhost **lab-only** operation plumbing later; none of those
protected files is edited here.

## Qwen build/test handoff and coverage

Coordinate exclusive guest ownership under TOOLING.md; another agent currently
owns S5–S7 build/test work. Do not stop/reset/rebuild that setup. Use its agreed
disposable Linux guest/clone after handoff, with Python >=3.11 and Linux
SO_PASSPIDFD/SCM_PIDFD support. No Wine or C build is needed for the new Python
component. Syntax compilation and component invocation from the synced clone:

```sh
# In an owned guest, never on the Windows host:
python3 -m py_compile tools/e1s6/evidence.py tools/e1s6/test-evidence.py
sudo bash scripts/test_evidence.sh --disposable-guest
```

The harness takes a nonblocking `/run` directory-inode flock, creates one
private random `/run/wex-evidence-test-XXXXXXXX` directory and checks its exact
device/inode/owner/mode before cleanup. It creates no users, installed service,
public socket, PAM file or account-map change, and does not touch S2/S7 fixtures.
The root-only audit is inside that owned directory. Final status is
`E1S8_EVIDENCE OK` or `E1S8_EVIDENCE FAIL`; unavailable kernel prerequisites fail.
No fixture directory or lock file is adopted from another run.

`tools/e1s6/test-evidence.py` supplies an explicit worker/result/authority fixture
and real Unix socketpairs through S6's **actual** packet receiver (kernel
credentials/SCM_PIDFD). Its transaction endorsement is a test double, not Wine
registration. All such doubles are confined to the test client. Cases cover:

* Redeem once/replay; another connection with equal identity; expiry at exact
  deadline; repeated cancel/late success; new service instance, broker/server/
  policy/owner death and connection EOF. An additional test spawns an actual
  isolated native fixture process, obtains evidence, SIGKILLs/reaps that exact
  process and starts a fresh instance; the old reference must deny there too.
* Independent mismatch of **every** S1 evidence field and header; mismatch burns
  the record. Changed frozen digest/map/policy/deadline or reused nonce denies.
* Three service-assigned attempts, fresh retry nonces, broker-restart tombstones,
  unchanged absolute deadline including queue time; bounded request table.
* Per-account failures across requesters and per-caller failures across accounts
  and broker reconnections; both throttle tables under pressure, refusal to
  evict unexpired history, then actual eviction/admission after window expiry.
* One active verifier per session/requester, across connections/sessions, and
  global cap; cancellation releases the slot after cleanup, pending reaping
  blocks new admissions.
* Unknown/ineligible selections versus wrong-password worker fixture: same
  worker/ready path, active slots and attempts; successful decoy results cannot
  issue evidence. Identical generic
  fixed replies and failure charges, candidate absent from audit. Worker failure,
  abnormal exit, dialog cancellation and queued broker cancel cannot create
  evidence. Zero/duplicate reference RNG, audit failure and backward clock deny.
* Real seqpacket framing, fixed replies, redeem/replay/cancel, wrong connection
  and malformed operation; default-disabled service refuses admission.

These tests **do not run real PAM** or create a real S6 broker registration.
The process-restart case uses fixture authorities/workers, not the installed
system-bus endpoint. Separately run S7's unchanged
`scripts/test_pam_worker.sh --disposable-guest wex-e1s-test` for real unknown/
wrong/locked/expired account and final-user/protocol checks, then S9/S18's actual
S6-endorsed integration with both Wine ABIs. S8 component success cannot replace
those gates or prove identical PAM timing. The repaired S7 test compares actual
prompt bytes and post-answer timing classes (three samples/class, 250 ms
tolerance); run as root in wexbuild or a throwaway VM, NEVER shared guest VM B.

Combined-batch lock/audit corrections: wire CANCEL delegates directly to
`Service.cancel` under its lock instead of reading `requests` without it.
Cancellation logs each matched request with its actual ID and attempt, including
cancel-all. Unmatched IDs cannot create misleading zero-ID audit rows. New
component cases check decoys, cancellation audit and proc-directory/pidfd
executable matching; earlier S8's 30-case pass does not validate these repairs.

## Files and unverified items

New: `tools/e1s6/evidence.py`, `tools/e1s6/test-evidence.py`,
`scripts/test_evidence.sh`, this document. Documentation register:
`docs/STOCK_CHANGES.md`, **zero additional stock Wine changes**. No edits to
S4, broker `main.c`/`e1s.h`, `wine/server`, wexhost or `tools/e1s7`; no commit.

Performed: manual state/packet/offset/ownership/cleanup review, Git Bash
`bash -n scripts/test_evidence.sh`, and whitespace checks. The command runner
returned empty output even for `Get-Location`; direct Node child-process output
was used for static Git/Bash checks instead. `where.exe python python3 py`
reported `INFO: Could not find files for the given pattern(s).` Python AST/
bytecode parsing was therefore not performed. No fallback WSL or VM was tried.
One edit attempt failed atomically with `apply_patch verification failed:
Failed to find expected lines` for a shortened `created`/`expiry` documentation
context; no files changed in that attempt. Reissuing with the exact source
context succeeded.

**Unverified:** Python parsing/import/execution; all component tests and exact
S6 kernel ancillary behavior; real worker integration/PAM success/failure and
cleanup; active callback timing, cancellation/redeem/restart races, pidfd/FD
leaks, allocator/RNG/I/O/fsync/storage/rate-limit failures, malformed ancillary
and delegated-writer behavior; root path/import/audit protections on the guest;
actual S18 full authoritative S5/clock/map/policy validator and message plumbing;
real broker/native/wineserver/host restart and both ABIs; UI serialization and
no-secret scans in all sinks; S19 crash containment and production distro/PAM/
throttle/audit retention policy. This static patch is not runtime security proof.
