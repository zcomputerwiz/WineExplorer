<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1 standard-user elevation: adversarial static review

Reviewed 2026-11-08. **Do not enable this chain.** The current source does not
implement the authenticated end-to-end sequence described by the S18c/S18d
status entries. There are multiple independent fail-closed blockers, but also
security checks replaced by fixtures, a remaining storage-generation fallback,
unsafe concurrent prompt state, and incomplete launch/rollback ownership.
Fixing only the documented storage-generation gap would be insufficient.

## Scope and evidence limits

Read CLAUDE.md; every owner decision and status entry in E1_DECISIONS.md,
including S10 through S18d; E1_S_STANDARD_USERS.md, including trust boundaries
and sections 8/9; E1_S1_CONTRACTS.md; E1_S18_BROKER_INTEGRATION.md, including its
stage, trust-decision, failure and known-gap tables; E1_S18D_UNIX_SIDE.md;
E1_S17_CREDENTIAL_DIALOG.md; E1_S14_LEASES.md, particularly S14b; and
E1_S10_TOKEN.md. Traced the requested broker, dialog, launch-preparation,
wexhost, native evidence/PAM, token, process/thread, registry and storage code.
Also followed relevant access checks into sysrun.c, handle.c, debugger.c,
file.c/fd.c and the native loader's sysrun.c, and read the S18 test script.

This review used filesystem reads and report creation only. **No shell commands,
builds, tests, generators, Wine programs, services, WSL, VM operations or probes
were run.** Historical PASS counts are the decision log's claims, not new
evidence. All file:line references describe the inspected working tree; ranges
identify related statements. Only this new report was written. No fixes or
commits were made.

Severity and exposure are separate:

* **Exploitable now** means reachable in the ordinary source path without E1-S
  enablement; it is a static attack argument, not an executed exploit.
* **Once enabled** includes the lab credential path and the intended usable
  integration. Where additional wiring defects currently block an attack, they
  are named explicitly. It never means that changing one registry value alone
  makes the complete attack work.
* **Hardening** denotes defense in depth or a proof/validation gap without an
  established attacker-controlled path to privilege acquisition.

Source shorthand below resolves to the requested paths: e1s18_broker.c,
cred_dialog.c/.h and launch_prep.c/.h are under src/programs/wexelevate;
consent.c is src/programs/consent/consent.c; e1s6.c is
src/dlls/wexhost/e1s6.c; evidence.py and e1s6-lab.py are under tools/e1s6;
pam-worker.c and supervisor.py are under tools/e1s7; unqualified server C/H
filenames are under wine/server. `storage.c` in the isolation table abbreviates
wine/server/wex_profile_storage.c.

No unauthenticated Alice token or Alice process creation is established in the
current shipping configuration. Default callers still deny without the broker
define, S10/S13/S6 retain their separate build gates, and the real path has
independent blockers below. This is not a security acceptance verdict: the
present inability to launch masks dangerous behavior farther downstream.

## Ranked findings

| ID | Severity | Exposure | Result |
| --- | --- | --- | --- |
| R1 | Critical if promoted | Once enabled and wiring repaired; lab service only today | Native adapter supplies a stored administrator password instead of requiring the typed credential. |
| R2 | High | Once enabled; reached before PAM | Concurrent broker threads mutate/free an unlocked prompt queue, and waiting entries spawn immediately. |
| R3 | High | Once Stage 2 can redeem | Storage-query failure still becomes successful generation 1 in the broker. |
| R4 | High | Once enabled; current server gates block token use | Fixed nonce, incomplete digests, invented generations/identity and incomplete evidence validation break S1 authentication binding. |
| R5 | High | Once downstream stages are usable | Profile commit and resume do not revalidate full authority/deadline or atomically bind caller validation to token use. |
| R6 | High | Once a launch succeeds | New path drops image protection at request cleanup instead of target exit. |
| R7 | High | Once token/profile/child stages are reached | Token leak, incomplete rollback ownership, orphan setup leases and lost-reply outcome violation. |
| R8 | High release blocker | Current enabled dialog path; fails closed | Actual dialog never receives binding initialization, submits no account/password, and can deadlock on pipe flush. |
| R9 | High release blocker | Current native PAM/evidence adapter; fails closed | Service startup/registration, phase order, sender identity and evidence layout do not interoperate. |
| R10 | High release blocker | Current real S10 call; fails closed | Invalid NEGOTIATE call and no authenticated consumed-record producer connect redemption to token issuance. |
| R11 | High | Blocked on Windows today; unsafe if bypassed to launch | Launch preparation uses invented token/profile/UID inputs; validated cwd and Unix environment are discarded; Windows block has the wrong encoding. |
| R12 | Medium | Exploitable now; inherited | Nonreading pipe clients can retain broker worker slots indefinitely and prevent service shutdown. |
| R13 | Medium | Once Alice exists; analogous parent behavior in filtered path | Designated Bob parent imports Bob-controlled debugger/job authority into Alice's process. |
| R14 | Medium | Once profiles are used | Second launch for an already loaded Alice hive is denied instead of acquiring an independent lease. |
| R15 | Low / hardening | Parsing, secret-memory and validation surfaces | Exact-frame checks, credential UI hardening and actual-token validation remain incomplete. |

### R1 — The native adapter authenticates with a stored fixture password

**Evidence:** tools/e1s6/e1s6-lab.py:465-498 creates a feeder socketpair; its
callback reads `/root/e1s-fixtures-passwords`, selects `name=...`, and sends that
password to PAM for a prompt. Lines 474-488 construct Python strings/bytes and
delete references without wiping their contents. This is not a credential
supplied by consent.exe. consent.c:780-809 reads the typed password, sends only
an empty-payload Confirm, and erases the local password.

**Attack:** if this adapter is made operational and used for actual evidence,
Bob selects Alice and confirms with an empty or incorrect password. The service
substitutes its stored Alice fixture password. If PAM accepts it and subsequent
issuance wiring is completed, Bob gets Alice's token/process without knowing
Alice's credential. It also answers supported-looking prompts with the same
saved password instead of a bounded user conversation.

**Reachability qualification:** this is explicitly an uninstalled lab service,
and R8-R10 plus the feeder's wrong sender identity currently prevent a complete
attack. It is not a demonstrated production credential bypass. It is a critical
latent bypass if these fixture semantics are carried into the usable chain.

**Fix:** remove the password-file feeder from every path capable of issuing S8
evidence. Keep synthetic authentication in a separate test-only module that
cannot advertise production readiness. Delegate a private channel to the exact
registered dialog process; only that dialog and the native worker may handle
typed secret answers. The Python service must route/control the transaction,
not read, manufacture or retain passwords. Tests must prove that wrong and empty
typed passwords fail even when a root-only fixture password file exists.

### R2 — Queue races can corrupt the SYSTEM broker; serialization is absent

