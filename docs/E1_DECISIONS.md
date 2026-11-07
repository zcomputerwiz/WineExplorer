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
