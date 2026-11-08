<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1 decisions (2026-11-06)

Owner decisions and the first-release design for Track E1 (production elevation). Background and the full comparison are in
`E1_ARCHITECTURE_DISCUSSION.md` (static review by Codex gpt-6.1-sol, checked against `src/programs/wexelevate/main.c` by hand).

## Decided by the owner

1. **Authority: the broker.** `wexelevate` (root-owned machine root, started by the system-role launcher) owns the authorization
   decision, request freezing, consent and use of the linked full token. wexhost stays plumbing (image lease). A host-side polkit
   authority is reconsidered when standard-user credentials are built; polkit may then supply an authenticated administrator
   identity as an input to the broker, not take over the decision.
2. **First release: click consent on the secure desktop for filtered administrators.** Standard users (over-the-shoulder
   credentials with an authenticated administrator identity) are the next goal, not part of the first release. A standard user gets
   a visible "not supported" error.
3. **Failure default: deny loudly.** Any failure (timeout, cancel, prompt crash, integrity break, setup failure) shows an error
   and leaves no process, handle or prompt behind.
4. **Presentation: best effort with documented limits** (same-user native X11 clients, Wayland compositor isolation).
5. **Request integrity: best effort and simple.** Identify the image (path, SHA-256, signature status if any), hold it open with
   write and delete denied plus the existing host read lease through consent and launch, and re-check identity and hash right
   before launch. Scripts, interpreters and loaded DLLs are a stated limit. The final path-based open by Wine is a stated residual
   race (parent rename, symlink or mount swap after the last check).

## Defaults taken for the open questions of the discussion (owner may change)

* Residual main-image path race: accepted for the first release. Closing it needs descriptor-bound PE loading or protected
  staging (later work).
* Image protection lasts until the target exits (current lease design); a break ends the target; service stop drops protection
  (stated limit).
* Commit contract: before the launch commit everything rolls back; after it nothing is retried. A lost reply after a started
  process is reported as failure to the client only if the process was terminated first (see the work package below).
* Executable sources: validated local filesystems only; NFS, 9p and unknown mounts are denied for the first release.
* Unsigned and unknown publishers: click consent with an accurate label; no signer-based automatic approval; an invalid
  signature is labelled as such. Read or binding failures deny.
* Environment: default environment of the target user; validated absolute working directory; private Temp chain created by the
  broker from the token-derived block only (done).
* Secure desktop policy 0 (ordinary desktop) stays as the documented, insecure administrator option (as on Windows); the default is the secure desktop.

## Work packages

* **E1-H1 (broker hardening, `main.c`).** Create the target suspended; resume only after the handle duplicate, lease arm and
  guard are in place; on any failure terminate it, wait for it, close the remote handle with `DUPLICATE_CLOSE_SOURCE`. Remove the
  `CloseHandle` of the caller's handle value in the broker's own table (`client_thread`). Check `stop_event` again right before
  launch. Deny when random request ids cannot be generated. Test: build, lab guest run (approve and deny paths), no survivor.
* **E1-H2 (caller binding).** Compare logon and session identity as well as the user SID; pin the pipe client process before
  trusting the PID. Static review first.
* **E1-H3 (policy narrowing).** Serialize prompts (one consent window per
  session); deny unvalidated mounts for the held image.
* **E1-T (tests).** Extend `scripts/test_sysrun_server.sh` and the lab fixtures: caller death, prompt crash, broker stop during
  consent, image change during consent (rename, rewrite), existing writer, rejected request leaves no child.
* **E1-S (standard users, later).** Decide how an authenticated administrator identity reaches the broker (broker keeps policy).

## Status

* **E1-S14 disabled implementation (2026-11-07), static only.** Server-owned
  managed-profile leases, stable SID locks, checked durable save/unload,
  generation-aware proxy roots and fail-closed interrupted-state detection are
  implemented in [E1_S14_LEASES.md](E1_S14_LEASES.md). Protocol is 979 (existing
  request 325). Both-ABI/normal/crash tests are extended but unrun; no build or
  runtime acceptance is claimed. S18 still owns authenticated transfer/rollback
  and guarded launch integration. All production gates remain disabled.