**Evidence:** main.c:1801-1817 creates independent request threads.
e1s18_broker.c:409-418 lazily initializes one global queue without synchronization.
cred_dialog.c:354-396 traverses and modifies the list without a lock;
:415-469 unlinks, frees and promotes entries without a lock. The caller in
e1s18_broker.c:706-765 never waits for `q_entry->is_active`; successful enqueue
immediately executes/spawns a dialog. There is no integration call to the
deadline sweeper. The filtered-admin approve path at main.c:1679-1689 never
enters this queue. Release also promotes a waiting entry even if another active
entry remains (cred_dialog.c:453-469).

**Attack:** Bob submits two concurrent standard requests, then repeatedly
cancels/exits one while another enqueues/releases. One thread can traverse an
entry another just freed, or race tail/head/count updates. Possible results are
use-after-free, list corruption, skipped quotas and broker crash; arbitrary
code execution is not proven statically. Even without a race, both requests can
show prompts together. This happens before credential verification. More
processes also bypass main.c's PID-based table quota, though the intended queue
quota uses the caller logon (cred_dialog.c:360-363).

**Fix:** use a service-initialized locked queue with retained entry ownership
and wake/cancel events. Wait for actual activation under one absolute deadline;
never free an entry while a request thread still owns it. Use session/validated
desktop routing with one active prompt per route, and include click consent in
the same arbitration. Cancellation/deadline checks must be part of that owner,
not a sweeper that frees another thread's raw pointer. Protect replay and burn
tables too: e1s18_broker.c:339-403 has the same unsynchronized global counters
and silently stops recording after 64 entries. Native S8 atomic consumption,
not this bounded cache, must remain the primary replay defense.

### R3 — The removed storage-generation default remains in the broker

**Evidence:** e1s18_broker.c:654-662 sets `*out_storage_generation = 1` after any
failed STORAGE_QUERY, then returns ERROR_SUCCESS at :672. This contradicts
E1_DECISIONS.md:198's statement that Stage 2 fails closed at the missing storage
provider. evidence.py:498-516 does deny without a provider, but
`_deny("storage")` uses a reason absent from Audit.REASONS (:114-117), causing
`_log` to mark the entire service failed and revoke all work (:232-239).

**Attack:** once a valid redemption reaches the query, make storage unavailable
or cause a query denial. The broker claims successful discovery of generation
1. A profile actually bearing 1 can pass the subsequent exact comparison;
other profiles deny for a misleading reason. Bob owns the on-disk metadata and
can prepare generation-1 data within the accepted native storage limit. This
does not by itself bypass PAM or the S10 account check, but it bypasses the
missing-authority/setup-failure denial and defeats the claimed binding.
An ordinary unsupported query can also disable the shared evidence service
through the invalid audit reason.

**Fix:** zero every output initially; return the actual denial/unsupported
status on query failure or zero/malformed generation; close the connection on
every exit. Add a fixed permitted audit reason without making unsupported
storage a successful result. Do not replace the fallback with another guessed
value. The recommended complete design is specified below.

### R4 — S1 bindings are synthetic, and Stage 2 trusts the wrong fields

**Evidence and consequences:**

* e1s18_broker.c:1734-1736 makes every nonce `i ^ 0xa5`. The channel ID is
  `nonce[i] ^ 0x5c` (:702), and appears in dialog argv (:492). No RNG is used
  for either. main.c:164-169 does correctly randomize the public request ID;
  that does not satisfy an independent random attempt nonce.
* :1657-1673 hashes only request ID/PID/session and then ID/binding/nonce/image
  hash. It omits S1's caller SID, primary object/security version, logon,
  endorsed native/prefix/route identities and generations; image file identity,
  path, command, cwd, show, flags and absolute deadline are absent too. These
  are not the canonical records required by S1:15-17,25. The main request's
  strings are copied and not normally mutated; digest incompleteness alone is
  not proof Bob can edit those private copies. It removes the required evidence
  against request/endpoint substitution and incompatible downstream producers.
* :809-812 uses Wine caller PID as the expected host UID, with fallback 1000.
  S1 kind 3 host_uid is **the post-PAM administrator UID**, as correctly stated
  in evidence.py:63 and S1:29, not Bob's UID or a Wine PID. Bob's host UID is a
  separate caller-binding field. Correct ordinary Alice evidence will usually
  be rejected; accidentally equal numbers do not establish identity.
* :845-885 accepts excess record bytes/unknown capability bits, attempts 1..3
  rather than this exact attempt, zero expiry, and does not validate completion
  time or any current root/server/broker/verifier/policy generation. `now` is
  sampled before a possibly blocking transport call (:800), so evidence that
  expires during that call can pass. The clock fallback returns an invented
  one second on failure (:327-332). No proof establishes that the PE clock and
  verifier CLOCK_MONOTONIC share the required epoch/semantics.
* :898-902 takes the account **name** from the dialog/boundary result, while
  only key and map generation come from evidence. :975-986 puts constant 1 in
  root/server/broker/policy generations. The evidence's corresponding fields
  are discarded. Stage 1 also substitutes `alice` for invalid/missing names
  (:568-572,746-751). No production canonical key-to-name lookup is present.
* :259-265 advertises all ten capabilities solely because the broker was
  compiled with E1S18. It negotiates no functional endpoint readiness. Native
  evidence actually advertises caps 0 (evidence.py:95-101), which the broker
  rejects. A compile flag cannot certify a working PAM, profile or commit guard.

**Attack:** arrange a late response, account substitution or authority restart
while setup is pending; the broker has neither canonical bound bytes nor the
live authoritative snapshot needed to distinguish it. Fixed nonce/channel IDs
also remove per-attempt separation and make channel identifiers predictable.
They are not sufficient to forge a response without channel access. Current
S8's connection identity and exact-record comparison and S10's installation gate
still stop a standalone serialized fake; do not remove those gates to make this
broker pass.

**Fix:** reuse the complete S5 canonical encoder with actual retained H2/S3
objects and endorsed S6/native identities; use checked cryptographic nonce
generation, a new nonce per account attempt and one recorded absolute deadline.
Retain and compare the exact verified evidence and all current generations.
Resolve its account key through the protected S4 authority to the canonical
name/SID/UID; typed text is only selection input. Keep requester host UID separate
from authenticated administrator UID. Enforce exact length/version/caps,
nonzero IDs/reference, expected attempt, completion <= current time < expiry
<= original deadline using the common native clock. Clock failure must deny.
Negotiate readiness across the actual components; unavailable work must return
unsupported before collecting a password.

### R5 — Commit/resume misses the full final gates and is not atomic with caller mutation

**Evidence:** standard dispatch returns at main.c:1673-1677, bypassing that
function's repeated bindings and `table_consume` at :1679-1688. The replacement
pipeline never consumes the table entry or uses S5's transition state.
e1s18_broker.c:1768-1774 does not recheck binding/stop immediately before child
creation; :1491-1515 only checks caller binding, stop and image event before
resume. There is no final expiry, policy, eligible-account, endpoint, route or
generation check. The S6 connection has already been closed at :665.

