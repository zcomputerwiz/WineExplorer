<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S18e R8 design review

2026-11-09. Verdict: agree with the direction, with the blocking corrections below before implementation.
Read docs/TOOLING.md and CLAUDE.md first; reviewed S1 records 2/7, endpoint establishment, earlier review
item 5, and the named code plus its immediate broker/supervisor/ABI callers.
Static review only: no code changes, builds, generators, PAM, D-Bus, VM or runtime tests. Keep E1-S disabled.
The stub/empty-Confirm diagnosis is correct (src/dlls/wexhost/e1s6.c:500-537; src/programs/consent/consent.c:780-811).

## Answers to the three questions

1. **D-Bus delivery is sound conditionally; prefer it initially.** Use consent's own private system-bus
   connection, authenticated against the server-retained dialog/native identity and live kernel pin.
   Bind the unique sender, UID, complete transaction and one-shot attach state; authenticate the reply's
   protected verifier owner/generation too. Then prove the delivered socket's actual worker sender as
   described below. D-Bus carries metadata/FDs only. Direct server transfer adds server FD ownership
   machinery without removing authentication/revocation. PID equality needs a correctly acquired live pin.
2. **No: image plus parentage is necessary, insufficient.** Under the accepted native same-UID exclusion,
   a first-main-image inode check is a useful gate, not a transaction registration. Retain actual creator,
   process, primary/security version, native endpoint/pin, session/desktop and full request binding.
   Protect process/thread/channel access from ordinary Wine clients. Do not infer creator authority from
   a designated parent, a handle's possession, a command-line path or a numerical Unix PID.
3. **Prefer two anonymous pipes**, pre-created and inherited through the exact handle list: child read
   end for initialization/results, child write end for selection/ACK/Confirm/Cancel. Broker ends must be
   non-inheritable; close all redundant copies after creation and check every handle operation.
   Use explicit bounded framing and cancellable I/O workers drained concurrently. No public name or
   connect race is needed. A named instance is reasonable if overlapped I/O materially simplifies this,
   but needs exclusive first-instance creation, exact client/process binding and replacement refusal;
   a SYSTEM-only DACL admits other SYSTEM processes and cannot supply that binding by itself.

## Missing or wrong

1. **High — the proposed endorsement still lacks its authority contract.**
   docs/E1_S18E_R8_DESIGN.md:30-34 omits primary/security-version retention and complete transaction binding.
   wine/server/sysrun.c:539-542 copies the supplied child field unchanged; the current checks at :485-511
   authenticate the broker/verifier, not that child. tools/e1s6/e1s6-lab.py:418-438 then accepts a matching
   HELLO and issues role 3. Replace this with a typed process-handle lookup in the registered broker,
   a retained actual-creator relation, protected first main image, SYSTEM primary/object/version,
   trusted session/desktop objects, and request/attempt/nonce/digests/channel/deadline/generations.
   Revalidate these authorities at attach and later use; serialized kind 7 alone grants nothing
   (docs/E1_S1_CONTRACTS.md:37-43). Keep the S3 caller comparisons at wine/server/named_pipe.c:1811-1821.
2. **High — a process object's Unix PID is not yet the required kernel proof.**
   docs/E1_S18E_R8_DESIGN.md:34 must specify pin acquisition and linkage to that exact Wine process.
   wine/server/thread.c:1755 accepts req->unix_pid; wine/server/process.c:661-669 separately records
   connection credentials. Do not simply pidfd_open a client-reported number. Establish the actual
   native endpoint with kernel credentials/pidfd on a process-bound handshake and retain that identity.
   Copy R13's first-image-only semantics and both architecture paths (wine/server/sysrun.c:311-355),
   not just stat-by-path or its token-ID-only check (:329-335,408-411); R8 needs security-version checks too.
   An ordinary Wine process mapping consent later or claiming broker parentage must remain unauthorized.
3. **High — mutual authentication and exclusive FD ownership are missing.**
   docs/E1_S18E_R8_DESIGN.md:35-45 authenticates answers only. Consent must authenticate the worker's
   per-message PID/UID and kernel pin from the protected supervisor's bound worker registration;
   SO_PEERCRED can describe the socketpair creator, not the eventual worker recipient.
   Preserve tools/e1s7/pam-worker.c:139-158 checks for exactly one credential and pin, UID, live registered
   identity, truncation and unexpected ancillary rejection; verify the received pin identifies that sender.
   Set credential options before any peer sends; close unwanted FDs, use CLOEXEC, and close Python's copies of BOTH ends before
   secret input. Add a nonsecret worker-ready/delegation ACK barrier. No Python watch/recv on secret FDs.
   The earlier requirement is explicit at docs/E1_S18E_DESIGN_REVIEW.md:196-202.
