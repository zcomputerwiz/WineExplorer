<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S18: Broker Wine-Only Integration

Survey, architecture, trust decisions, failure handling, and implementation design for Work Package E1-S18 of [E1-S](E1_S_STANDARD_USERS.md) (section 7 row 18, "broker Wine-only integration").
Following [E1 decisions](E1_DECISIONS.md), [S1 contracts](E1_S1_CONTRACTS.md), [S4 accounts map](E1_S4_ACCOUNTS_MAP.md), [S5 broker records](E1_S5_BROKER_RECORDS.md), [S6 transport](E1_S6_TRANSPORT.md), [S7 PAM worker](E1_S7_PAM_WORKER.md), [S8 evidence](E1_S8_EVIDENCE.md), [S10 token](E1_S10_TOKEN.md), [S12 profile storage](E1_S12_PROFILE_STORAGE.md), [S13 HKU load](E1_S13_HKU_LOAD.md), [S14 leases](E1_S14_LEASES.md), [S15 profile env / launch prep](E1_S15_PROFILE_ENV.md), [S16 coverage](E1_S16_COVERAGE.md), and [S17 credential dialog](E1_S17_CREDENTIAL_DIALOG.md).

---

## 1. Codebase Survey & File:Line Evidence

| Component / Subsystem | File & Lines | Implementation Details & Architectural Evidence |
|---|---|---|
| **Caller Elevation Identification** | `src/programs/wexelevate/main.c:520-527` | `identify_caller()` queries `TokenElevationType`. Currently: `TokenElevationTypeLimited` returns `ERROR_SUCCESS` (filtered admin); `TokenElevationTypeDefault` (standard user) and `TokenElevationTypeFull` (already elevated) return `ERROR_NOT_SUPPORTED` (50). S18 branches `TokenElevationTypeDefault` to the standard-user credential flow when and only when all 10 S1 capabilities are present and lab define `WEXELEVATE_E1S18` + runtime policy `EnableStandardUserElevation` are enabled. Filtered admin remains byte-for-byte unchanged. |
| **Request Parsing & Hygiene** | `src/programs/wexelevate/main.c:548-590` | `parse_request()` checks magic `WEXELEVATE_MAGIC`, version `WEXELEVATE_VERSION`, string lengths, NUL character absence, absolute image path (`[A-Za-z]:[\\/]`). For standard user, inherited environment (`WEXELEVATE_ENV_INHERIT`) is strictly forbidden and rejected. |
| **Request ID & Table Collision** | `src/programs/wexelevate/main.c:190-223` | `table_add()` allocates an unoccupied slot in `table[MAX_REQUESTS]` (32 slots) and generates a cryptographic 64-bit random ID via `random_id()`. Checks against collision with existing live table entries. RNG failure or collision retry exhaustion denies with `ERROR_GEN_FAILURE`. Slot stays reserved until request cleanup (`free_request`). |
| **Image Freeze, Leases & Hashing** | `src/programs/wexelevate/main.c:626-655` | `freeze_image()` opens `req->image_file` with `FILE_SHARE_READ`, acquires host Linux read lease via `pLeaseTake()` (wexhost), creates `req->lease_event`, and hashes the file with SHA-256 via `hash_file()` (`BCryptHashData`). Held open through launch. |
| **H2/S3 Provenance & Caller Binding** | `src/programs/wexelevate/main.c:390-475` | `check_binding()` rechecks opener and consumed-writer process handles, primary token handles, `TokenId`, `ModifiedId`, `AuthenticationId`, session, and SID via `NtQueryWexPipeCallerBindingEx` (protocol 976). Ensures no impersonation, process liveness, and token immutability. |
| **Filtered-Admin Launch Path** | `src/programs/wexelevate/main.c:1023-1210` | `launch()` retrieves `TokenLinkedToken` (which must be Primary, Full, same logon AuthenticationId, same caller SID, same session). Creates suspended, duplicates limited handle (`SYNCHRONIZE \| PROCESS_QUERY_LIMITED_INFORMATION`), arms lease (`pLeaseArm`), starts guard (`start_guard`), verifies caller binding and stop event, and calls `ResumeThread`. Failure terminates suspended child, closes remote handle in caller table (`DUPLICATE_CLOSE_SOURCE`), and rolls back. |
| **Sequential Dispatch** | `src/programs/wexelevate/main.c:1658-1691` | `handle_request()` executes sequential pipeline: `identify_caller -> parse_request -> table_add -> freeze_image -> [probe hook] -> check_binding -> approve -> check_binding -> table_consume -> verify_image -> lease_arm -> launch`. S18 introduces a clean branch for standard callers without altering filtered-admin execution. |
| **S1/S5 Records & State Engine** | `src/programs/wexelevate/e1s.h:32-36, 80-89, 288-299` | Defines logical states: `QUEUED -> PROMPTING -> AUTHENTICATING -> IDENTITY_READY -> CONSUMED -> LAUNCH_PREPARED -> COMMITTED` (with terminal `CANCELLED`). Enforces canonical SHA-256 domain tags: `"E1S1-BIND\0"`, `"E1S1-REQ\0"`, `"E1S1-ENV\0"`. |
| **S17 Credential Dialog & Prompt Queue** | `src/programs/wexelevate/cred_dialog.c:1-550`, `cred_dialog.h:1-199` | Implements H3b serialized prompt queue (`wex_prompt_queue_enqueue`, FIFO, limits: 2/caller, 8/session, 32/global), S4 typed account validation (`wex_valid_account_name`: length 1..63, `[a-zA-Z0-9_.-]`), secure buffer wipe (`wex_secure_wipe`), S1 Kind 2 Answer Record serialization/verification (`wex_build_answer_record`, `wex_verify_answer_record`), and conversation state machine. Child exit without explicit Confirm fails closed. |
| **S15 Environment & Launch Prep** | `src/programs/wexelevate/launch_prep.c:1-450`, `launch_prep.h:1-114` | `wex_prepare_launch()` validates same-server inputs, generates Alice's Windows environment (with prefix-local profile directories), sanitizes Bob's Unix environment (`HOME`, `USER`, `LOGNAME`, `DISPLAY`), strips dangerous variables (`LD_*`, `SSH_*`, `WINE*`, `KRB5*`, `DBUS_*`), checks cwd containment without symlink traversal, and strictly rejects real-UID transitions (`WEX_LAUNCH_PREP_ERR_REAL_UID_UNSUPPORTED`). |
| **S8 Native Evidence & Quotas** | `tools/e1s6/evidence.py:1-331` | Manages single-use connection-owned evidence (S1 Kind 3, 220 bytes), limits (3 attempts per request, 120s total request deadline, 1 active per UID/session), authoritative S4 account key/incarnation binding, and atomic redemption. |
| **S7 PAM Conversation Worker** | `tools/e1s7/pam-worker.c:1-500` | Native root worker running fixed service `wineexplorer-elevate` (lab `wex-e1s-test`), executing `pam_authenticate` (rejecting null tokens) and `pam_acct_mgmt`, resolving canonical `PAM_USER`, and zeroing secret buffers with `explicit_bzero`. |
| **S10 Server Token Issuance** | `wine/server/token.c:2903-2962` | Handles `create_e1s_account_token` (request 324, protocol 979): `WEX_TOKEN_NEGOTIATE` (checks caps `0xc0`), `WEX_TOKEN_ISSUE` (204-byte envelope = S1 Kind 4 record + 16-byte evidence ref -> S1 Kind 5 response + non-inheritable Primary, Full, High token with fresh logon LUID and caller session). Retains standard session linked-token denial (`line 2970`). |
| **S13/S14 Profile Load & Leases** | `wine/server/registry.c:3088-3450`, `wex_profile_storage.c:1-1000` | Handles `load_e1s_profile` (request 325, protocol 979): `WEX_HKU_LOAD` (verifies broker role and context, locks prefix/SID, loads hive under `HKU\<Alice SID>`, publishes Volatile Environment with Alice's 9 variables, issues S14 lease in state 1), `WEX_HKU_TEST_TRANSFER` (commit process tracking, state 2), `WEX_HKU_UNLOAD_S14` (last-user and open-handle safe unload). |
| **S6 Transport & Endorsement** | `src/dlls/wexhost/e1s6.c:1-600` | Enforces server endorsement of broker process object and connection, pins bus owner and host PID, and imports control socket. |

---

## 2. End-to-End Execution Sequence for Standard User Elevation

The complete lifecycle for `e1s18_handle_standard_request()` proceeds through the following strictly ordered phases:

```mermaid
flowchart TD
    A["Caller pipe connect (Bob, TokenElevationTypeDefault)"] --> B{"Policy & Cap Gate (10 S1 caps present?)"}
    B -- "No" --> B_Deny["Deny: ERROR_NOT_SUPPORTED (50)"]
    B -- "Yes" --> C["Phase 1: Freeze Request & Caller Binding (e1s_request_digest, e1s_binding_digest)"]
    C --> D["Phase 2: Serialized Prompt Queue (S17 FIFO, acquire session slot)"]
    D --> E["Phase 3: Launch Credential Child (consent.exe /credentials on Winlogon)"]
    E --> F{"Bound Dialog Conversation (S1 Kind 2 Answer)"}
    F -- "Cancel / Exit / Timeout" --> F_Cancel["Rollback: Release Queue Slot -> Deny ERROR_CANCELLED"]
    F -- "Explicit Confirm" --> G["Phase 4: S8 Evidence Verification & Redemption (burn reference once)"]
    G --> H["Phase 5: S10 Account Token Issuance (NtCreateWexAccountToken ISSUE)"]
    H --> I["Phase 6: S13/S14 Profile Load & Lease Acquisition (HKU\\<Alice SID>, Volatile Env)"]
    I --> J["Phase 7: S15 Launch Preparation (Alice Windows env, Bob Unix env, cwd verify)"]
    J --> K["Phase 8: Create Child Suspended (CreateProcessAsUserW, Bob parent, normal desktop)"]
    K --> L["Phase 9: Verify Installed Identity & Arm Image Lease (path + SHA-256 + token check)"]
    L --> M["Phase 10: Duplicate Limited Handle & Transfer Lease (SYNCHRONIZE | QUERY_LIMITED)"]
    M --> N["Phase 11: Guarded Resume (Broker table_cs lock, check_binding, single ResumeThread)"]
    N --> O["Phase 12: Commit (Return PID/TID/Handle to caller, release setup state)"]

    K -- "Fail" --> Rollback["Explicit Rollback: Terminate child, close remote handle, unload hive, release lease, wipe secrets"]
    L -- "Fail" --> Rollback
    M -- "Fail" --> Rollback
    N -- "Fail" --> Rollback
```

### Detailed Sequence Steps

1. **Gate & Pre-Flight Validation**:
   - Verify caller token elevation: must be `TokenElevationTypeDefault`.
   - Verify runtime policy: `HKLM\Software\WineExplorer\Elevation\EnableStandardUserElevation` must be `1` (default `0` denies with `ERROR_NOT_SUPPORTED`).
   - Verify all 10 S1 capabilities (`0x3ff`): `H2_OPENER`, `MESSAGE_PROVENANCE`, `BINDING_V1`, `DIALOG_V1`, `VERIFIER_V1`, `EVIDENCE_V1`, `ACCOUNT_MAP_V1`, `TOKEN_V1`, `PROFILE_V1`, `COMMIT_GUARD_V1`. Any missing bit denies with `ERROR_NOT_SUPPORTED` (never falls back).
   - Verify request flags: `WEXELEVATE_ENV_INHERIT` is strictly forbidden for standard users; if set, denies with `ERROR_INVALID_PARAMETER`.
   - Verify caller binding: `check_binding()` proves opener process, opener primary token, no impersonation, matching session, and valid logon LUID.

2. **Request & Caller Freeze**:
   - Compute `binding_digest`: SHA-256 over `"E1S1-BIND\0"` + S1 Kind 1 canonical caller binding record.
   - Compute `request_digest`: SHA-256 over `"E1S1-REQ\0"` + request ID, binding digest, held image metadata (volume serial, file index, file size, last-write time, SHA-256), image path, command line, absolute cwd, environment policy (`1` = fresh managed), show state, flags, and 120s deadline.
   - Compute `environment_digest`: SHA-256 over `"E1S1-ENV\0"` + `env_policy` (`1`).
   - Generate cryptographic 32-byte `nonce`.

3. **Prompt Serialization & Credential Dialog (S17)**:
   - Enqueue in session prompt queue (`wex_prompt_queue_enqueue`).
   - Check per-caller limit: at most 2 active/queued requests per caller PID (`WEX_PROMPT_QUEUE_PER_CALLER_LIMIT`). If exceeded, deny with `ERROR_BUSY`.
   - Check per-session limit (8) and global limit (32).
   - Wait for slot activation in FIFO order.
   - Spawn credential child on `WinSta0\Winlogon` (or `WinSta0\Default` if insecure desktop policy configured):
     `consent.exe /broker /credentials /ppid <broker_pid> /request %016llx /token <nonce_hex> /channel <handle> [/secure]`
   - Dialog displays mandatory explanation:
     *"The administrator account's rights apply inside this Wine prefix only, with no host or root access. The requesting user's host account and files are unchanged. Settings for this program are stored in this Wine prefix only."*
   - Child sends typed account name: validated with `wex_valid_account_name` (S4 rule).
   - Only an explicit S1 Kind 2 Answer Record with `answer_kind == 2` (Confirm) matching request ID, attempt, nonce, binding digest, request digest, and channel ID approves.
   - Any child termination, crash, timeout (120s), or cancel (`answer_kind == 3`) fails closed with `ERROR_CANCELLED` (1223).
   - Dequeue and release prompt slot (`wex_prompt_queue_release`).

4. **Single-Use Evidence & Quotas (S8 / S7)**:
   - S8 native evidence service validates PAM conversation result from S7 worker (fixed service `wineexplorer-elevate`, `pam_authenticate` + `pam_acct_mgmt`, final `PAM_USER` mapped to S4 account key).
   - S8 returns S1 Kind 3 Evidence Record (220 bytes) + 16-byte connection-bound reference.
   - Broker verifies:
     - `request_id`, `request_digest`, `binding_digest`, `nonce` match frozen request.
     - `host_uid` matches Bob's host UID (Wine-only identity boundary).
     - Account key is an active administrator in S4 account map.
     - Monotonic expiry is valid (`now < expiry`).
   - Broker redeems evidence reference: reference is burned atomically by verifier. Replay or reuse is impossible.

5. **Account Token Issuance (S10)**:
   - Dispatch `NtCreateWexAccountToken(WEX_TOKEN_ISSUE, ...)` (request 324, protocol 979) with 204-byte envelope (S1 Kind 4 record + 16-byte evidence reference).
   - Server validates context, broker SYSTEM credentials, consumes request, builds account token, and allocates fresh logon LUID.
   - Broker receives Primary token handle and S1 Kind 5 response (136 bytes).
   - Broker independently queries token via `GetTokenInformation`:
     - `TokenType == TokenPrimary`.
     - `TokenElevationType == TokenElevationTypeFull`.
     - `TokenIntegrityLevel >= 0x3000` (High).
     - `TokenSessionId == req->caller_session`.
     - `AuthenticationId` is nonzero and distinct from `req->caller_stats.AuthenticationId`.
     - `TokenUser.User.Sid` equals Alice's S4 SID.
     - `SE_GROUP_LOGON_ID` matches fresh logon LUID.

6. **Profile Load & Leases (S13 / S14)**:
   - Call `load_e1s_profile(WEX_HKU_LOAD, context, token, ...)` (request 325, protocol 979).
   - Server validates account, locks prefix `.profile.lock` and SID `.hive.lock`, loads `HKU\<Alice SID>`, registers managed save branch, publishes Volatile Environment with Alice's 9 variables, and issues S14 lease in state 1 (setup).
   - Broker obtains HKU root key handle and generation.

7. **Launch Preparation (S15)**:
   - Invoke pure launch preparation `wex_prepare_launch(&params, &result)`.
   - Verifies:
     - `requested_uid == bob_uid` (real-UID switch denied with `WEX_LAUNCH_PREP_ERR_REAL_UID_UNSUPPORTED`).
     - `flags & WEX_LAUNCH_PREP_FLAG_INHERIT_ENV` rejected.
     - Working directory containment verified via descriptor-anchored path resolution without symlink traversal.
   - Generates:
     - Alice's Windows environment block with prefix-local profile paths (`USERPROFILE`, `APPDATA`, `LOCALAPPDATA`, `TEMP`, `TMP`, `USERNAME`, `USERDOMAIN`, `HOMEDRIVE`, `HOMEPATH`).
     - Sanitized Bob Unix environment (`HOME`, `USER`, `LOGNAME`, `DISPLAY`), stripping dangerous variables.

8. **Suspended Child Creation**:
   - Re-verify held image identity and SHA-256 (`verify_image`).
   - Re-check caller binding (`check_binding`).
   - Re-check stop event (`WaitForSingleObject(stop_event, 0)`).
   - Configure `STARTUPINFOEXW` with `PROC_THREAD_ATTRIBUTE_PARENT_PROCESS` set to `req->caller` and desktop set to `WinSta0\Default` (caller's normal desktop, not the credential desktop).
   - Call `CreateProcessAsUserW` with Alice's issued token, `CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_CONSOLE`, sanitized Windows environment, and sanitized cwd.
   - Verify child was created suspended.
   - Query child process token: verify token SID == Alice SID, session == caller session.
   - Arm read lease on image descriptor (`pLeaseArm(req->lease, pi.dwProcessId)`).
   - Start lease guard (`start_guard(req, guard_process)`).

9. **Limited Handle Duplication & Lease Ownership Transfer**:
   - If caller requested handle (`req->flags & WEXELEVATE_WANT_HANDLE`):
     Duplicate `pi.hProcess` into caller's process table with strictly limited rights:
     `SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION`.
   - Transfer profile lease from setup (state 1) to committed process tracking (state 2) via `e1s_profile_commit_process` / `WEX_HKU_TEST_TRANSFER`.

10. **Guarded Resume**:
    - Enter `table_cs` critical section.
    - Final recheck of caller binding (`check_binding(req, req->caller_pid, NULL, FALSE)`).
    - Final check of `stop_event` and `lease_event`.
    - Resume child thread: `ResumeThread(pi.hThread)`. Must be executed by broker only, guarded, and called exactly once (`ret != (DWORD)-1`).
    - Leave `table_cs`.

11. **Commit & Reply**:
    - Close broker's local process and thread handles (`CloseHandle(pi.hThread)`, `CloseHandle(pi.hProcess)`).
    - Return `ERROR_SUCCESS` with `reply->pid = pi.dwProcessId`, `reply->tid = pi.dwThreadId`, `reply->handle = remote`.

---

## 3. Trust Decision Table

| Stage / Decision | What is Checked | By Whom | Bound Identifiers & Objects |
|---|---|---|---|
| **D1: Caller Admission** | Pipe opener process, primary token, elevation type == Default, no impersonation, nonzero caller logon LUID, matching pipe & token session. | Broker (`identify_caller`) | Caller PID, caller process handle, caller primary token, `AuthenticationId`, `TokenId`, `ModifiedId`, `session_id`, `caller_sid`. |
| **D2: Capability Negotiation** | All 10 S1 capabilities (`0x3ff`) present on link and components. | Broker (`e1s18_check_capabilities`) | Link capability mask `0x3ff`, `version == 1`. |
| **D3: Runtime Policy Gate** | Registry policy `EnableStandardUserElevation == 1`. | Broker (`e1s18_check_policy`) | `HKLM\Software\WineExplorer\Elevation`, DWORD value. |
| **D4: Request Hygiene & Freeze** | Valid PE image, local supported filesystem, read lease taken, SHA-256 computed, cwd absolute, no environment inheritance. | Broker (`parse_request`, `freeze_image`) | Volume serial, file index, file size, last-write FILETIME, image SHA-256, lease ID, `image_file` handle. |
| **D5: Digest Canonicalization** | Binding, Request, and Environment digests match canonical S1 format with domain tags `"E1S1-BIND\0"`, `"E1S1-REQ\0"`, `"E1S1-ENV\0"`. | Broker (`e1s_canonical_digests`) | `binding_digest[32]`, `request_digest[32]`, `environment_digest[32]`, `request_id`, 32-byte `nonce`. |
| **D6: Prompt Serialization** | Per-caller queue limit <= 2, per-session limit <= 8, global <= 32, FIFO order. | Broker (`wex_prompt_queue_enqueue`) | `caller_pid`, `caller_logon`, `session_id`, `desktop`, `request_id`, `deadline`. |
| **D7: Credential Confirmation** | Bound S1 Kind 2 Answer Record: `answer_kind == 2` (Confirm), typed account valid in S4, channel ID matches. Child exit or crash never approves. | Broker (`wex_conversation_apply_answer`) | `request_id`, `attempt`, `round`, `index`, `nonce`, `binding_digest`, `request_digest`, `channel_id`, typed account name. |
| **D8: S8 Evidence Verification** | Single-use S1 Kind 3 record (220 B), PAM success, canonical account mapped in S4, `host_uid == Bob UID`, `now < expiry`. | Broker & S8 Service (`evidence.py:collect`, `redeem`) | `evidence_ref[16]`, `request_id`, `account_key[16]`, `host_uid`, `completed`, `expiry`, `root_gen`, `server_gen`, `broker_gen`, `verifier_gen`, `map_gen`, `policy_gen`. |
| **D9: Token Issuance** | Request 324 ISSUE envelope: context valid, SYSTEM caller, SeTcbPrivilege, Primary token, Full elevation, High integrity, fresh logon LUID, caller session, Alice SID. | Server (`wine/server/token.c:create_e1s_account_token`) & Broker query | `context`, `request_id`, `account_key`, `nonce`, issued `token_handle`, fresh `AuthenticationId`, `SE_GROUP_LOGON_ID`, `Alice SID`. |
| **D10: Profile Lease Acquisition** | Request 325 WEX_HKU_LOAD: broker context, Alice token account, prefix/SID flock, HKU key load, Volatile Env, lease state 1. | Server (`wine/server/registry.c:load_e1s_profile`) & Broker | `token`, `context`, `Alice SID`, `account_key`, `incarnation`, `prefix_id`, `lease_id`, HKU proxy handle. |
| **D11: Launch Prep & Traversal** | Pure launch prep: `exec_uid == Bob UID`, real-UID switch rejected, cwd containment verified via descriptors without symlinks, clean Windows/Unix env. | Broker (`wex_prepare_launch`) | `bob_uid`, `bob_gid`, `requested_uid`, `prefix_fd`, `profile_dir_fd`, `cwd_dir_fd`, `requested_cwd`. |
| **D12: Installed Child Validation** | Image re-hash matches frozen hash; child created suspended; child token SID == Alice SID; child session == caller session; lease armed. | Broker (`verify_image`, child query) | Child PID, child TID, child process handle, `pi.hThread`, `req->lease`, `lease_event`, Alice SID. |
| **D13: Handle Duplication** | Duplicate limited process handle to caller with `SYNCHRONIZE \| PROCESS_QUERY_LIMITED_INFORMATION`. Transfer lease to state 2. | Broker & Server (`DuplicateHandle`, `e1s_profile_commit_process`) | `req->caller`, `pi.hProcess`, `remote_handle`, `lease_id`. |
| **D14: Guarded Resume** | Final binding recheck, stop event clear, lease event clear, broker executes `ResumeThread` exactly once. | Broker (`table_cs`, `check_binding`, `ResumeThread`) | `table_cs`, `req->caller_pid`, `stop_event`, `lease_event`, `pi.hThread`. |

---

## 4. Failure and Denial Table

Every failure in the sequence leads to immediate denial with a documented error code and comprehensive, explicit cleanup:

| Failure Point / Condition | Observed Error / Condition | Returned Status | Required Cleanup Actions |
|---|---|---|---|
| **Missing capability** | Any of 10 S1 capabilities missing from negotiated link mask. | `ERROR_NOT_SUPPORTED` (50) | Deny immediately; no table entry or child process created. |
| **Runtime policy disabled** | `EnableStandardUserElevation` is 0 or key missing. | `ERROR_NOT_SUPPORTED` (50) | Deny immediately; standard-user path remains disabled. |
| **Caller impersonating** | `check_binding` detects thread impersonation on pipe. | `ERROR_ACCESS_DENIED` (5) | Close client thread; revert impersonation; deny. |
| **Caller token mutated** | Primary token modified ID, token ID, or logon ID changed. | `ERROR_ACCESS_DENIED` (5) | Remove table entry; close handles; deny. |
| **Inherited env requested** | `req->flags & WEXELEVATE_ENV_INHERIT` set for standard user. | `ERROR_INVALID_PARAMETER` (87) | Remove table entry; release lease; deny. |
| **Queue quota exceeded** | Caller has >2 requests active in prompt queue. | `ERROR_BUSY` (170) | Deny request; do not enqueue; do not spawn dialog. |
| **User cancels dialog** | Child sends S1 Answer Record with `answer_kind == 3`. | `ERROR_CANCELLED` (1223) | Dequeue prompt slot; wipe secrets; release lease; deny. |
| **Dialog timeout** | Deadline (120s) elapsed without Confirm answer. | `ERROR_TIMEOUT` (1460) | Terminate dialog child; dequeue slot; wipe secrets; deny. |
| **Dialog child crash/exit** | Child exits without explicit bound Confirm message. | `ERROR_CANCELLED` (1223) | Helper exit never approves; dequeue slot; wipe secrets; deny. |
| **Typed account invalid** | Account name fails S4 character set or length rules. | `ERROR_ACCESS_DENIED` (5) | Reject attempt; dialog prompts retry or terminates; wipe buffers. |
| **Evidence redemption fail** | Reference expired, already consumed, or digest mismatch. | `ERROR_ACCESS_DENIED` (5) | Dequeue prompt slot; wipe secrets; release lease; deny. |
| **Token issuance denied** | Server rejects ISSUE request (status != 0, e.g. bad context). | `ERROR_ACCESS_DENIED` (5) | Release evidence; dequeue prompt slot; wipe secrets; deny. |
| **Token shape mismatch** | Issued token is not Primary, Full, High, or wrong session. | `ERROR_INVALID_PARAMETER` (87) | Close issued token; dequeue slot; release lease; deny. |
| **Profile load failure** | Hive missing, locked by another server, or storage error. | `ERROR_FILE_NOT_FOUND` / `ERROR_ACCESS_DENIED` | Close issued token; dequeue slot; release lease; deny. |
| **Real-UID switch requested** | Launch prep detects `requested_uid != bob_uid`. | `ERROR_NOT_SUPPORTED` (50) | Unload profile; close token; dequeue slot; deny. |
| **Working dir traversal** | Cwd escapes prefix or contains unsafe symlinks. | `ERROR_ACCESS_DENIED` (5) | Unload profile; close token; dequeue slot; deny. |
| **Image rewrite / writer race**| Image modified or writer opened image during prompt. | `ERROR_FILE_CORRUPT` (1392) | Unload profile; close token; dequeue slot; deny. |
| **Suspended create fail** | `CreateProcessAsUserW` fails (e.g. bad binary, memory). | `GetLastError()` from CreateProcess | Unload profile; close token; dequeue slot; wipe env; deny. |
| **Child token mismatch** | Child process does not have Alice's issued token. | `ERROR_ACCESS_DENIED` (5) | `TerminateProcess(pi.hProcess, 1)`; wait; close handles; unload profile; close token; deny. |
| **Handle duplication fail** | `DuplicateHandle` into caller fails. | `GetLastError()` | `TerminateProcess(pi.hProcess, 1)`; wait; close handles; unload profile; close token; deny. |
| **Lease transfer fail** | Server rejects ownership transfer to process. | `ERROR_ACCESS_DENIED` (5) | Close remote handle (`DUPLICATE_CLOSE_SOURCE`); `TerminateProcess`; wait; unload profile; deny. |
| **Pre-resume binding fail** | Caller died, impersonated, or replaced token before resume.| `ERROR_CANCELLED` (1223) | Close remote handle (`DUPLICATE_CLOSE_SOURCE`); `TerminateProcess`; wait; unload profile; deny. |
| **ResumeThread failure** | `ResumeThread(pi.hThread)` returns `(DWORD)-1`. | `GetLastError()` | Close remote handle (`DUPLICATE_CLOSE_SOURCE`); `TerminateProcess`; wait; unload profile; deny. |
| **Stop event signaled** | Broker service stop signaled at any step. | `ERROR_CANCELLED` (1223) | If child suspended: `TerminateProcess`; close remote handle; unload profile; close token; deny. |

---

## 5. Lab Guest Case Design (Section 8 Matrix & S20 End-to-End Suite)

This test scenario design defines the multi-user lab validation suite for execution in a clean, snapshotted guest environment (e.g. following S2 baseline with fixture accounts Bob, Alice, Carol, and Eve). **Per standing instructions, this case design is documented statically and must NOT be run on VM B (which is down). Real end-to-end guest execution is designated for Work Package E1-S20.**

### Test Setup & Fixtures
1. **Accounts**:
   - `bob` (UID 62001): Standard user (`TokenElevationTypeDefault`).
   - `alice` (UID 62002): Mapped administrator (`BUILTIN\Administrators`).
   - `carol` (UID 62003): Second mapped administrator.
   - `eve` (UID 62004): Standard user (authenticates but not an administrator).
2. **Environment & Policies**:
   - Test PAM service `wineexplorer-elevate` (configured with `pam-lab-module.so`).
   - `HKLM\Software\WineExplorer\Elevation\EnableStandardUserElevation = 1`.
   - Broker running under SYSTEM with enabled `SeTcbPrivilege` and S6 transport registration.
   - Wine build with `WEX_E1S10_TOKEN_TEST`, `WEX_E1S13_HKU_TEST`, `WEX_E1S14_CRASH_TEST`, and `WEX_E1S18` enabled on wineserver and ntdll.

### Test Cases

| Case ID | Scenario & Injected Input | Expected Verification Assertions |
|---|---|---|
| **TC-S18-01: Positive Standard Elevation** | Bob launches elevated prompt; enters Alice's valid credentials; confirms dialog. | 1. Prompt queue enqueues and activates.<br>2. S1 Confirm answer received with valid nonce/digests.<br>3. S8 evidence redeemed once.<br>4. Alice Primary/Full/High token issued with fresh logon LUID != Bob logon.<br>5. `HKU\<Alice SID>` loaded, S14 lease acquired, Volatile Env populated.<br>6. Child created suspended with Bob parent and normal desktop.<br>7. Target process executed under Bob host UID (verified via `/proc/<pid>/status`).<br>8. Limited handle (`SYNCHRONIZE \| QUERY_LIMITED`) duplicated to Bob.<br>9. Broker resumes child; exit code 0; clean rollback of setup leases. |
| **TC-S18-02: Non-Administrator Denial** | Bob launches elevated prompt; enters Eve's credentials (Eve authenticates in PAM but is not in sudo/admin group). | 1. PAM authenticates Eve.<br>2. S4 account resolution checks eligibility: Eve is not an administrator.<br>3. Broker denies with `ERROR_ACCESS_DENIED`.<br>4. Nothing is launched; zero surviving processes or orphan leases. |
| **TC-S18-03: User Cancellation** | Bob launches elevated prompt; clicks Cancel (`IDNO`) in dialog. | 1. Dialog sends S1 Answer Record with `answer_kind == 3`.<br>2. Broker returns `ERROR_CANCELLED` (1223).<br>3. Prompt queue slot released immediately.<br>4. No token issued, no hive loaded, nothing started. |
| **TC-S18-04: Dialog Child Killed** | Credential dialog child terminated externally (`SIGKILL` / `TerminateProcess`). | 1. Broker observes child exit without explicit Confirm.<br>2. Helper exit fails closed (`ERROR_CANCELLED`).<br>3. Prompt queue slot released; no survivor. |
| **TC-S18-05: Queue Starvation / Quota** | Bob attempts to enqueue 3 concurrent elevation requests simultaneously. | 1. First 2 requests admitted to prompt queue.<br>2. Third request immediately denied with `ERROR_BUSY` (170).<br>3. Caller cannot exhaust broker resources. |
| **TC-S18-06: Inherited Environment Denial** | Bob requests standard elevation with `WEXELEVATE_ENV_INHERIT` flag set. | 1. Broker rejects in pre-flight with `ERROR_INVALID_PARAMETER`.<br>2. No prompt shown, no token issued. |
| **TC-S18-07: Real-UID Switch Denial** | Request parameter attempts to request Alice's host UID (`requested_uid == 62002`). | 1. Launch prep returns `WEX_LAUNCH_PREP_ERR_REAL_UID_UNSUPPORTED`.<br>2. Broker denies with `ERROR_NOT_SUPPORTED`.<br>3. No root fallback or switch allowed. |
| **TC-S18-08: Working Directory Traversal** | Working directory set to symlink pointing outside prefix (`/tmp/secret`). | 1. Launch prep detects traversal / symlink escape.<br>2. Broker denies with `ERROR_ACCESS_DENIED`. |
| **TC-S18-09: Image Rewrite During Consent** | Image binary overwritten while credential prompt is waiting for input. | 1. Host lease break fires or `verify_image` hash mismatch detected.<br>2. Broker aborts launch with `ERROR_FILE_CORRUPT`.<br>3. No process executed. |
| **TC-S18-10: Filtered-Admin Regression** | Filtered-admin caller (`TokenElevationTypeLimited`) executes click consent. | 1. Standard-user branch NOT taken.<br>2. Existing filtered-admin path executes byte-for-byte unchanged.<br>3. Linked full token used with caller's same logon LUID.<br>4. Success with exit 0. |

### S20 End-to-End Guest Execution Test Design (Wired In-Process Stages)

Work Package E1-S20 will run the full end-to-end sequence in the guest VM with real server processes once the service plumbing for Stages 1 and 2 is integrated. The guest test harness exercises the exact in-process stages wired in S18b:

1. **Guest NTDLL Dynamic Export Resolution**:
   - `wexelevate.exe` loads `ntdll.dll` and locates `NtCreateWexAccountToken` and `NtLoadWexProfileEx` via `GetProcAddress`.
   - Harness verifies both function pointers resolve successfully without fallback stubs.

2. **Stage 3 Server Token Issuance (End-to-End)**:
   - Call `NtCreateWexAccountToken(WEX_TOKEN_NEGOTIATE)`: verify wineserver grants `0xc0` capabilities and returns valid S10 broker context handle.
   - Construct S1 Kind 4 envelope (188 bytes) + 16-byte evidence reference (204 bytes total).
   - Call `NtCreateWexAccountToken(WEX_TOKEN_ISSUE)`: verify wineserver creates real primary token and returns S1 Kind 5 record (136 bytes).
   - In-guest assertions:
     - `GetTokenInformation(TokenType)` returns `TokenPrimary` (1).
     - `GetTokenInformation(TokenElevationType)` returns `TokenElevationTypeFull` (2).
     - `GetTokenInformation(TokenIntegrityLevel)` returns High integrity (>= `0x3000`).
     - `GetTokenInformation(TokenSessionId)` matches Bob's caller session.
     - `GetTokenInformation(TokenStatistics)` returns nonzero `AuthenticationId` distinct from Bob's logon LUID.
     - `GetTokenInformation(TokenUser)` SID equals Alice's mapped administrator SID and does NOT equal Bob's caller SID.

3. **Stage 4 Profile Load & Lease Generation (End-to-End)**:
   - Construct S12/S13 record (48 bytes: version 1, size 48, Alice's account key).
   - Call `NtLoadWexProfileEx(WEX_HKU_LOAD, context, token, KEY_ALL_ACCESS, ...)`:
     - Verifies S10 context claim in `wine/server/token.c:token_claim_e1s_profile`.
     - Verifies hive lock, volatile environment creation under `\Registry\User\<Alice SID>\Volatile Environment` with Alice's 9 variables (`USERPROFILE`, `USERNAME`, `HOMEDRIVE`, `HOMEPATH`, `USERDOMAIN`, `APPDATA`, `LOCALAPPDATA`, `TEMP`, `TMP`).
     - Returns valid HKU proxy key handle and non-zero lease generation (`lease_id`).
   - S10 context closed immediately by broker.

4. **Stage 5 Launch Preparation & Environment Generation (End-to-End)**:
   - `wex_prepare_launch` executes with Bob's host UID/GID (`62001/62001`), prefix descriptors, and cwd.
   - Generates sanitized double-NUL Windows environment block for `CreateProcessAsUserW` containing Alice's profile variables and clean system paths.
   - Confirms caller-supplied environment inheritance is denied with `ERROR_INVALID_PARAMETER`.

5. **Stage 7 & 8 Process Creation & Identity Verification (End-to-End)**:
   - `CreateProcessAsUserW` called with Alice's token, suspended flag, parent attribute set to Bob caller, and sanitized environment.
   - `OpenProcessToken` on suspended child verified: user SID matches Alice SID, session matches caller session.

6. **Stage 11 Lease Transfer to Child Process Tracking (End-to-End)**:
   - Call `NtLoadWexProfileEx(WEX_HKU_TEST_TRANSFER, pi.hProcess, token, 0, NULL, 0, &unused, &managed, &generation)`.
   - Requires server built with `WEX_E1S14_CRASH_TEST`.
   - Wineserver calls `e1s_profile_commit_process(child, lease_id, request_id)`: transfers lease from setup state (state 1) to committed process lifetime (state 2).
   - Broker closes local HKU proxy key handle on commit.

7. **Stage 13 Guarded Resume & S19 Rollback / Busy Unload Handling**:
   - Broker calls `ResumeThread(pi.hThread)` exactly once.
   - In positive test: child executes, uses Alice's HKU registry hive, exits with code 0.
   - In negative/termination test: child terminated while holding open registry key handle.
   - Broker calls `NtLoadWexProfileEx(WEX_HKU_UNLOAD_S14, NULL, token, ...)`.
   - If child or key is still active, server returns `STATUS_RESOURCE_IN_USE` (0xc0000708) or `STATUS_CANNOT_DELETE` (0xc0000121).
   - Broker records `s_unload_busy = TRUE` with `s_unload_busy_lease_id` for S19 background reaper rollback instead of hanging or looping.

---

## 6. Production Safety & Gating

- **Lab Define `WEXELEVATE_E1S18`**:
  The standard-user integration branch is compiled **only** when `WEXELEVATE_E1S18` is explicitly defined during compilation. In production builds without this define, `TokenElevationTypeDefault` callers unconditionally return `ERROR_NOT_SUPPORTED` (50).
- **Runtime Policy Gate**:
  Even in lab builds with `WEXELEVATE_E1S18` defined, standard-user elevation remains disabled by default unless the administrator explicitly provisions the DWORD registry value `EnableStandardUserElevation = 1` under `HKLM\Software\WineExplorer\Elevation`.
- **Production Build Exclusions**:
  `scripts/test_sysrun_server.sh` and `scripts/test_broker_e1s18.sh` assert that neither `WEXELEVATE_E1S18` nor associated test symbols appear in production binaries, Makefiles, or installer manifests.

---

## 7. Stage Status & Wiring Audit

The default operations table (`struct e1s18_ops s_default_ops`) strictly enforces a **fail-closed** policy for any stage whose external subsystem connection is not yet completed in the tree. Under no circumstances does a missing subsystem connection return `ERROR_SUCCESS` as a stub.

### Known Gaps Before Enablement

Before standard-user elevation can be enabled in production or shipping builds, the following known gaps must be resolved:

1. **Server Lease Transfer Operation (Stage 11 Production Commit Gate Missing)**:
   - In the S14 Wine server (`wine/server/registry.c:3476-3493`, `load_e1s_profile`), lease transfer is exposed only as `WEX_HKU_TEST_TRANSFER` (op 10) behind the test compile guard `#ifdef WEX_E1S14_CRASH_TEST`.
   - A production-named commit operation with its own broker-only gates is missing in the server. Without this server gate, any call to lease transfer in a production build returns `STATUS_NOT_SUPPORTED` (`0xc00000bb`), mapping to `ERROR_NOT_SUPPORTED`. Consequently, Stage 11 cannot commit in a production build.
2. **Stage 1 (Prompt Queue & Bound Credential Dialog) Not Wired**:
   - Spawning `consent.exe /credentials` on secure desktop `WinSta0\Winlogon`, the named pipe IPC channel handshake, the conversation loop with the S17 prompt queue (H3b), and 120s timeout/cancellation handling are not yet wired into the elevation broker.
   - Stage 1 fails closed by default with `ERROR_NOT_SUPPORTED` (50).
3. **Stage 2 (Evidence Verification & Redemption) Not Wired**:
   - Connection to the native S8 evidence daemon over Unix domain socket `/run/wineexplorer/evidence.sock`, sending the S1 Kind 2 Answer record, receiving the S1 Kind 3 Evidence record (220 bytes) and 16-byte reference, and verifying S7 PAM worker authentication are not yet wired.
   - Stage 2 fails closed by default with `ERROR_NOT_SUPPORTED` (50).
4. **Stage 2 Dependent Fields Unset by Default (Fail Closed at Stages 3–5)**:
   - Authoritative S4 `account_name`, `account_key`, `map_generation` and S12 `storage_generation` in `struct request` must be populated upon Stage 2 evidence redemption.
   - Because Stage 2 is not yet wired, these fields remain unset (zero) by default.
   - Stages 3 (`issue_token`), 4 (`load_profile`), and 5 (`prep_launch`) strictly fail closed with `ERROR_INVALID_DATA` (13) if any required Stage 2 fields are unset; they never fall back to hardcoded defaults or guesses (e.g. literal `"alice"` or hardcoded generation 1).

### Detailed Stage Audit (Default Operations Table)

| Stage # | Stage Name | Implementation Status | Behavior / Subsystem Invoked |
|---|---|---|---|
| **Stage 1** | `prompt_dialog` | **Not yet wired (Fail closed)** | Returns `ERROR_NOT_SUPPORTED` (50). Service plumbing needed: spawn `consent.exe /credentials` on `WinSta0\Winlogon`, establish named pipe IPC channel handshake, execute conversation loop with S17 prompt queue (H3b), and enforce 120s timeout and cancellation. |
| **Stage 2** | `redeem_evidence` | **Not yet wired (Fail closed)** | Returns `ERROR_NOT_SUPPORTED` (50). Service plumbing needed: connect to native S8 evidence daemon over Unix domain socket `/run/wineexplorer/evidence.sock`, send S1 Kind 2 Answer record, receive S1 Kind 3 Evidence record (220 B) and 16-byte reference, and verify S7 PAM worker authentication. |
| **Stage 3** | `issue_token` | **Wired (Real NTDLL export)** | Requires Stage 2 `account_key` and `map_generation`; fails closed with `ERROR_INVALID_DATA` (13) if unset. Calls `NtCreateWexAccountToken(WEX_TOKEN_NEGOTIATE)` then `NtCreateWexAccountToken(WEX_TOKEN_ISSUE)` with S1 204-byte envelope. Verifies returned token: Primary, Full elevation, High integrity, caller session, distinct nonzero AuthenticationId, Alice's mapped SID. Closes handles and fails closed on any mismatch. Retains `s10_context` for Stage 4. |
| **Stage 4** | `load_profile` | **Wired (Real NTDLL export)** | Requires Stage 2 `account_key`, `map_generation`, and `storage_generation`; fails closed with `ERROR_INVALID_DATA` (13) if unset. Calls `NtLoadWexProfileEx(WEX_HKU_LOAD)` with 48-byte S12/S13 record and `s10_context`. Retains `lease_id` (generation) for transfer and unload. Closes `s10_context` on completion. Returns HKU root key handle. |
| **Stage 5** | `prep_launch` | **Wired (Real launch_prep)** | Requires Stage 2 `account_name`, `account_key`, `map_generation`, and `storage_generation`; fails closed with `ERROR_INVALID_DATA` (13) if unset. Calls `wex_prepare_launch` from `launch_prep.c` with explicit same-server inputs: Bob's host UID/GID from caller, profile record, cwd. Real-UID switch requests denied with `ERROR_NOT_SUPPORTED`. Inherited environment rejected with `ERROR_INVALID_PARAMETER`. Builds sanitized Windows environment block for `CreateProcessAsUserW`. |
| **Stage 6** | `verify_image` | **Real Subsystem** | Calls the real broker routine `verify_image(req)` (`main.c:629`), checking held file handle, volume serial, file index, timestamps, and SHA-256 digest against frozen request metadata. |
| **Stage 7** | `create_process_suspended` | **Real Subsystem** | Invokes Win32 `CreateProcessAsUserW` with `CREATE_SUSPENDED`, `CREATE_UNICODE_ENVIRONMENT`, `CREATE_NEW_CONSOLE`, `EXTENDED_STARTUPINFO_PRESENT`, and `PROC_THREAD_ATTRIBUTE_PARENT_PROCESS` set to `req->caller`. |
| **Stage 8** | `verify_child` | **Real Subsystem** | Opens child token with `OpenProcessToken`, verifies `TokenSessionId` matches caller session and `TokenUser` matches Alice's mapped SID (`req->alice_sid`). |
| **Stage 9** | `arm_lease` | **Real Subsystem** | Calls the real wexhost `pLeaseArm(req->lease, pi.dwProcessId)` routine to bind Linux file lease to the created child PID. |
| **Stage 10** | `duplicate_handle` | **Real Subsystem** | Calls Win32 `DuplicateHandle(GetCurrentProcess(), pi.hProcess, req->caller, &remote, SYNCHRONIZE \| PROCESS_QUERY_LIMITED_INFORMATION, FALSE, 0)`. |
| **Stage 11** | `transfer_lease` | **Wired (Real NTDLL export)** | Calls `NtLoadWexProfileEx(WEX_HKU_TEST_TRANSFER)` (op 10) with child process handle and Alice token. Note: on the server side (`wine/server/registry.c:3476-3493`), this operation is gated behind `#ifdef WEX_E1S14_CRASH_TEST`. |
| **Stage 12** | `check_pre_resume` | **Real Subsystem** | Invokes real `check_binding(req, req->caller_pid, NULL, FALSE)` and verifies that neither `stop_event` nor `req->lease_event` has fired. |
| **Stage 13** | `resume_thread` | **Real Subsystem** | Enters `table_cs` critical section, performs final binding and stop event validation, and executes `ResumeThread(pi.hThread)`. |

### Cleanup / Rollback Hooks

| Hook | Status | Behavior |
|---|---|---|
| `terminate_child` | **Real Subsystem** | Calls `TerminateProcess(process, 1)` and `WaitForSingleObject(process, 5000)`. |
| `close_remote_handle` | **Real Subsystem** | Calls `DuplicateHandle(caller, remote, NULL, NULL, 0, FALSE, DUPLICATE_CLOSE_SOURCE)`. |
| `unload_profile` | **Wired (Real NTDLL export)** | Closes broker's local `hkey` handle, then calls `NtLoadWexProfileEx(WEX_HKU_UNLOAD_S14)`. Tolerates 'busy' (`STATUS_RESOURCE_IN_USE` 0xc0000708, `STATUS_CANNOT_DELETE` 0xc0000121, `STATUS_SHARING_VIOLATION` 0xc0000043) by recording `s_unload_busy = TRUE` and `s_unload_busy_lease_id` for S19 rollback instead of looping. |
| `close_handle` | **Real Subsystem** | Calls `CloseHandle(handle)`. |
| `free_buffer` | **Real Subsystem** | Calls `free(ptr)`. |