S14b checks lease/token/process consistency correctly, but its
token_validate_e1s_commit (token.c:2840-2865) deliberately does not check caller
liveness/version, expiry, fresh eligibility or the native validator. Those are
explicitly left to S18 (token.c:2816-2818). LOAD does perform those checks
(:2899-2905); success at LOAD does not extend their validity to resume.
`table_cs` only serializes broker threads and stop handling. `check_binding`
and ResumeThread issue separate server requests, so a different Wine process
can mutate/replace its own primary or die between the last query and resume.

**Attack:** authenticate legitimately, then revoke administrator eligibility,
restart an endpoint, let the original deadline expire, or change Bob's primary
after LOAD/binding validation while delaying remaining setup. The broker can
resume under stale authorization. A concurrent pipe disconnect without caller
death is also not checked by the final binding routine. These attacks do not
manufacture an initial credential but violate cancellation and one-request
authorization. The final caller-query/resume race also exists in the older
filtered path; it is not a new S14b defect.

**Fix:** install an authoritative consumed record (R10), maintain explicit
request states with cancellation precedence, and revalidate all gates after
slow work and before creation/resume. Add a narrow server-authorized final
resume operation bound to the original context, caller objects/versions, exact
issued token, child and lease; compare those retained objects and perform the
resume in one dispatch. Its native authority must be a live revocable owner,
not a submitted Boolean or fixture callback. Preserve S14b's never-resumed
proof and fail-closed identity checks. A disconnected caller or expired grant
must invalidate uncommitted work without an implicit retry.

### R6 — No lease guard is started; successful reply ends image protection

**Evidence:** e1s18_broker.c:1780-1784 arms the image lease, but never calls
start_guard. Its success path closes pi handles (:1814-1817), leaving req->lease
and req->lease_event owned by the request. client_thread frees that request
(main.c:1717); free_request releases the lease/event/image at :242-244.
lease.c:347-365 explicitly unlocks and closes its pidfd; it does not terminate
a target merely because protection was released. The filtered path transfers
ownership with a duplicate process handle and start_guard at main.c:1142-1165.
The new default_arm_lease also returns success when its export is absent
(e1s18_broker.c:1429-1437), although normal freeze/startup currently makes that
case difficult to reach.

**Attack:** after a successful credential launch/reply, Bob rewrites the target
image. The promised through-target-exit lease no longer exists; later mapped
image reads can be affected and the integrity monitor cannot kill the target.
The precise effect of modifying a particular PE mapping is runtime-dependent;
the lost protection is a source-level fact. This exceeds the owner's accepted
final path-open race and service-stop limit.

**Fix:** duplicate the process handle, establish the existing guard before
commit, and transfer lease/event ownership only after guard creation succeeds.
Retain a safe local handle through final validation and rollback. Missing
required arm support must deny. Retain target-only termination and service-stop
limits honestly; descriptor-bound PE loading is separate future work.

### R7 — Cleanup cannot meet the no-survivor/no-handle contract

**Evidence and attacks:**

1. On success, e1s18_broker.c:1814-1835 never closes `issued_token`. The handle
   is local and is not saved in req->issued_token, so free_request cannot close
   it. Each legitimate successful request leaks a broker TOKEN_ASSIGN_PRIMARY |
   TOKEN_DUPLICATE | TOKEN_QUERY handle. unix_env and its elements are never
   freed on success or failure (:1702-1703,1825-1826,1870-1871). Repetition
   exhausts resources and unnecessarily retains administrator authority.
   The token is not thereby placed in Bob's handle table.
2. default_terminate_child ignores TerminateProcess and the five-second wait
   result (:1522-1529). Remote close errors are discarded (:1535-1541). Failure
   cleanup then closes its remaining handles and returns an ordinary denial.
   Injected termination failure or delayed native death can therefore leave a
   suspended child without a durable cleanup owner. Broker death after CREATE
   has the same problem: no job/supervisor owns that pending child.
3. Profile rollback discards almost every unload failure. Busy state is only
   one process-global Boolean and a u64 cache generation (:239-252,1568-1575),
   not an account/token/context/lease owner; another request can overwrite it.
   It does not retain the token required for a retry and has no reaper caller.
   Other failures, including poisoned/save-failed hives, are silently lost.
   registry.c:3412-3423 automatically drains only state-2 leases; a broker crash
   after LOAD leaves state 1, loaded hive and locks until explicit cleanup or
   server exit. Deliberately poisoned hives must remain retained, but the broker
   must report and track that state rather than claim a clean rollback.
4. client_thread main.c:1707-1710 explicitly leaves a committed process alive
   if the reply is lost, closing only the remote handle. This violates the
   already decided lost-reply rule (E1_DECISIONS.md:31-32; S1:56). It is inherited
   behavior and is acknowledged S19 work, not a new decision to reopen.

**Fix:** close the issued token after successful transfer/resume and free every
environment allocation with the matching result destructor. Give pending
child/dialog/worker/profile resources an exact supervisor-owned record before
creating them. Cleanup must return confirmed status and preserve handles/pins
when reaping cannot be confirmed. Retain busy/poisoned profile owners per
prefix/account/actual lease, with checked retry/reporting and no force unload
of another live user. Tie state-1 release to broker/context death through a
server/supervisor contract. Keep enough committed process ownership to terminate
and confirm death before reporting a lost-reply launch as failed; never retry.

### R8 — Live dialog wiring is absent, and its pipe protocol can hang

**Evidence:** e1s18_spawn_consent_dialog creates a **one-way** anonymous pipe
(e1s18_broker.c:464-466), passes only request ID/channel token/handle in argv
(:487-494), and never sends initialization. consent.c:199 zeroes nonce and both
digests; no non-test read populates them. Confirm at :791-800 consequently sends
zero binding fields. The broker rejects that Confirm against its nonzero
nonce/digests (:550-553). The typed account/password never leave the dialog
(:780-809); broker success substitutes literal `alice` (:568-572).

The child calls FlushFileBuffers on the pipe before EndDialog
(consent.c:802-811,836-845). The broker waits for child exit before ReadFile
(e1s18_broker.c:523-541). A pipe implementation that waits for unread data to
drain makes this a cyclic wait until the broker kills the prompt at timeout.
Even if that flush returns immediately on a particular Wine implementation,
the zero-bound answer still denies. After child exit the broker uses an
unbounded synchronous read; another inherited write end can keep it open.
CreateProcessW uses TRUE handle inheritance with no handle allowlist (:505),
so simultaneous dialogs can inherit each other's inheritable pipe ends.