* **E1-S13 disabled implementation (2026-11-07), static only.** Broker/S10-bound
  prefix-local exact-SID HKU/classes loading, explicit Windows descriptors,
  managed HKCU/HKCR/cache/userenv paths and normal/lab test sources are in
  [E1_S13_HKU_LOAD.md](E1_S13_HKU_LOAD.md). Protocol is now 978 (request 325).
  No build, generation or runtime evidence here. S14 leases/save/unload/recovery,
  S15 consumers/environment and S18 integration still gate managed launching;
  no production enablement or S16/S20 acceptance is claimed.

* **E1-H1 done (2026-11-06).** Suspended create with full rollback, remote handle close in the caller's table, `stop_event` gate
  before launch, deny when no random id. Qwen wrote it, review fixed `ResumeThread` error test (`== (DWORD)-1`) and a wait on a
  handle the guard may close. Guest test (VM B, lab test broker): approve path exit 0; deny path (no approval) error 1223 with no
  surviving process. Script: lab `guest-sysrole/test-e1h1.sh`.
* **E1-H2 analysis done** (`E1_H2_CALLER_BINDING.md`, proposed stock-Wine changes in `STOCK_CHANGES.md`, not implemented): the
  broker alone cannot bind logon identity (`AuthenticationId` is a stub that returns zero) or pin the pipe client process; it needs a
  small Wine server change.