4. **High — wrong-peer attach must not cancel somebody else's request.**
   docs/E1_S18E_R8_DESIGN.md:40 makes a guessed/publicly observed request/channel a denial-of-service
   capability: another process can cancel the real prompt. Reject unauthorized senders without mutating
   the victim. Only after authenticating the registered dialog should duplicate/late/out-of-order calls
   be terminal. Atomically reserve one attach, bind its unique bus owner, bound quotas, and cancel on
   failed/lost transfer without retry/reassignment. Monitor that dialog owner as well as broker/verifier;
   tools/e1s6/e1s6-lab.py:600-605 currently watches the verifier and broker owners only.
5. **High — bootstrap ordering can still deadlock or collect credentials too early.**
   docs/E1_S18E_R8_DESIGN.md:24 writes initialization before resuming the reader; a bounded display record
   can still exceed pipe capacity. Queue it to a cancellable writer, resume after registration, then
   receive the exact initialization ACK. Complete attach, worker authentication and delegation readiness
   before enabling password input. Never synchronously wait for attach/ACK while the dialog is suspended.
   Retain one admission deadline on the shared native clock, including queue/setup/PAM/confirmation;
   tools/e1s6/e1s6-lab.py:290 still starts a five-second setup deadline. Specify every state and timeout.
6. **High — account selection, PAM completion and final-account confirmation need actual messages.**
   docs/E1_S18E_R8_DESIGN.md:42-48 skips how selection reaches the verifier, how consent learns PAM is
   complete, and how canonical identity reaches both broker and dialog. Current consent only stores the
   account locally (src/programs/consent/consent.c:780-802); the broker merely validates out_account
   without filling it (src/programs/wexelevate/e1s18_broker.c:772-780). Worker results go to supervisor
   control, not the dialog (tools/e1s7/pam-worker.c:591-595). Define bound nonsecret selection/result/final
   account challenge messages, then empty-payload Confirm tied to that exact retained account/challenge.
   No password in those messages. Keep PAM_USER changes denied initially (:556-558); supporting them
   requires fresh S4 resolution/eligibility and a new explicit final-account confirmation, not a relabel.
7. **Medium — S1 kind numbers and wire translation are ambiguous.**
   docs/E1_S18E_R8_DESIGN.md:42 calls conversation text "S1 kind 1"; it is record kind 2,
   answer_kind=1. Confirm/Cancel are the same record kind 2 with answer_kind=2/3 and empty payload
   (docs/E1_S1_CONTRACTS.md:27). Specify exact version/length/caps/reserved checks, UTF-8 without NUL,
   round/index/style expectations and rejection of extra/missing/replayed packets. S7 currently binds
   request/attempt/nonce only (tools/e1s7/pam-worker.c:161-166; tools/e1s7/protocol.h:7-24).
   Freeze channel/digests plus authoritative generations/deadline in authenticated setup, or carry the
   complete S1 answer. Keep kind 7 unchanged; its missing request/attempt fields need a private bound
   envelope/retained state. Allocate fresh per-attempt delegation state and invalidate the previous attempt.
8. **High — do not turn the normalized S7 flow into arbitrary PAM prompt forwarding.**
   docs/E1_S18E_R8_DESIGN.md:44 overstates the normal worker: tools/e1s7/pam-worker.c:276-307,528
   presents one fixed password prompt before backend lookup and privately answers one echo-off prompt;
   raw conversations are test-only (:445-450). Preserve decoy behavior, fixed release timing, limits,
   fixed service, PAM_DISALLOW_NULL_AUTHTOK and pam_acct_mgmt (:548-555). General MFA/raw prompts need
   separate policy/design; backend-dependent transcripts must not disclose account validity.
   "Rejects null passwords" (design :17-18) means the PAM null-authtok flag, not a wire-level nonempty check.