**Attack:** Bob triggers requests to occupy the secure desktop/queue for the
full timeout, and concurrent inheritance can extend EOF-dependent waits. Alice
can type a real password but nothing authenticates it. This is denial and a
false integration claim, not proof that an empty Confirm currently elevates.
The prompt also defaults to `setup.exe` (consent.c:305-306): the broker supplies
neither actual image details nor the promised initialization, so the human
cannot approve the displayed frozen request.

**Fix:** implement a duplex, exact-child-bound initialization/confirmation
channel and separate dialog-to-native secret channel. Send verified request
identity/presentation over that channel; require child acknowledgement of the
same initialized binding. Use an explicit inherited-handle list with checked
handle-flag operations. Process reads concurrently with child/death/caller/
stop/lease/deadline waits; do not wait for exit before draining the pipe.
Remove all `alice` substitution and require a verified canonical account result.
Do not collect credentials until the actual verifier endpoint is ready.

### R9 — The native evidence adapter cannot establish its advertised flow

**Evidence:**

* e1s6-lab.py:229 constructs Service(root_gen, verifier_gen, ...) before those
  local variables are assigned at :238. `--pam-worker` startup raises an
  UnboundLocalError if it reaches that line. Without PAM arguments the evidence
  service is absent. The module's own header still says no evidence exists.
* Transaction.initialize constructs evidence.Connection at :286-287 before
  endorsement creates server_gen/broker_gen/session (:370). Its constructor
  immediately reads those attributes (evidence.py:549-554). Moving the earlier
  assignment alone is insufficient.
* Broker transport OPEN is followed immediately by REDEEM
  (e1s18_broker.c:620-634). It sends the first 16 bytes of the authentication
  nonce as a supposed evidence reference and leaves the expected 220-byte
  record zero. No begin/child/PAM/collect operation obtains a real reference.
  The service is still in phase `child` (e1s6-lab.py:408-415), expecting a
  registration/answer/child-FD packet, not this redemption. wexhost's phase>=1
  acceptance for REDEEM/STORAGE_QUERY (e1s6.c:666,683) does not repair the remote
  state machine.
* The Python feeder socket is written by the root service, but Worker is
  configured with the registered child PID/UID (e1s6-lab.py:495-497).
  pam-worker.c:157 requires every answer sender to match that child. The
  kernel-reported root-service sender cannot pass. Bypassing this check to make
  R1's feeder work would weaken the authentication boundary.
* Evidence records use caps 0 and authenticated Alice UID
  (evidence.py:95-101); the broker expects 0x3ff and caller PID (R4).
  The adapter assigns request_digest from template[40:72], actually the S1
  answer's nonce; binding_digest uses [72:104], and the true request digest at
  [104:136] is ignored (e1s6-lab.py:438-451; cred_dialog.h:83-86).
* The production validator is still a lambda checking an attribute `stale`
  that defaults false (e1s6-lab.py:229-230), not current S4/policy/S5 authority.
  `created` is reset at PAM start (:460) rather than original admission;
  policy_gen is invented as 1 (:459). Transaction.live retains the old five
  second setup deadline (:282,323-324), not the full request deadline.
* Worker/feeder GLib watches and worker_dialog are not recorded in Transaction
  ownership. cancel (:298-319) has no corresponding removal/close for those
  locals; failures between watch creation and worker readiness can retain them.
  S6's synchronous endorsement polling and Worker.verify_exit/close execute on
  the shared loop (e1s6-lab.py:335-339; supervisor.py:148-158,193-199), so the
  promised <=50ms tick is not guaranteed during these operations.

**Attack/impact:** valid credentials cannot traverse the live adapter. Repeated
setup/cancel failures can consume descriptors/watch owners or block other
transactions. Once made usable, the weak validator and renewed deadlines would
permit stale-generation/cancellation/quota mistakes; root-password feeding is
R1. S8's component tests do not exercise this complete adapter.

**Fix:** initialize authorities before use, construct Connection only after real
endorsement, and implement explicit registered-child/begin/challenge/ready/
redeem phases using the same connection. Preserve all exact S1 fields and the
original deadline. Obtain selections and fresh eligibility from S4, not START
assertions; supply a validator backed by retained live authority. Record and
remove every watch/socket/worker under Transaction cancellation. Keep blocking
NSS/PAM/cleanup off the service admission lock/loop with bounded supervised
ownership. Never solve these mismatches by relaxing SCM credentials, caps,
generation, nonce or server checks.

### R10 — S10 has no authenticated consumed-record installation

**Evidence:** default_issue_token calls NEGOTIATE (:954) then ISSUE (:993),
without an intervening authoritative installation. There is an earlier live
export failure: :954 passes NULL for the required `HANDLE *token` output even
on NEGOTIATE. wine/dlls/ntdll/sec.c:149-154 rejects that locally with
STATUS_INVALID_PARAMETER before any server negotiation. Pass a real local
output pointer and require its returned handle to remain NULL on negotiation;
the fake boundary must enforce the same requirement. After that repair,
NEGOTIATE initializes
state 0 (token.c:2990-2998); ISSUE requires state 1 and burns the context before
checking (:2746-2752). token_install_e1s_consumed exists at :2960, but there is
no caller of it in the server C/H sources. The only current producer is
e1s_install_fixture (:2701-2733), dispatched through WEX_TOKEN_TEST_INSTALL
(:3005-3009). The trusted validator/caller objects required by e1s_token.h:25-39
cannot be installed by merely serializing kind 4.

**Attack/impact:** the real chain denies at issuance even if preceding stages
are repaired, first at the wrapper and then at the absent installation. There
is no present ISSUE-without-evidence token bypass. A
tempting repair—call TEST_INSTALL from the broker or accept a submitted
`authenticated` flag—would make a synthetic always-true validator the security
authority and is unacceptable. Invented broker generation 1 also cannot match
the S6 server-generated registration except by accident.

**Fix:** implement the actual S8-to-server trusted producer. It must retain the
real consumed S3 caller/primary objects, canonical S4 account, exact kind-4 and
reference, original expiry, endpoint/generation/lease/route state and a revocable
validator cookie, on the original broker thread/context. Install once after
atomic redemption and request consumption; ISSUE must keep its existing
burn-before-validation semantics. A private producer must authenticate the
native connection/receipt and recheck its current authority, not trust record
bytes as a capability. Keep TEST_INSTALL and its always-true callback lab-only.

### R11 — Launch preparation is fixture data, and its outputs are misused

**Evidence:** e1s18_broker.c:1223-1259 hard-codes Bob UID/GID 1000 on Windows,
token/auth IDs 1, fake SID `S-1-5-21-999-1001`, and profile path
`C:\users\.e1s\admin`. It ignores the actual token argument. Native builds use
`/tmp` for both prefix and profile anchors (:1233-1239); Windows uses -1, which
launch_prep.c:496-498 rejects. The S18 compile script does not enable the S15
implementation for its real PE broker builds (test_broker_e1s18.sh:65-81), so
launch_prep.c:27-39 additionally returns disabled there.

