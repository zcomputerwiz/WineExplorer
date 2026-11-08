# E1-S18e design review

Reviewed 2026-11-08 against the working tree, including existing uncommitted broker changes.
Read TOOLING.md and CLAUDE.md first, then the review, remediation, R10 design, decisions and code.
S1 below means docs/E1_S1_CONTRACTS.md. This is static review: no code changes, builds,
generators, services, VMs or runtime probes. Historical tests are not new acceptance evidence.
Keep the standard-user chain disabled. All five directions need the corrections below.

1. R10 ordering: agree with changes.

NEGOTIATE must precede redemption but follow S6 registration: the server requires its generation
and retained primary (wine/server/token.c:2545-2564,3017-3029). Stage 2 currently opens,
redeems and closes S6, and Stage 3 negotiates (src/programs/wexelevate/e1s18_broker.c:745-776,1094-1107).
Simply moving negotiation ahead of that OPEN will fail.

Establish authenticated readiness before collecting credentials; retain that transaction through
dialog registration, PAM, collection, negotiation, redemption, installation and final checks.
Negotiation may happen earlier once registration exists; it allocates an empty context, not
authorization. Keep it on the thread that will ISSUE; close it on every failure, even before Stage 3.

Do not OPEN another connection between NEGOTIATE and REDEEM: an endorsement
with expected generation zero replaces the process's broker generation
(wine/server/sysrun.c:463-479,509-516). Define stable broker registration versus
per-request connection identity explicitly. Concurrent requests must not replace
each other's authority or channels: wexhost currently has one global active
channel and pipe (src/dlls/wexhost/e1s6.c:57-66,572-580). Either retain explicit
per-request transport objects or serialize their entire lifetimes initially.
Retain the original admission deadline, including queue time; the native
transaction's current five-second setup deadline is not that authority
(tools/e1s6/e1s6-lab.py:281-289).

2. R10 receipt: agree with changes; reuse the control socket initially.

An authenticated verifier-to-server receipt is the right producer. Keep ISSUE's byte comparison,
fresh S4 lookup and burn-before-validation (wine/server/token.c:2740-2759). Never expose TEST_INSTALL.
The receipt is a versioned private envelope around unchanged S1 evidence, not another S1 record
or portable grant. Require exact framing, bounded canonical name, nonzero collision-checked ID,
zero reserved fields, negotiated capabilities, exact operation and transaction sequence.
Generate and bind the receipt ID before publishing the REDEEM endorsement.

Bind more than request ID/reference/generations. During the broker's endorsed
REDEEM dispatch, retain the actual consumed S3 opener/writer/primary objects,
their original security versions, session, pipe connection, registered route,
frozen request/binding digests, nonce, exact attempt, evidence reference and
original deadline. The S3 objects are available where endorsement is called
(wine/server/named_pipe.c:1795-1821); pass them internally rather than reopen a
PID or accept object identities from the receipt. Installation currently
snapshots token versions at receipt time (wine/server/token.c:2682-2687).
Compare against the original retained versions first, so a mutation before
installation cannot become the new accepted baseline.

Compare every field of the exact 220-byte kind 3 with that frozen expectation
and current authorities. Check method/service, capability mask, completion and
expiry, with completed <= now < expiry <= original deadline. The administrator
UID is the post-PAM mapped UID, not the caller UID. Independently resolve the
canonical name/key/UID/map/eligibility relation through S4. Take session from
the retained caller, since kind 3 contains none. Build kind 4 from these checked
values, preserving the exact attempt and generations; keep verifier generation,
method and other evidence-only fields in the validator's retained state.
The broker must receive the canonical result too: changing only the server's
name leaves its launch preparation using dialog selection text
(src/programs/wexelevate/e1s18_broker.c:1034-1035,1473).