9. **High — cleanup needs explicit owners and a success/retirement boundary.**
   docs/E1_S18E_R8_DESIGN.md:53-54 omits caller/pipe/lease/stop loss, token/route/generation change,
   replacement and partial-transfer failures. Give each pending FD, pin, watch, child and worker one owner;
   remove watches before freeing, make cancellation terminal, and terminate/reap only owned processes.
   Keep consent alive until the broker's terminal completion/cancellation ACK; its current EndDialog/exit
   (src/programs/consent/consent.c:810-811,925) must not revoke authentication before redemption/use.
   Adapt native-stub kill/waitpid cleanup (src/dlls/wexhost/e1s6.c:269-279) to the real protected Wine child.
   Keep one request's channels throughout; current global transport state (:55-66,577-580) requires full
   lifetime serialization or request-owned objects. No second OPEN or early CLOSE to rotate registration.
10. **Medium — hygiene and the implementation inventory need concrete acceptance.**
    docs/E1_S18E_R8_DESIGN.md:49-51 must apply/check non-dumpability before credential input, cover cancel,
    window-close and transport failure, suppress edit undo/history and copy/cut, and wipe all owned copies.
    src/programs/consent/consent.c:66,780 uses 256 WCHARs: detect overlength and UTF-8 conversion errors,
    reject rather than silently truncate, enforce the 4096-byte wire limit. Never log prompts/passwords.
    The file list at design :65-68 also needs supervisor.py, D-Bus policy (currently only Open is allowed:
    tools/e1s6/org.wineexplorer.E1S6Lab.conf:9-10), PE exports/Unix ABI headers and both native/WoW64 tables
    (src/dlls/wexhost/unixlib.c:2591,2689), server retained-state lifecycle/protocol artifacts as needed,
    and docs/STOCK_CHANGES.md. Preserve normal-build NOT_SUPPORTED and separate shipping R13 behavior.

## Incremental build/test order and delegation

Each item is a separate change/test gate. "Mechanical" means a weaker agent with frozen layouts/state
tables, exact file allowlist, failure behavior and named tests. Careful steps need a security-capable implementer.
Build changed pieces in WoW64 at each gate; regenerate immediately for protocol changes. Use fake peers until integration.

1. **Careful:** settle the immutable registration/attempt tuple, state/ownership tables, startup barriers,
   account/result/confirmation exchange and cancellation precedence. Verify readiness before any password.
2. **Mechanical after 1:** implement only pure frame codecs and malformed/replay test vectors: exact lengths,
   enums, reserved/caps, UTF-8 and 4096-byte bounds. Do not modify S1 kinds or evidence/token formats.
3. **Careful:** add server-retained consent identity/creator/native-pin endorsement, reusing R13 image gating
   without broadening the broker allowlist. Test foreign handles, spoofed parent/PID, wrong image, second
   mapped image, token replacement/mutation, session/desktop mismatch, child death and both architectures.
4. **Mechanical after 1:** replace the broker pipe with two explicit-list pipes and INIT/ACK handling only;
   test oversized/partial initialization, blocked writers, broken pipes, cancellation and unrelated handles.
5. **Careful:** wire DialogAttach and verifier/worker mutual authentication plus one-shot FD delegation;
   test wrong sender without victim cancellation, duplicate attach, owner loss, pin substitution, FD passing,
   truncation, missing credentials, lost replies and worker readiness. Verify Python closes both duplicates.
6. **Mechanical after 1/5:** add approved fixed-width PE/Unix wrappers, export/spec/policy changes and both
   ABI table entries; test normal-build denial and x86/x64 parameter/handle-width round trips.
7. **Mechanical after 1/5:** implement the specified consent UI states, UTF-8 conversion and secret wiping;
   test overlength input, every close/cancel/error exit, clipboard/undo suppression and checked dump policy.
8. **Careful:** join selection, normalized PAM, authenticated results and final Confirm on the retained
   transaction; test correct/wrong/empty typed passwords, decoys, unsupported prompts and redirection denial.
   Keep token issuance/launch disabled while testing authentication independently.
9. **Careful:** exercise full lifecycle/cleanup and cancellation races at every transition, including live
   pins with EOF, caller/broker/dialog death, service restart, stop/lease loss and deadline expiry; prove no
   surviving owned child/worker/FD or late success. Re-review before joining the separate R10 ACK/R5/R7 work.
10. **Mechanical execution of an approved matrix:** regenerate affected protocol artifacts, update the stock
    change inventory, build normal and gated WoW64 configurations, and run regression/negative/secret tests
    in an owned clean S6 fixture with S7 identities. Refuse foreign fixture state; never reset shared VM B.
    R8 success is not whole-chain acceptance or permission to enable E1-S. None of these tests ran here.