Caller cwd is lossy-mapped with `& 0x7f` and bounded truncation (:1261-1271).
The resulting checked descriptor is closed (:1308-1316), the returned cwd is
ignored (:1341-1342), and CreateProcessAsUserW receives the original unvalidated
req->cwd (:1379). STARTUPINFO.lpDesktop is never assigned. The Unix environment
is produced but never applied. launch_prep.c:195-227 builds narrow byte strings;
the broker passes them as a Unicode Windows block with
CREATE_UNICODE_ENVIRONMENT (:1377-1379). The parser can read incorrect wide
strings or past the intended narrow terminator; an actual out-of-bounds read
depends on byte alignment/allocation and is untested. The fallback
HOME=/home/bob and USER=bob after failed NSS resolution
(launch_prep.c:587-608) is another guessed identity.

**Attack:** after preceding blockers are repaired, Bob can submit a cwd whose
truncated/lossy checked representation differs from the path used at creation,
or exploit dependency/configuration search in a caller-controlled actual cwd.
Alice's environment can point to an invented shared profile rather than her
validated SID/incarnation bundle. Native anchors do not prove anything about
the caller's prefix; secure/normal desktop routing is also unproven.

**Fix:** perform descriptor/UID-dependent preparation in the native authority
that owns the actual caller prefix and profile, or expose a narrow authenticated
bridge to it. PE integers are not native file descriptors. Populate real token
IDs and S4/S12 profile mapping from the validated token/loaded lease. Fail on
lookup errors. Preserve full UTF-16 cwd bytes, validate both Alice Wine access
and Bob host access, retain/recheck the actual directory through creation, and
use the validated result. Set the explicit bound normal desktop. Build a real
bounded UTF-16LE double-NUL Windows environment and apply a validated Bob Unix
loader environment. Do not make -1 descriptors or fabricated paths pass checks.

### R12 — A standard client can indefinitely occupy broker I/O workers

**Evidence:** the public pipe grants authenticated users read/write access
(main.c:1729-1735). Worker count is bounded to 64 (:71,1801), but each worker
calls synchronous FlushFileBuffers after reply handling (:1711), outside
pipe_io's timeout/stop handling. serve waits forever for active_threads on
shutdown (:1835). Unauthenticated/unsupported requests still receive a reply
and reach the flush. In the credential child there is the related R8 cycle.

**Attack:** a standard Bob client sends a request and keeps the pipe open while
never reading the reply. Fill the available workers with such connections.
Server-side pipe flush waits for the reader to consume outstanding data; new
clients lose service and stop cannot drain the workers. Exact Wine scheduling
and flush cancellation behavior require a live probe. Even before that step,
silent connections can continually occupy the 64 read slots for ten seconds
without authenticated per-caller accounting.

**Fix:** avoid unbounded flush on an untrusted peer; use bounded delivery and
disconnect semantics plus correct committed-outcome ownership (R7). Apply
admission/rate limits to pinned callers before expensive work where possible,
and bound stop/drain ownership without abandoning operations. This finding is
inherited and does not require standard-user elevation to be enabled.

### R13 — Caller-as-parent imports debugger and job control

**Evidence:** the broker sets Bob as designated parent
(e1s18_broker.c:1355-1356). Server process creation inherits parent->job
(process.c:1374-1385) and, absent an explicit debugger, parent->debug_obj when
debug_children is set (:1421-1430). It defaults true (:640). Debug events suspend
the target (debugger.c:425-435), and continue_debug_event authenticates the
debug-object handle, not a newly acquired Alice process handle (:558-584).
JOB_OBJECT_TERMINATE operates on the job's existing membership
(process.c:2027-2034).

**Attack:** Bob runs the requesting process under his debugger with child
debugging enabled, or in a job he controls. After Alice authorizes one launch,
Bob can withhold debug continuation or terminate/control it through that job,
despite receiving only a limited process handle. This can strand pre-entry
setup or kill a committed process.

**Not a confirmed injection finding:** debug-event process/thread handles use
alloc_handle (debugger.c:198-204), which checks the debugger's effective token
at wait_debug_event (:545; handle.c:266-276). Alice's protected DACL should deny
Bob ALL_ACCESS. Therefore inheriting a debug port does not, from this code
alone, prove Bob receives VM_WRITE/SET_CONTEXT or Alice's token.

**Fix:** for broker-authorized cross-account creation, suppress debugger and
untrusted job inheritance in the server, or reject caller contexts that cannot
be safely detached. Use an explicit authorized launch flag/context rather than
globally changing ordinary CreateProcess inheritance. Test debug callbacks,
exceptions, kill-on-close and nested jobs through actual designated-parent
creation. If necessary keep parent attribution separate from inherited control.

### R14 — A loaded Alice hive prevents a second authorized Alice launch

**Evidence:** e1s_load_hive rejects any existing same-SID entry, including an
exact already-loaded account (registry.c:3094-3101). Each hive stores one
reserved setup/committed lease; there is no per-request lease set for that
loaded identity. S14b state 1 -> 2 is deliberately one-shot (:3379-3401).

**Attack:** keep one authorized Alice application or registry reference alive;
later otherwise valid Alice launches fail until that hive can unload. Bob can
amplify this with inherited control from R13. This contradicts the design's
independent leases/logons sharing one persistent profile and is a release
availability/contract defect, not evidence of Carol gaining Alice's SID.

**Fix:** retain one validated loaded bundle per exact prefix/SID/incarnation,
but issue distinct request-scoped lease owners for additional logons. Commit
each owner once, count live primary/impersonation/descendant/key users, and
release only the failed request's lease. Continue denying mismatched account,
incarnation, poisoned or foreign loaded state. Do not solve this by unloading
the first application's hive or sharing one transferable setup grant.

### R15 — Additional hardening required before a usable build

**Evidence:** answer verification accepts `len >= 156` instead of exactly 156
(cred_dialog.c:139); evidence accepts extra data/caps (R4). The broker accepts
any nonzero policy DWORD rather than specifically 1 (:276-279). Actual-token
checks (:1053-1083) do not verify the S1 group/privilege/owner/default-DACL/logon
SID requirements, nor compare all actual statistics with the response. SID
length is permitted up to 68 at :1041-1043 although a 136-byte response has only
60 bytes remaining at offset 76; a replacement boundary returning malformed
data could cause out-of-bounds reads/copy. The actual NTDLL wrapper already
requires the fixed 28-byte mapped SID and validates the response header,
reserved field and generation (wine/dlls/ntdll/sec.c:191-205), so this is not an
identified live-server or standard-caller-controlled read primitive. Keep that
wrapper validation and make broker checks consistent with it. Integrity SID
dereferences assume a valid nonempty
SID. verify_child checks only SID/session and ignores the supplied token
(:1401-1420); S14b's later stronger lineage check presently compensates for
much of this. Native secret buffers have stronger hardening than the dialog:
pam-worker.c:431-434 disables dumps/clears environment, whereas consent.c has
no corresponding explicit dump suppression or locked secret memory and no
clipboard/history subclass. ES_PASSWORD is a mask, not proof of all copy,
message-read or crash-artifact behavior. Worker defaults to fixed lab service
`wex-e1s-test` (pam-worker.c:30-32), not shipping wineexplorer-elevate.