Socket reuse is acceptable within the stated native same-UID exclusion. SOCK_SEQPACKET is duplex;
opposite-direction endorsements and receipts do not compete for the same receive queue.
The broker's duplicate can steal or shut down receipts, but cannot forge root sender credentials.
A dedicated socket is unnecessary for authenticity. Use one transaction per control endpoint,
one server reader and explicit state/sequence dispatch; never several context watchers on
duplicated ends of one receive queue. A dedicated server-owned endpoint is useful if multiplexing
or descriptor ownership makes this hard, but does not solve freshness or revocation itself.

Enable SO_PASSCRED before the verifier can send. Use recvmsg with MSG_DONTWAIT and
MSG_CMSG_CLOEXEC; reject truncation, duplicate/missing credentials, unexpected ancillary data
and extra packets; close received descriptors on every exit. Require kernel uid 0 and the
registered verifier PID while both peer pins remain live. Bind them to the protected verifier
registration, not any root socket; preserve same-peer checks (wine/server/sysrun.c:436-453).
Use per-call nonblocking flags: changing shared file-description flags affects the broker too.

The proposed validator is insufficient: live pins and unchanged stored numbers do not prove
current authority. A live verifier can lose its bus name, cancel a transaction, lose its dialog
or change policy. Retain a revocable transaction owner with authenticated invalidation on
ownership/generation change, EOF/HUP/error, cancellation, caller/pipe loss and either expiry/deadline.
Both callback and validator must observe terminal state; queued revocation must win over
ISSUE regardless of fd/request dispatch order. Synchronize authoritative map/policy updates
with use: invalidate affected server owners before acknowledging the new generation as usable.
An eventually delivered notification alone is not a final atomic authorization check.
Define the ordering and deny when freshness cannot be established without blocking the server.

This owner must survive evidence removal. Service.redeem burns the evidence
before comparing it, and _stop clears its reference/record and terminates its
worker (tools/e1s6/evidence.py:242-254,470-489); its ordinary tick skips terminal
requests (:503-510). Create separate consumed-transaction state, not a revived
evidence reference. Send a receipt only after redeem returns successfully,
including its final audit, authority and cancellation checks. A send, install,
acknowledgment or broker-reply failure ends this attempt permanently. Keep
revocation available through LOAD and guarded commit; the current Stage 2
CLOSE would otherwise revoke the new authority immediately. Closing a socket
does not kill its peer's pidfd (tools/e1s6/e1s6-lab.py:299-332).

The callback needs a different internal entry point: token_install_e1s_consumed and
e1s_context_current dereference current, which is only set during request dispatch
(wine/server/token.c:2558-2564,2992-2999; wine/server/request.c:97,276,303).
Resolve/check the context during endorsed REDEEM; install through retained typed objects
and explicit owner checks. Never borrow current or treat a handle number as authority.
Preserve owner-thread checks on later broker operations.

Pending fd/pins need cleanup even if installation never succeeds. On failure the producer
owns its cookie; on success the context releases it once (wine/server/e1s_token.h:21-23).
Remove watchers/timers before freeing objects, avoid reference cycles, and cancel pending
work on broker thread/process death or handle closure. Bound contexts, packets and sends.
The callback/validator must cover request/image/route/cancellation, not just pidfds
(wine/server/e1s_token.h:10-23). R5's final guarded-resume work remains required.

3. R10 arrival ordering: agree with changes; prefer an installation ACK.

A read-only status query is sound if it distinguishes pending, installed and
terminal failure, verifies the original broker/context/thread, and never burns
or extends the request. The current handler burns on unknown operations, so
add an explicit query branch rather than probing ISSUE
(wine/server/token.c:3034-3048). Poll only in the broker, with bounded short
waits that also observe cancellation/death/stop and the original deadline.
Installed is advisory: ISSUE must still validate and may deny if revocation
wins after the query. Never retry ISSUE, including after a lost reply.