* **E1-H2a/b/c done (2026-11-06).** Token `AuthenticationId` (server, protocol 974), opener process and token retained on the
  pipe connection with the private `get_pipe_caller_binding` request and ntdll `NtQueryWexPipeCallerBinding` (protocol 975, system
  role with SeTcbPrivilege only), and the broker's `identify_caller` now requires: the bound process and primary token are the
  objects found by the pid (`CompareObjectHandles`), the opener was not impersonating, its primary token is unchanged, and logon and
  session of the primary token equal those of the pipe impersonation token; `launch()` requires the linked full token to have the
  caller's logon. An older server without the request denies. Guest test (VM B, current build runtime): approve path exit 0, deny
  path 1223. Not done: per-message writer provenance (a different process writing the request on the opener's connection), and
  negative tests that forge each mismatch (E1-T).
* **E1-T first part done (2026-11-06).** Probe modes `impersonate` (an opener that impersonates is denied with error 5, nothing
  starts) and `callerdeath` (a caller that disconnects without reading the reply leaves no tagged process) in
  `scripts/wexelevate_probe.c`, wired into `scripts/test_sysrun_server.sh` section 5. Run in VM B against the lab test broker with a
  positive control (`run` passes): all pass. Not yet covered: a forged mismatch of process or token (needs a second process sharing
  the pipe handle), broker stop during consent, image change during consent, existing writer, prompt crash.
* **E1-S design written (2026-11-06)** in `E1_S_STANDARD_USERS.md` (Codex gpt-6.1-sol, static): PAM verifier in a root-owned helper,
  request-bound transactions, new-account administrator token. Implementation waits for the owner questions in its section 9,
  chiefly what "run as the administrator" means (real administrator host UID and profile, which needs cross-UID server work, or a
  Wine-only identity under the caller's UID).
* **E1-S owner decision (2026-11-06): Wine-only identity first.** The administrator's elevated process gets the administrator's
  token (own logon id) but runs under the caller's host UID in the caller's prefix and session, as Windows over-the-shoulder
  elevation does (the token is the identity). Real administrator host UID and cross-UID server work are a later option, not part of
  E1-S. Launches that would need a real UID deny as unsupported. Honest limit to document: host-level file ownership and raw
  syscalls stay the caller's. The administrator's profile and HKCU live in the caller's prefix and need their own storage design.
* **E1-T part 2 done (2026-11-06).** Probe modes `imagerace` (image replaced from Unix while consent is pending: ERROR_FILE_CORRUPT,
  nothing starts) and `writerfirst` (existing writer: ERROR_SHARING_VIOLATION), both with a no-surviving-child check. Run in VM B
  with `guest-sysrole/test-e1t2.sh`: pass. Still open: forged process/token mismatch, broker stop during consent, prompt crash.
* **E1-H3a done (2026-11-06).** `wexhost` `lease_take` calls `fstatfs` on the image descriptor and allows only ext2/3/4, xfs, btrfs,
  tmpfs, overlayfs, ramfs and vfat (also when no lease is needed); anything else (NFS, 9p, FUSE, iso9660, unknown) is denied with
  ERROR_NOT_SUPPORTED. Codex terra wrote it, Qwen built and tested it in VM B: approve exit 0, deny 1223, image on ext4 runs
  (0), image on an iso9660 loop mount denied (50). Own code, not stock Wine. H3b (prompt serialization) is still open.
* **E1-S1 contracts written (2026-11-06)** in `E1_S1_CONTRACTS.md` (Codex terra): seven versioned records, endpoint registration,
  ordered launch checks, state-transition owners.
* **E1-S2 lab baseline done (2026-11-06)**, `E1_S2_LAB_BASELINE.md` (Qwen): pinned guest hashes, fixture accounts bob, alice, carol and eve
  (throwaway passwords root-only in the guest), a test PAM service `wex-e1s-test`, controls (filtered admin approve 0, cancel 1223,
  standard user denied with error 50), snapshot procedure, cleanup script. Not taken: a disk snapshot.
* **E1-S3 done (2026-11-06).** Per-message writer provenance: the server records writer process, primary token, token and modified id and
  impersonation at each client write (including transceive) and keeps the record of the single request message the broker's end
  consumes; private `NtQueryWexPipeCallerBindingEx` (protocol 976); the broker requires writer == opener, unchanged tokens, no
  impersonation, and denies on a server without the fields. Token modified ids now change on every security-relevant update.
  Codex sol wrote it, Qwen built it (0 errors, same 39 warnings), `kernel32 pipe` 0 failures, `advapi32 security` the same 10
  baseline failures, and in VM B (slim runtime): approve 0, deny 1223, impersonate/callerdeath/writerfirst/imagerace PASS and 7
  provenance modes PASS.
* **E1-S5 disabled broker implementation (2026-11-06), statically reviewed only.** Frozen canonical request/binding digests,
  environment-policy digest, random attempt nonce, broker-owned states, answer/evidence comparisons, single-use table/evidence
  consumption and live H2/S3 rechecks are in `src/programs/wexelevate/e1s.h` and `main.c`, guarded by
  `WEXELEVATE_TEST_APPROVAL`. Checked reversion, request-ID collision checks and repeated binding checks also harden the existing
  filtered-admin flow. The standard-user path still returns error 50. New 32/64-bit probes and fixture/remaining-gate details:
  [E1-S5 broker records](E1_S5_BROKER_RECORDS.md). No build or run here; endpoint/account/clock/verifier/launch inputs are synthetic
  lab fixtures, not production authentication. Caller-primary replacement probes and concurrency/crash tests remain unrun.
* **E1-S6 disabled transport (2026-11-06), static review only.** Server endorsement of the specific broker image/process/connection,
  fresh generations, system-bus unique-owner plus kernel process pins, private metadata FD channel and native child fixture.
  Requires explicit WEX_E1S6_TRANSPORT_TEST; no production enablement or readiness capabilities. No credentials, PAM, evidence,
  profile/token creation or launch authority. Both-ABI probes and Qwen VM B handoff: [E1_S6_TRANSPORT.md](E1_S6_TRANSPORT.md).
  Compilation, kernel/bus behavior, restart/race controls and all runtime tests remain unverified.
* **E1-S4 done (2026-11-06).** Account map authority: `/etc/wineexplorer/accounts.map` (0640 root:wexsys, incarnation ids, SIDs, tombstones,
  generation), `tools/wex-accounts` (locked atomic sync, retire, rename), server `lookup_account` (unused by default, strict file
  checks, distinct statuses), provisioning hook and wexhost post-create sync. Known gap: an account deleted and recreated with the
  same name and uid between syncs needs a `retire` call. Codex sol wrote it, Qwen built and tested it: tool 0 warnings,
  `scripts/test_accounts_map.sh` passes, Wine build ok, `kernel32 pipe` 0 failures, `advapi32 security` the same 10 baseline
  failures, VM B regression (approve 0, deny 1223, impersonate/callerdeath/writerfirst/imagerace PASS).
* **E1 batch S4 fixes, S5, S6, S7, S8, S10 committed (2026-11-07), S6 acceptance still failing.** Built and tested together in wexbuild
  and VM B. Passing: full WoW64 build with zero warnings, `kernel32 pipe` 0 failures, `advapi32 security` the 10 baseline failures,
  S10 `wex_token` 128 tests, accounts map, S8 evidence (35 cases), S7 PAM worker (35 cases incl. the unknown/wrong/ineligible timing
  comparison within 250 ms), brokers and probes with no test code in production binaries, VM B regression (approve 0, deny 1223,
  impersonate/callerdeath/writerfirst/imagerace) and the S5 matrix 38/38. An independent Antigravity review found 8 issues in
  S6 to S8 (fixed or answered in `E1_BATCH_REPAIR.md`) and 3 in S10/S5 (lost-reply termination: S19; stop_event race and a
  reference leak in `e1s_install_consumed`: still to fix). **Open:** `scripts/test_e1s6_transport.sh` fails at the S6 accept case;
  the denial site is `wexhost` `endorse-import-control` (`wine_server_fd_to_handle` on the control socket received over D-Bus,
  see `E1_S6_ACCEPT_REVIEW.md`). The S10 probe matrix waits for S6 acceptance. All of this stays disabled by default.
* **E1-S6 acceptance passes (2026-11-07).** Root cause of the failing accept case: both Wine handle imports in `src/dlls/wexhost/e1s6.c`
  asked for access 0, which the server rejects (`alloc_handle`, STATUS_ACCESS_DENIED). They now ask for `FILE_READ_ATTRIBUTES`; every
  wexhost denial site now logs the real NTSTATUS/errno and the native service logs exception text. `scripts/test_e1s6_transport.sh`
  (root, clean wexbuild): `E1S6_TRANSPORT OK`, 302 PASS, 0 FAIL, all 9 cases in both ABIs plus the same-UID bus impostor. After redeploy
  to VM B: approve 0, deny 1223, impersonate/callerdeath/writerfirst/imagerace PASS, S5 matrix 38/38. Not run yet: the S10 probe matrix
  of `E1_S10_TOKEN.md` (its negative cases need the S11 trusted driver or server fault injection); the probe builds clean for both ABIs.
  Process note: a Qwen job left the kept S6 fixture installed in wexbuild; it was removed by hand (users wexadm/wexstd/wexcar, group
  wexsysc2, system role directories).
* **E1-S11 prerequisites and two review fixes written (2026-11-07), static only.** The real early-stop race in broker
  `service_main` is fixed by creating/checking its stop event before handler registration. The real retained caller/primary
  reference leak in `wine/server/token.c:e1s_install_consumed` now releases/clears both objects on failed caller validation;
  no matching function exists in broker `e1s.h`. New `scripts/test_e1s10_token.sh` sources the S6 fixture, builds both extended
  probes and forces the lab objects, runs both ABI matrices plus normal-runtime denial controls, and cleans owned state on exit.
  [E1_S11_TOKEN_TESTS.md](E1_S11_TOKEN_TESTS.md) records invocation, coverage and open fault/mutation/acceptance work. The new
  probe/harness have not been built or run here; S11 is not accepted. Lost-reply termination remains S19; production stays disabled.
* **E1 batch 2 committed (2026-11-07): S6 review fixes, S11 fixes, S12 and S13 as work in progress.** Validated: S6 acceptance
  (`scripts/test_e1s6_transport.sh`) 302 PASS / 0 FAIL twice with Codex's S6 review fixes (control-socket peer checks on both
  sides, transactional endorsement, pidfd cap, polled control read); Wine tests `advapi32 wex_token` 128/0 and `kernel32 pipe` 0
  failures in both ABIs. The S11 fixes (service handler created after `stop_event`, `e1s_install_consumed` reference leak) are
  in. Not validated: the S10/S11 token harness (`scripts/test_e1s10_token.sh`) stops in its first case (broker probe `FAIL line 617`,
  silent peer; see `E1_S10_HARNESS_FAILURE.md`); S12 (profile storage, `tools/wex-profile`, `test_profile_storage.sh`) and S13
  (HKU load, `test_hku_load.sh`) are untested static work, disabled by default. Lab recipe corrections: Wine makefiles use
  `CFLAGS`, not `EXTRA_CFLAGS`; the lab objects are `server/sysrun.o`, `server/named_pipe.o`, `dlls/wexhost/e1s6.o`.
- 2026-11-07 VM B redeploy after clean rebuild (protocol 978, S12/S13 static code, kernelbase fix): test-e1h2 approve 0 and deny 1223, test-e1t and test-e1t2 all PASS, S5 matrix 38 of 38 PASS. S10/S11 harness failure still open.
- 2026-11-07 E1-S10 harness repaired (probe rights fix, diagnostics, finite longer timeouts, evidence retention): E1S10_TOKEN OK with 52 PASS. S12 storage test passes. S12/S13 review findings fixed by Codex (hooks gated by WEX_E1S13_HKU_TEST, bounded access masks, ancestor cache); main and normal builds pass. S13 lab harness: first run fails at the peer BEFORE state (see docs/E1_S13_LAB_FAILURE.md); S13 stays disabled by default.
- 2026-11-07 E1-S13 lab harness passes (E1S13_HKU_LOAD OK, 46 PASS, both ABIs). Fixes: kernelbase RegOpenCurrentUser now delegates to RtlOpenCurrentUser (the old self-impersonation fallback returned the predefined HKCU value as a handle), fixture suppresses the account watcher units that raced the accounts lock (sync status 6), seed file mode and format, and the inherited-file ownership check moved to S15 because the system-role broker runs in the machine prefix. test_accounts_map.sh passes.
- 2026-11-07 E1-S14 (Codex, static) built and brought up in the lab: protocol 979, leases, managed unload, generation-checked root caches. Lab harness: S13 cases and the S14 unload case pass (64-bit broker); reload after unload needed NAME_EXISTS clearing in the managed load path. Crash cases (checkpoints 1..3) and recovery still open, see docs/E1_S14_LAB_STATUS.md. 32-bit broker leg and Wine-side normal-server test not yet run for S14.
- 2026-11-07 E1-S14 lab harness passes: E1S13_HKU_LOAD OK, 68 PASS, both ABIs, including unload, persistence and reload, stale roots, crash checkpoints 1..3 and recovery denial. The harness proves injected crashes by the foreground server's exit status 86, not by peer death. Still to run for S14: S10 and S12 regression after the shared harness edits, Wine-side normal-server tests.
- 2026-11-07 E1-S15 (Antigravity) in the lab: managed-token profile lookup (userenv, shell32, Volatile Environment published by the server on load), pure launch-preparation function src/programs/wexelevate/launch_prep.c (clean allow-list environments, protected-variable and inherited-environment rejection, descriptor-anchored cwd check, real-UID requests rejected; launches nothing). Native test scripts/test_launch_prep.sh passes; S13/S14/S15 lab harness E1S13_HKU_LOAD OK with 68 PASS; S12 and S10 regressions pass. The effective-token fallback of the profile request is limited to read-side operations (LOAD still needs an explicit token). Not yet done: VM B regression on the newest build, S14/S15 normal-server Wine tests run on the stock suite, S16 and later.
- 2026-11-07 VM B (vmb-run-11) regression on the S15 build (protocol 979): test-e1h2 approve 0 and deny 1223, test-e1t and test-e1t2 all PASS, S5 matrix 38 of 38 PASS.
- 2026-11-07 E1-T part 3 (Antigravity wrote, I reran in VM B): probe modes `mismatch` (second process with a duplicated pipe handle writes the request: denied by the writer binding checks, nothing starts), `brokerstop` (test broker killed during a consent wait: the pending request fails closed with ERROR_BROKEN_PIPE, nothing starts, a restarted broker serves a fresh request) and `promptcrash` (consent prompt child killed: ERROR_CANCELLED 1223, nothing starts), with positive controls before and after and a no-surviving-child check. Driver guest-sysrole/test-e1t3.sh (lab scripts outside the repo). The E1-T adversarial list is now closed except the real interactive secure-desktop dialog, which needs a graphical session.
- 2026-11-07 E1-S17 (Antigravity): Credential dialog child, bound conversation/confirmation protocol over pipe, typed account entry with S4 validation, masked password with non-elided buffer wiping, prominent Wine-only profile and host-access explanation ("The administrator account's rights apply inside this Wine prefix only, with no host or root access. The requesting user's host account and files are unchanged."), and shared serialized prompt queue (H3b: one prompt at a time per session/desktop, FIFO order, timeout and cancel handling, cancelled/timed-out prompts never approve, starvation DoS limit of at most 2 queued entries per caller). Helper exit never approves: child process exit (code 0 or otherwise) without explicit valid signed-by-binding Confirm message is treated as DENIED. Pure native tests pass in wexbuild (`scripts/test_credential_dialog.sh`), production string-exclusion checks pass (no test strings in production binaries), and headless Xvfb runs with `scripts/cred_dialog_probe.c` verify rendering, Confirm action transmission, Cancel transmission, invalid account rejection, and child kill denial. All standard-user paths remain disabled by default until S18.
- 2026-11-07 (checked) E1-S17 credential dialog (Antigravity): consent.exe credentials mode, S4 account-name validation, masked password and wiped buffers, S1 Answer record Confirm/Cancel over a bound channel (helper exit or crash never approves), serialized prompt queue (H3b: FIFO, 2 per caller, 8 per session, 32 global, timeout and cancel never approve) in src/programs/wexelevate/cred_dialog.{c,h}. Native unit tests and the headless Xvfb dialog run pass (scripts/test_credential_dialog.sh, reran myself). Disabled by default, not wired into the broker (S18). Not rerun: the existing click-consent regression with consent_test; the real secure-desktop input path.
- 2026-11-07 E1-S16 (Antigravity, verified by the harness log): lab cases two-admins (two accounts loaded at once, separate hives and HKCU views, unloading one leaves the other), descendants (child and grandchild hold the profile busy, unload denied 0xc0000708 until both end), handle-held (descendant key handle blocks unload 0xc0000121), save-fail (read-only profile directory at save: hive poisoned, lease kept, durable hive unchanged), post-subst (hive swapped for a symlink after load: next save denied, hive poisoned), host-uid (a process with Alice's token still runs as Bob's host UID and cannot read another host user's 0600 file), both ABIs. E1S13_HKU_LOAD OK with 92 PASS (68 before). Coverage table in docs/E1_S16_COVERAGE.md. Not established: the claim that PE code cannot issue raw Linux syscalls; the host-uid case proves the credentials stay Bob's, it does not test raw syscall dispatch.
- 2026-11-08 E1-S18 (Antigravity, checked by me): standard-token branch of the broker, lab define WEXELEVATE_E1S18 only, runtime policy off by default, all 10 S1 capabilities required, separate from the filtered-admin path (production objects byte-identical to before for x86_64 and i686). A first version compiled only in the fakes-based unit test; the real broker with the define failed to compile (duplicate struct request): fixed with a shared request header and a two-ABI -Werror compile check in scripts/test_broker_e1s18.sh. Real in the broker now: image verification, suspended create with the issued token, child token and session check, lease arm, limited handle duplication, binding recheck and the guarded single resume. STILL NOT WIRED, failing closed with ERROR_NOT_SUPPORTED (documented in docs/E1_S18_BROKER_INTEGRATION.md section 7): prompt dialog spawn and queue (S17), evidence/PAM worker connection (S8/S7), token issue call (S10), profile load (S13/S14), launch preparation (S15) and profile lease transfer. Next: S18b wiring those six stages, then a Codex adversarial review of the whole sequence, then S19 and S20 (VM B).
- 2026-11-08 E1-S18b (Antigravity): wired the in-process stages of the standard-token broker branch (e1s18_broker.c). Stage 3 (issue_token): calls NtCreateWexAccountToken NEGOTIATE then ISSUE with S1 204-byte envelope, validates returned token (Primary, Full elevation, High integrity, caller session, distinct nonzero AuthenticationId, Alice mapped SID, closes on mismatch), retains s10_context. Stage 4 (load_profile): calls NtLoadWexProfileEx(WEX_HKU_LOAD) with 48-byte S12/S13 record, retains lease_id generation for transfer and unload, closes s10_context. Stage 5 (prep_launch): calls wex_prepare_launch with Bob's host UID/GID from caller, profile record, cwd; real-UID requests denied; builds sanitized Windows environment block; caller-supplied environment rejected. Stage 11 (transfer_lease): calls NtLoadWexProfileEx(WEX_HKU_TEST_TRANSFER) with child process handle and Alice token (gated on server by WEX_E1S14_CRASH_TEST). Cleanup hook (unload_profile): closes broker's hkey, calls NtLoadWexProfileEx(WEX_HKU_UNLOAD_S14), tolerates 'busy' (STATUS_RESOURCE_IN_USE, STATUS_CANNOT_DELETE, STATUS_SHARING_VIOLATION) by recording s_unload_busy for S19 rollback instead of looping. Stages 1 (dialog spawn) and 2 (PAM evidence redemption) remain failing closed with ERROR_NOT_SUPPORTED with explicit interfaces documented. Unit tests with injected fakes at NTDLL export boundary in test_broker_e1s18_main.c / scripts/test_broker_e1s18.sh (17 test suites, 100% pass across production disabled check, all 13 stage failure matrix, export boundary fakes for success, each failure, wrong token shape, wrong SID, wrong session, bad auth ID, busy unload, and full pipeline rollback). Two-ABI -Werror MinGW cross-compilation passes clean (x86_64 and i686). Byte-for-byte identical production objects verified with cmp for both ABIs.
- 2026-11-08 E1-S18b (Antigravity, checked by me; 22 fake-based unit tests and the two-ABI -Werror lab compile pass, production objects byte-identical to HEAD): stages 3 (NtCreateWexAccountToken NEGOTIATE+ISSUE with token-shape, session, auth-id and SID checks), 4 (NtLoadWexProfileEx LOAD), 5 (wex_prepare_launch), 8, 11 and the unload cleanup (busy recorded for S19) are now real calls at the ntdll export boundary; account name, account key and the map and storage generations are taken from Stage 2 and stages 3 to 5 fail closed with ERROR_INVALID_DATA when they are unset (an earlier draft hard-coded 1 and the name alice; fixed). KNOWN GAPS BEFORE ENABLEMENT: (a) the S14 server exposes lease transfer only as WEX_HKU_TEST_TRANSFER behind WEX_E1S14_CRASH_TEST, a production commit operation with broker-only gates is missing in the server (needs a Codex-reviewed server change); (b) Stage 1 (prompt dialog spawn and queue) and Stage 2 (evidence socket and PAM worker) are not wired and fail closed; (c) nothing of the real calls has run against a live server yet (S20 guest design is in docs/E1_S18_BROKER_INTEGRATION.md). Linking the real broker with the lab define needs launch_prep.c next to main.c.