**Attack/impact:** malformed trusted-boundary replies or accidental protocol
extensions can be accepted or mishandled; exposed UI/toolkit copies can outlive
the wiped data->password buffer. No standard-caller-controlled token-response
forgery or password exfiltration through these observations is established.
The new confirmation path is a byte comparison on a private channel, not a
cryptographic signature despite comments saying 'signed'.

**Fix:** exact bounded LE decode/encode, reject excess/unknown fields, validate
SID structure and offsets before dereference, compare actual token/child shape
and originating logon with the authoritative snapshot, and preserve S14b's
checks. Use exactly documented policy values. Add explicit UI copy/history and
dump controls where supported and measure remaining copies with dummy canaries.
Provision/validate the fixed production PAM stack separately; general MFA is
unsupported until its bounded conversation and enumeration behavior are tested.

## What Bob's other processes can and cannot do

The following separates real Windows-object enforcement from the owner's
accepted caller-UID/native-storage boundary. It assumes system role and elevation
enforcement are active; merely sharing a UID is not a Wine SYSTEM registration.

| Surface | Static conclusion |
| --- | --- |
| Direct S10 issuance | Ordinary Bob cannot pass e1s_broker: SYSTEM service identity, privileged connection, SeTcbPrivilege, held broker-image endorsement, retained primary/version and broker generation are required (token.c:2545-2564,2985). Copied numeric context handles are not authority. Generic linked-token denial for standard sessions remains at :3044-3048; generic create-token privilege and process grant paths must stay separate. |
| Alice process/thread handles | OpenProcess uses alloc_handle and DACL checks (process.c:1546-1554; handle.c:266-276). sysrun_process_created replaces elevated descriptors with SYSTEM/Administrators full access and Users/Authenticated Users wait/limited query; owner is Administrators, preventing Bob's owner WRITE_DAC shortcut (sysrun.c:197-238,260-273). Threads receive equivalent protection (:284-289). Broker uses NULL supplied descriptors, so these server defaults matter. Parenthood alone is not full handle access; debugger/job inheritance is R13. |
| Alice token | OpenProcessToken requires full process query in this mode and then checks the token DACL (token.c:1823-1860). token_protect_elevated permits SYSTEM/Administrators, not Bob (:1100-1121). The returned broker-to-Bob process handle has only wait/limited-query rights (e1s18_broker.c:1446-1447). No direct duplication/impersonation/assignment path is established for an ordinary Bob Wine token. A legitimately elevated administrator can enable SeDebugPrivilege and access other elevated processes by design (token.c:1634-1640); it is not an ordinary standard token. |
| Loaded HKU/HKCU | Keys are loaded under exact mapped SID and classes, unknown existing roots deny, all managed keys get explicit Alice/SYSTEM/Administrators descriptors and High labels (registry.c:2887-2914,3122-3158; e1s_profile.c:137-165). Read-side managed profile requests must match the effective managed identity (registry.c:3495-3497). Ordinary Bob should not open Alice's live keys simply by naming her SID. Actual negative ACL/cache/WoW64 tests remain required. |
| Hive/profile files through Wine | Registered inode descriptors are checked rather than trusting arbitrary user xattrs (e1s_profile.c:283-303,413-426). Parent/inheritance checks reject unregistered inserted ancestry; fd.c:2019-2021 enforces before truncation/returning a descriptor. Apply-tree security uses Alice and SYSTEM for files, 0700 directories/0600 files, and checks ownership, link count and metadata (e1s_profile.c:168-211). Existing pre-load handles and imported/raw native FDs need explicit acceptance tests; changing a DACL does not revoke an already granted native descriptor. |
| Native Bob access | The Unix profile/hive is Bob-owned, not Alice-owned (storage.c:168-180,922-924). Bob's native code can read/copy/change that storage, retain pre-load descriptors, replace his prefix ancestors, ignore advisory flock, or poison future saves. 0600 and Wine SDs cannot protect it from its Unix owner. Offline changes can influence a later authenticated Alice application that consumes her settings; anchored identity checks are not an integrity signature over every value. Do not persist Alice host secrets or reusable credentials there. This is the explicit Wine-only model limit, not an alleged root/host-UID boundary. |
| Native ptrace/process_vm/procfs | Same UID alone is insufficient to conclude ptrace works against a system-role/elevated process that retains wexsys and is non-dumpable. ntdll/unix/sysrun.c:195-205,212-244 preserves full-token system-group behavior and documents a separate service-child post-exec nondumpable window. Kernel dumpability, effective/saved groups, Yama and exec transitions must be measured. Do not claim either native isolation or a successful ptrace bypass from this static review. Same-UID signals and storage modification are separate availability/native limits. |

S14b itself has meaningful protections: original owner/thread/context and exact
issued token/ModifiedId/lease/request/server binding (token.c:2840-2886), local
alive/unbound child, same account and exact creation-token lineage
(registry.c:3366-3405), and initial never-resumed suspension
(process.c:1131-1147). thread.c:1696-1701 sets the sticky proof after adding the
first thread; :950-959 clears it on final resume. A second thread clears it,
and resuspension cannot restore it. This closes the previously missing
production commit operation at source level; it does not provide S18's missing
authorization checks or supervisor.

Storage lock acquisition is nonblocking, so the prefix/SID/save lock-order
reacquisition does not establish a blocking lock-cycle deadlock
(wex_profile_storage.c:193-210,729-750,854-880). Bob can deliberately hold/replace
his own lock files and cause denial; that is distinct from silently succeeding
or loading another account. Durable-save failure correctly poisons and retains
the hive/locks rather than claiming persistence (registry.c:3296-3337).

## Safest resolution of the S12 storage-generation gap

Choose the **descriptor-anchored read-only query** family of designs, with the
query hosted by the native wineserver that already owns config_dir_fd, S4/S10
context and the S12 validator. This is a narrower implementation of the
privileged-service candidate than adding a root Python pathname walker. Do not
add '0 means current' to LOAD. A LOAD reply is too late to preserve today's
expected-generation precondition, and its existing `generation` is an S14
cache generation, not S12's completion generation. These are distinct values
(S14:162-176; registry.c:3480-3481).