Prefer an installation ACK: the server sends a transaction-bound authenticated ACK over
control, and the verifier releases successful REDEEM data only after accepting it.
Handle receipt/ACK asynchronously with bounded queues and the original deadline; neither
event loop blocks waiting for the other. Reuse S6 server credential/process authentication
(tools/e1s6/e1s6-lab.py:343-378). A failed/lost ACK burns/cancels rather than re-redeems.
This orders the successful reply after installation and leaves ISSUE single-shot.
A status query is a reasonable fallback; implement one approach initially.

4. R13 shipping filtered-admin creation: agree with changes.

A narrow authorized flag is preferable to rejecting all debugged/jobbed callers or documenting
their control over the target. The broker designates the caller as parent
(src/programs/wexelevate/main.c:1100-1123); the server imports its job/debug object
(wine/server/process.c:1374-1385,1421-1430). Preserve filtered-admin same-logon linked tokens;
do not require S10 evidence here or restore caller parentage in the standard-user path.

Validate the flag in the server before allocation or breakaway checks, using
the actual endorsed broker process/primary/security version with no thread
impersonation. Bind its use to an authorized suspended elevation creation,
explicit target token, retained caller and validated session/desktop. Reject
unauthorized callers and incompatible explicit debugger, job-list or inherited
handle inputs; do not silently ignore the flag. Skip both caller-job inheritance
and its breakaway prerequisite (wine/server/process.c:1241-1248), and all debug
attachment on this operation. Keep ordinary CreateProcess behavior unchanged.
NO_DEBUG_INHERIT alone does not fix this source: the parent->debug_children
branch does not test that flag. Post-creation detach is too late.

There is a shipping prerequisite: S6's image endorsement and transport are
currently test-gated (wine/server/sysrun.c:319-332,403; sysrun.h:58-59).
Extract/establish the minimal protected broker identity gate for production
without enabling the standard-user/PAM chain or allowing test broker images
in the shipping allowlist. Do not substitute SYSTEM SID alone, nor make the
shipping fix depend on the lab verifier being installed. If that gate cannot
yet be shipped, a server-atomic refusal of unsafe parent contexts is an interim
fail-closed measure, not a completed flag implementation.

Keep parent attribution and required console/window routing, but audit which
other properties are imported: create_process also copies affinity and may
copy handles, and startup processing duplicates console/standard handles
(wine/server/process.c:719-728,1400-1418). Allow only the intended placement and
explicitly permitted console resources; no caller debug/job control or general
handle inheritance. Carry the private flag through both ntdll ABIs, allocate
it without collision, regenerate affected protocol artifacts as required, and
list every stock-Wine change in docs/STOCK_CHANGES.md, per CLAUDE.md:17.