The inspected S12 really does write `.complete`, with **WEXPROFILE 1**, exact
SID/incarnation/stored label, prefix device/inode, generation, seed and ACL intent
(wex_profile_storage.c:529-536,963-965). validate_profile validates the complete
manifest, not merely a line beginning `generation=` (:658-697). The old S18d
diagram/format prose must not replace this actual parser. Generation is a
snapshot identity check, never an authentication capability or protection from
Bob forging Bob-owned offline metadata.

Recommended exact changes, to be implemented separately from this report:

1. Remove the storage-generation dependency from evidence redemption. Stage 2
   yields only exact verified identity evidence and the authoritative account
   snapshot. Add a query **after S10 issuance and before LOAD**, so it can use
   the original live S10 context and issued token. Eliminate the broker's
   fallback 1 immediately, even before the interface is implemented.
2. Reserve an explicit broker-only operation, for example
   `WEX_HKU_STORAGE_QUERY=12`, on existing request 325. Require the same exact
   token_e1s_broker gate as LOAD/COMMIT, original consumed context/thread,
   original issued token, live caller/expiry/native validator and fresh S4
   account. Accept access=0 and a 48-byte identity record: version/size,
   account_key, nonzero exact map_generation, storage_generation=0 and reserved=0.
   Zero is permitted **only in this query operation**, never LOAD. Reuse
   token_validate_e1s_profile without burning profile_attempted; repeated
   queries must not create a new token grant or extend expiry.
3. Refactor S12 discovery into a shared **no-create** validator, with a distinct
   query entry point. Start from server-owned config_dir_fd and the trusted
   account, not broker/dialog paths, HOME, usernames used as path authority or
   a caller's FD integer. Retain filesystem/mount, prefix/mapping, owner/group,
   0700/0600, no-follow/reparse/ACL/link, stored-label/incarnation, full-manifest,
   skeleton and recovery-marker checks. Use existing prefix -> SID lock order,
   nonblocking acquisition and checked directory edges. Query must not seed,
   repair, delete transactions or create `.hive.lock`. It must not acquire an
   unnecessary writable hive descriptor. Missing/unprovisioned profiles deny.
4. Return NULL hkey, managed=1 and the verified nonzero **storage generation**
   in the operation's explicitly documented generation output; return zeros
   on failure. This can retain the current fixed request/reply sizes, but
   update the shared operation enum, wrappers, normal-server denials and tests
   together; audit whether repository protocol-version policy requires a bump.
   Existing LOAD/QUERY/CACHE generation semantics remain unchanged. If paths or
   further identity data are needed by S15, use a separate versioned bounded
   result/bridge rather than overloading this u64 or sending native FD numbers
   to PE code. If the exact account is already loaded, query its held validated
   storage under the manager; foreign/poisoned/conflicting state denies.
5. Build the unchanged LOAD identity record with that exact nonzero generation.
   Keep token_claim_e1s_profile's nonzero precondition (token.c:2926-2931) and
   open_existing's exact comparison (storage.c:748). LOAD reopens/revalidates
   the actual bundle under exclusive ownership; a query-to-load substitution,
   mapping/identity change or generation mismatch denies and consumes setup,
   never silently selects a newer generation. Keep the opened profile/hive
   references and revalidation through save/unload as today.
6. If an S6-side service query is retained instead, it needs an authenticated
   server-delegated pinned prefix descriptor/identity plus account/context,
   request and generations. The current 32-byte account-key-only S6 request
   does not identify a prefix. Use the same native parser under Bob's storage
   credentials; do not let root traverse arbitrary Bob-supplied paths. A
   server-side query avoids that extra delegation and duplicate authority, so
   is preferred. Retire unused S6 query plumbing or leave it unsupported.
7. Test wrong broker/context/thread/token/key/map, missing/zero/malformed
   manifest, symlink/reparse/hardlink/mount/owner/ACL substitution, renamed
   accounts, foreign prefix, interrupted save, lock contention, exact query to
   LOAD generation change, and correct output clearing in both ABIs. Test
   genuinely random generations, including 1 as data, with no special fallback.

This preserves the strongest existing LOAD contract. A server-discovered
generation and its subsequent exact revalidation solve discovery; they do not
repair R1-R11 or establish native same-UID confidentiality.

## Filtered-administrator compatibility verdict

**No direct branch shadowing or same-logon weakening found.** In main.c:506-516
only TokenElevationTypeDefault takes the new branch, and only under its compile
gate/policy. Limited callers continue to the old approve/table-consume/image/
linked-token launch sequence (:1679-1689). Full callers still deny. launch
still requires Primary/Full, caller SID/session and the original caller
AuthenticationId (:1028-1063). It is not changed to accept Alice's distinct
logon. Default production compilation excludes the included e1s18_broker.c
(:1870-1872); normal S10/S13/S6 code denies its private operations.

**Byte-for-byte object identity is unverified here.** Static source reading
cannot certify compiler output against an earlier baseline. The existing
test_broker_e1s18.sh only runs cmp if optional `/tmp/main_prod{64,32}_baseline.o`
files exist (:89-99), then prints identical regardless (:100). That check also
does not compare consent.exe, server binaries, linked images or installed
artifacts. Make baseline presence/hash mandatory for such a claim. The consent
parser/UI has shared changes, so production main-object identity alone is not
a complete behavior regression proof.

With E1S18 enabled, the new unlocked queue, inherited pipe handles and shared
native transport state can affect concurrent work even though a Limited caller
does not take the credential branch. Click consent is not serialized with it
(R2). Thus there is no static basis to promise byte-for-byte unchanged runtime
behavior for mixed enabled traffic. R12 and the lost-reply mismatch are inherited
baseline findings, not evidence that the new branch changed their old behavior.

## Stage-by-stage failure and ownership audit