Microsoft documents that the designated parent supplies the job, while the debug port comes
from the creating process; Wine's designated-parent debug behavior differs here. Its UAC
overview identifies AppInfo as the elevation service. I do not know its exact job/debug
policy across Windows versions with enough confidence to claim UAC always escapes caller jobs.
These sources do not establish that: [parent attributes](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-updateprocthreadattribute),
[UAC architecture](https://learn.microsoft.com/en-us/windows-server/security/user-account-control/how-user-account-control-works).

5. R8 password channel: agree with changes.

Use a private per-attempt AF_UNIX SOCK_SEQPACKET channel between consent's in-process
wexhost Unix side and the pinned S7 worker. Local plaintext fits the accepted threat model;
encryption would not authenticate the dialog. The broker gets nonsecret selection/Confirm/Cancel;
Python handles orchestration/results, never password packets. Consent currently sends only
Confirm and exits (src/programs/consent/consent.c:780-811); S6's child is a forked echo stub,
not consent.exe (src/dlls/wexhost/e1s6.c:490-540).

Register the real protected consent process object, Unix process connection,
primary/security version, session/desktop, request, attempt, nonce, digests and
channel with wineserver. The verifier must receive a server endorsement tying
that exact native endpoint and live kernel pin to the registered child and
broker transaction. A HELLO with the same UID, a supplied PID, inherited FD,
parent claim or possession of the channel ID is insufficient. wexhost's Unix
call executes in the loading process (src/dlls/wexhost/wexhost.c:291-299);
authenticate that process, not a generic name "wexhost". An out-of-process
shared relay would need a different per-dialog proof and must not relay secrets.

The verifier creates/delegates the pair: one end only to its pinned worker, the other directly
to the dialog over its authenticated registration channel. The broker never retains an end.
Close Python's duplicates before secret input; use CLOEXEC and explicit transfer lists.
On every answer verify SCM_CREDENTIALS PID/UID and kernel pin identity against the endorsed
dialog while its retained pin lives, preserving tools/e1s7/pam-worker.c:123-158,480-490.
The dialog authenticates the worker using the root supervisor's bound registration/pin and
per-message credentials. SO_PEERCRED alone may identify the creator rather than FD recipient.

Preserve separate kind 1 secret answers and kind 2/3 broker confirmation.
Bind the private S7 framing to the complete S1 transaction; its current header
contains request/attempt/nonce/round/index/style but lacks channel and digests
(tools/e1s7/pam-worker.c:108-117; tools/e1s7/protocol.h:7-23). Either carry S1
answers or specify an authenticated immutable setup binding that makes the
translation exact. Never put password payloads on the broker pipe or D-Bus.
Keep the dialog alive through PAM and final bound confirmation; its death must
cancel pending authentication. Drain channels while waiting; remove the
FlushFileBuffers/exit/read dependency. Check exact writes, ACK initialization,
and treat extra, late or out-of-order messages as terminal failures.

Wipe owned UTF-16/UTF-8/edit-control/transport buffers on every exit, suppress dumps where
supported and disable clipboard/history. Preserve bounded conversation, fixed PAM service,
null-token rejection and account management; deny unsupported styles and unconfirmed PAM_USER
redirection (tools/e1s7/pam-worker.c:276-307,552-558). Do not promise erasure of kernel buffers
or PAM-module copies. Keep supervisor termination/reaping and the original deadline on every failure.

Contract conflicts and build order.

The corrected designs preserve E1_DECISIONS.md:10-18,31-38,107-111: broker authorization,
visible denial/cleanup, secure-desktop default, no retry after commit and Bob's host UID.
A receipt never permits independent verifier/server launching. The abbreviated validator,
early CLOSE, synthetic deadline/generations and numeric dialog identification conflict with
S1:15,25,29,41-43,53-60. Correct them without weakening filtered-admin same-logon checks.

Keep S1 kind 3 exactly 220 bytes and kind 4 exactly 188, with evidence_ref as
the existing separate 16-byte transport envelope (wine/include/wine/wex_token.h:13-14).
Receipt ID, canonical name, ACK and verifier lifecycle state belong outside
those records. Readiness must cover all ten launch capabilities, not just the
isolated S10 mask 0xc0 (S1:11-17). Current abbreviated broker digests and literal
attempt/generations, plus the lab validator/policy generation, remain blockers
(src/programs/wexelevate/e1s18_broker.c:1121-1135,1854-1879;
tools/e1s6/e1s6-lab.py:231-232,449-465). Receipt wiring must not bless them.

Build independent shipping R13 identity/isolation first. For disabled E1-S, establish canonical
S1 bindings, one native clock/deadline, stable registrations, request-owned channels and
revocation; then real dialog registration/duplex initialization and R8 delegation/PAM;
then move NEGOTIATE and install consumed receipts with ACK or the chosen status query.
Finish R5 final atomic authorization and R7 cleanup before positive launch acceptance.
R14 and other open findings still gate enablement; preserve normal-build denial throughout.

Acceptance needs both ABIs/normal builds; substituted dialogs/FDs/senders; wrong/empty typed
passwords despite fixture files; every receipt mismatch/replay; both arrival orders; policy/bus
loss with live verifier; queued cancel versus ISSUE/commit; EOF with live pins; deadline expiry;
allocation/send/ACK failures; broker/thread death and fd cleanup; nested jobs/debuggers and
kill-on-close under real designated-parent creation; console/window regression. None was run here.