| Stage | Current static behavior | Failure/ownership verdict |
| --- | --- | --- |
| Admission | Real H2/S3 opener/writer object/version/session checks and checked RevertToSelf (main.c:382-519). Standard branch tests Default, but not the full ordinary Medium/unrestricted/no-linked shape; S10 checks more at token.c:2589-2599. | Old/mismatched binding denies. Negotiate real readiness before prompting; early admission needs the supported standard shape. |
| Parse/table/freeze | Copied bounded input, collision-checked random request ID, held file + local-filesystem lease + SHA-256. | free_request releases normal resources. No absolute cwd validation in parse and no complete request deadline during potentially expensive hashing. Not a PE-validity proof. |
| 1 dialog/queue | Real Win32 spawn, but no initialization/secret submission; unlocked queue never waits. | Fails closed on answer mismatch or timeout, with pipe/deadlock/race issues R2/R8. Wrong/missing account must never become Alice. |
| 2 evidence | OPEN then invalid REDEEM; adapter cannot initialize; absent provider. | No live identity proof. Broker storage denial fallback is fail-open (R3), despite earlier fail-closed blockers. |
| 3 token | Real export calls and independent partial token checks. | NEGOTIATE currently fails local wrapper validation; fixing its argument exposes the uninstalled-context denial/burn. No authenticated producer (R10); malformed response hardening R15. Success token handle leaks later (R7). |
| 4 LOAD | Original context and exact key/map/storage record sent; retained context used for S14b. Server anchors/locks/validates and publishes exact-SID managed hive. | Server failure rolls back new load state (registry.c:3198-3214). Broker context closes on failed call. State-1 crash/busy cleanup remains unowned (R7). |
| 5 prep | Real function call with fixture inputs, disabled/invalid descriptors in actual PE path. | Denies today. Sanitized output is not usable launch proof (R11); Unix allocation ownership leaks. |
| 6 image verify | Reopens path, compares held/reopened file identity and hash (main.c:642-661). | Observed replacement denies. Accepted final path-open race remains; no installed-image verification after CREATE. |
| 7 CREATE suspended | Explicit token, FALSE inherited handles for target, designated Bob parent, NULL supplied object SDs. | Server applies elevated descriptors. No immediate binding/stop recheck here, explicit desktop or durable supervisor; validated cwd/env misuse R11. |
| 8 child verify | Checks child SID and token session only. | Mismatch attempts termination; not proof of full auth/logon/process session/image. S14b subsequently strengthens token/initial-suspension checks. |
| 9 image arm | Real arm call when lease nonzero. | Missing export succeeds; no guard transfer (R6). |
| 10 caller handle | Duplicates only non-inheritable wait/limited-query handle into retained caller object. | No token/profile handle deliberately returned. Rollback closes source in caller table, but discards failure (R7). |
| 11 profile COMMIT | Real broker-only S14b operation with original context and exact issued token/child. | Strong local identity/never-resumed gate; all mutation follows validation. Not fresh native authorization or resume (R5). |
| 12/13 final/resume | Caller checks, stop/lease event, broker-local table lock, one ResumeThread invocation. | No complete deadline/policy/account/endpoint/state validation; caller mutation can interleave server calls. Checks only -1, not expected initial suspend count. |
| Success/reply | Returns PID/TID/limited handle; frees normal request. | Leaks token/Unix env and releases image lease early. Lost reply leaves target running (R6/R7). |
| Failure after child/load | Attempts remote close, terminate/wait, unload, token/context close. | Attempts are not confirmed cleanup. Busy/poisoned hives and broker death require exact supervised ownership (R7); intentional retained live hives must be distinguished from leaks. |

Image TOCTOU accepted by the owner remains: verify_image closes its path reopen
before CreateProcessAsUserW reopens req->image. A parent rename, symlink or mount
swap can change the final path after the last check; loaded DLLs, interpreters
and arguments remain outside the held main-image proof (E1_DECISIONS.md:20-28).
No descriptor-bound PE loading is present. The new code's child verification
does not inspect its loaded image; describing that stage as an 'installed image
identity check' is inaccurate. Preserve and disclose this accepted limit;
R6's missing through-exit guard and R11's discarded cwd are additional defects.

## Required live evidence and release gate

Nothing below was run. Use an owned disposable guest, dummy credentials and the
existing shared fixture refusal/cleanup rules. Do not use real host passwords,
shared-VM resets, foreign fixture deletion or weaker checks to get a positive
control. The following must supplement the full E1-S section 8 matrix:

1. **Unfaked positive path first:** current real consent.exe, actual private
   channels, root worker/PAM stack, S8 ready/redemption, authenticated S10
   installation, S12 discovery, S14b LOAD/COMMIT and guarded CREATE/resume.
   Both ABIs and cross-ABI caller/target combinations. Record exact loaded
   binary/server/prefix/protocol identities, never infer success from native
   boundary fakes or a standalone PAM test.
2. **Credential independence:** wrong/empty/Alice/Carol/Eve/unknown/locked/expired
   selections, no stored-password fallback, canonicalization/selection changes,
   multi-round/unsupported MFA, crash/cancel/late success. Canary scans across
   argv, environment, bus, logs, journal, memory/crash artifacts and other Wine
   processes; document toolkit/PAM/swap limitations.
3. **Authority and replay:** exact evidence/request/attempt/account/reference
   swaps, all generations and endpoint ownership changes, equal numeric session
   across servers/prefixes, token replacement/ModifiedId mutation, no reused
   nonce, expiry during slow redemption/setup and at resume. More than 64
   successes/failures and concurrent requests; broker/native restart must not
   reset native failure quotas. Retain current S8 atomic burn-before-compare
   and global/caller/account throttle controls with real adapter coverage.
4. **Concurrency/DoS:** simultaneous enqueue/release/cancel, real FIFO activation,
   mixed click/credentials, multiple sessions, per-logon/session/global limits,
   pipe writer inheritance, hung dialog/PAM/NSS, nonreading clients, stop during
   flush/handshake and native global transport replacement. Verify no UAF/list
   corruption, bounded loop delays or permanent slot exhaustion.
5. **Every resource boundary:** inject failure/death after evidence consumption,
   issue, load, prep, CREATE, token check, arm, guard creation, handle duplication,
   S14b commit and resume. Track process/native death, remote handles, broker
   token count, FDs/watches, keys, hives, exact leases/locks and environment
   allocations. A timeout without confirmed cleanup is not PASS. Cover busy
   and poisoned/save-failed hives without forcing unload of legitimate users.
6. **Bob-to-Alice isolation:** direct OpenProcess/Thread/Token, VM_WRITE,
   SET_CONTEXT, DUP_HANDLE, impersonation/assignment, raw server requests,
   debug-parent/debug-object and job inheritance, managed HKU/HKCU/classes and
   file opens including pre-load/imported handles. Measure actual native
   UID/GID/groups/dumpability and harmless ptrace/procfs/process_vm probes;
   separately demonstrate Bob-owned hive native access as the documented limit.
7. **Integrity/lifecycle:** parent-path/mount/image replacement before/after
   verification and reply, retained through-exit lease, descendants and two
   Alice logons sharing one profile, source/hive/manifest substitutions,
   recovery/save faults, stop/crash and lost replies before/after commit. Show
   no implicit retry and termination before reporting a started launch failed.
8. **Regression/shipping:** required hashed production baselines for both main
   objects plus consent/server/runtime artifacts, no test feeder/callback/hook
   reachable in shipping images, normal-server denial of all private operations,
   existing filtered-admin same-logon approve/cancel/provenance/integrity tests,
   and real secure-desktop input on the selected display backends.

E1-S section 9 still gates account/group/local-provider policy, supported
distro/PAM/filesystem/display matrix, authentication/lockout/audit choices and
accepted containment limits. Wine-only host identity and lost-reply termination
are already decided. Current code/source inconsistencies are implementation
defects, not reasons to weaken those decisions or ask for a new authentication
authority. Keep the entire credential chain disabled until the defects,
supervisor ownership, selected release choices and live acceptance are resolved.
