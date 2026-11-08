<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S17: Credential Dialog, Bound Confirmation, and Serialized Prompt Queue

Survey and design for Work Package E1-S17 of [E1-S](E1_S_STANDARD_USERS.md),
following [E1 decisions](E1_DECISIONS.md), [S1 contracts](E1_S1_CONTRACTS.md),
[S5 broker records](E1_S5_BROKER_RECORDS.md), [S7 PAM worker](E1_S7_PAM_WORKER.md),
[S8 evidence](E1_S8_EVIDENCE.md), and [S15 launch prep](E1_S15_PROFILE_ENV.md).

Wine-only identity boundary: The elevated target process executes under the caller's (Bob's)
host Unix UID/GID in Bob's prefix and session with an authenticated administrator's (Alice's)
Wine token and fresh logon ID. The credential prompt must explain this boundary explicitly:
administrator rights apply inside this Wine prefix only, no host/root access, and Bob's
host files and account remain unchanged.

---

## 1. Codebase Survey: Existing Elevation Consent & Dialogs

### 1.1 Broker Elevation Prompt Path (`src/programs/wexelevate/main.c`)

| Function / Symbol | Lines | Existing implementation and findings |
|---|---|---|
| `CONSENT_EXE` | `78-80` | Evaluates to `consent_test.exe` if `WEXELEVATE_TEST_APPROVAL` is defined; otherwise `consent.exe`. |
| `consent_timeout` | `845-854` | Evaluates to registry override `ConsentTimeoutMs` in lab builds, or default `APPROVAL_TIMEOUT` (120,000 ms = 120 s). |
| `approve_consent` | `861-923` | Click consent execution for filtered administrators. Passes `/broker`, `/ppid <broker_pid>`, `/secure` (if secure desktop), `/image "<req->image>"`, `/cmd <req->cmdline>` to `consent.exe`. Inherits no handles. Switches desktop via `secure_enter()`. Waits for `pi.hProcess`, `req->caller`, `stop_event`, `req->lease_event`. **Vulnerability/Contract Gap**: Line 921: `if (code == ERROR_SUCCESS) return ERROR_SUCCESS;` trusts process exit status of child as authorization! It has no authenticated-account result, no bound confirmation record, and no secret-handling channel. |
| `approve_stub` | `926-955` | Test stub used when no policy is configured; executes `TEST_STUB` and trusts exit code 0. |
| `approve` | `958-971` | Dispatches to `approve_consent()`. Rejects non-click-consent policy with `ERROR_NOT_READY`. |
| `identify_caller` | `1050-1090` | Rejects non-filtered-admin callers (`elevation_type != TokenElevationTypeLimited`) with `ERROR_NOT_SUPPORTED` (50). Standard-user elevation remains unreachable. |
| `handle_request` | `1658-1691` | Calls `identify_caller`, `parse_request`, `table_add`, `freeze_image`, `check_binding`, `approve`, `check_binding`, `table_consume`, `verify_image`, `pLeaseArm`, and `launch`. Concurrent `client_thread` instances execute `handle_request` without serialization across sessions/desktops (H3b is open). |
| `client_thread` | `1693-1721` | Spawns per incoming pipe connection; reads request and dispatches `handle_request` concurrently. |

### 1.2 Broker Records and Contract Layouts (`src/programs/wexelevate/e1s.h`)

| Structure / Helper | Lines | Existing implementation and findings |
|---|---|---|
| `enum e1s_state` | `32-36` | `E1S_QUEUED`, `E1S_PROMPTING`, `E1S_AUTHENTICATING`, `E1S_IDENTITY_READY`, `E1S_CONSUMED`, `E1S_LAUNCH_PREPARED`, `E1S_COMMITTED`, `E1S_CANCELLED`. |
| `struct e1s_answer` | `80-89` | S1 Record Kind 2 layout: `kind` (2), `version` (1), `length` (156), `caps` (0x3ff), `id`, `attempt`, `round`, `index`, `answer_kind` (1=text, 2=confirm, 3=cancel), `nonce` (32 B), `binding_digest` (32 B), `request_digest` (32 B), `channel` (16 B), `payload_len` (0 for broker channel). |
| `e1s_answer_matches` | `261-269` | Validates that an incoming answer record matches expected attempt, round, index, nonce, binding digest, request digest, and channel ID, rejecting duplicate consent (`!a->consent`). |
| `e1s_broker_step` | `288-299` | Strictly monotonic state advance under lock: `QUEUED -> PROMPTING -> AUTHENTICATING -> IDENTITY_READY -> CONSUMED -> LAUNCH_PREPARED -> COMMITTED`. Requires `a->consent` and `evidence_ref` before reaching `IDENTITY_READY`. |

### 1.3 Consent Program (`src/programs/consent/consent.c`)

| Function / Symbol | Lines | Existing implementation and findings |
|---|---|---|
| `struct consent_data` | `25-44` | Holds command line, image, details, flags (`broker`, `secure`), `parent_pid`, `progname`, `publisher`, `origin`, `is_verified`, `credentials_mode`, `details_visible`. |
| `parse_command_line` | `172-240` | Parses `/hwnd`, `/broker`, `/secure`, `/ppid`, `/testid`, `/image`, `/cmd`. |
| `is_credentials_mode_required` | `334-358` | Checks thread token elevation type and `ConsentPromptBehaviorAdmin` registry policy. |
| `test_hook_*` | `360-450` | `WEXCONSENT_TEST_HOOK` test harness hooks (writes `.shown` file, polls `.approve` / `.cancel`). |
| `consent_dlg_proc` | `452-541` | Dialog proc for `IDD_CONSENT`: handles `WM_INITDIALOG`, `WM_DRAWITEM` (banner), `WM_COMMAND` (`IDYES`, `IDNO`, `IDC_UAC_DETAILS_BTN`). |
| `wWinMain` | `553-600` | Lines 562-570: checks `data.credentials_mode`. If true, issues warning `"Administrator credential authorization is not available.\n"` and terminates with `ERROR_NOT_SUPPORTED` (50). |

### 1.4 Consent Resources (`src/programs/consent/consent.rc` & `resource.h`)

| Resource / ID | Lines | Existing implementation and findings |
|---|---|---|
| `IDD_CONSENT` | `consent.rc:18-35` | Standard click-consent dialog layout with banner, program name, publisher, origin, details button, Continue (`IDYES`) and Cancel (`IDNO`). |
| `IDD_CREDENTIALS` | `consent.rc:37-56` | Early placeholder template: contains banner, program name, publisher, prompt text (`"To continue, type an administrator password, and then click Yes."`), user tile, combobox `IDC_UAC_ACCOUNT`, password edit `IDC_UAC_PASSWORD`, error text, Yes (`IDYES`) and No (`IDNO`). **Gaps**: Lacks the required Wine-only profile and host-access explanation text; uses an account combobox instead of typed account entry; lacks account-name validation; lacks buffer clearing; and is not wired to a bound conversation pipe. |
| `IDC_UAC_*` | `resource.h:18-32` | Defines `IDC_UAC_BANNER` (1001), `IDC_UAC_PROGRAM` (1003), `IDC_UAC_PUBLISHER` (1004), `IDC_UAC_ACCOUNT` (1009), `IDC_UAC_PASSWORD` (1010), `IDC_UAC_PW_LABEL` (1011), `IDC_UAC_ERROR` (1012), `IDC_UAC_PROMPT` (1014). |

### 1.5 Account Validation Authority (`wine/server/accounts.c`)

| Function | Lines | Existing implementation and findings |
|---|---|---|
| `valid_name` | `77-86` | S4 account-map name validation: length 1..63; cannot start with `-`; cannot be `.` or `..`; character set strictly restricted to `[a-zA-Z0-9_.-]`; trailing `$` allowed only as the last character (`i == len - 1`). Rejects path separators, control characters, quotes, and markup injection. |

### 1.6 PAM Worker and S8 Evidence Protocol (`tools/e1s7/pam-worker.c` & `tools/e1s6/evidence.py`)

| Component | Lines | Findings |
|---|---|---|
| `pam-worker.c` | `74-90` | Bounded seqpacket conversation: 72-byte header (`E1S7PAM\0`, version=1, type, length, request_id, attempt, round, index, style, nonce). Types: 1=START, 2=PROMPT (to child), 3=ANSWER (from child), 4=CANCEL, 5=RESULT. |
| `pam-worker.c` | `169-192` | Wipes secret buffers with `explicit_bzero` on every reuse, callback return, cancellation, and exit. |
| `evidence.py` | `98-106` | Native quotas: 3 attempts per request, 120s total request deadline, 1 active authentication per (UID, session). |

---

## 2. E1-S17 Design and Architecture

### 2.1 Protected Request-Specific Child Process

1. **Broker Spawning and Argument Hygiene**:
   - The broker spawns the credential dialog process (`consent.exe /broker /credentials ...`) directly as its child under the service's SYSTEM token on the target desktop (`WinSta0\Winlogon` or `WinSta0\Default`).
   - The command line passed to `CreateProcessW` contains **strictly broker-controlled parameters**:
     - `"<system_dir>\consent.exe" /broker /credentials`
     - `/ppid <broker_pid>` (broker PID for parent liveness watch)
     - `/request %08lx%08lx` (64-bit request ID)
     - `/token <32-char-hex>` (128-bit bound conversation token / channel ID)
     - `/channel <inherited_handle>` (numeric handle of inherited duplex communication pipe)
     - `/secure` (present only if running on `WinSta0\Winlogon`)
   - **Absolute Rejection of Caller Input**:
     - The broker NEVER passes caller-supplied command-line text, arguments, working directory, or account names on the dialog child's command line or environment.
     - Any descriptive information (e.g., verified program path from the held read-leased descriptor, verified requester name from the authenticated caller token) is passed strictly over the authenticated internal conversation channel.

2. **Wine-Only Explanation Text**:
   - The dialog UI displays the exact boundary decided in [E1 decisions](E1_DECISIONS.md) and [E1-S](E1_S_STANDARD_USERS.md):
     > *"The administrator account's rights apply inside this Wine prefix only, with no host or root access. The requesting user's host account and files are unchanged. Settings for this program are stored in this Wine prefix only."*
   - If the request is configured to run on an ordinary desktop rather than the secure desktop, the dialog prominently displays an alert:
     > *"Warning: Prompt running on ordinary desktop (insecure desktop policy)."*

3. **Typed Account Entry with S4 Validation**:
   - The dialog replaces the draft combobox with a clean text edit control (`IDC_UAC_ACCOUNT`).
   - Account names entered by the user are validated against the strict S4 rules before transmission:
     - 1 to 63 ASCII characters.
     - Allowed characters: `a-z`, `A-Z`, `0-9`, `_`, `-`, `.`.
     - First character cannot be `-`.
     - Cannot be `.` or `..`.
     - Trailing `$` permitted only as the final character (`i == len - 1`), with at least one preceding character.
     - Immediate rejection of any path characters (`/`, `\`), null bytes, quotes, spaces, control characters, or markup (`<`, `>`, `&`).
   - Invalid names display a visible error on `IDC_UAC_ERROR` without being dispatched across the pipe.

4. **Masked Password Entry and Buffer Wiping**:
   - The password edit control (`IDC_UAC_PASSWORD`) has `ES_PASSWORD`.
   - Clipboard operations (cut/copy) are suppressed on the credential field.
   - All memory buffers holding passwords, account strings, or marshaled message payloads are cleared using `SecureZeroMemory` (Win32) or `explicit_bzero` (POSIX) immediately after message transmission, on dialogue cancellation, and during dialog tear-down.

5. **Buttons and Navigation**:
   - **Confirm** (`IDYES` / `IDOK`): Validates the account name, constructs the bound confirmation message, transmits it over the channel, and closes.
   - **Cancel** (`IDNO` / `IDCANCEL`): Emits a bound cancel message over the channel and closes.

---

### 2.2 Bound Conversation and Confirmation Protocol

1. **Transport**:
   - An inherited duplex pipe between broker and dialog child.
   - The broker creates the pipe pair via `CreatePipe` with inheritance enabled on the child end, passes the handle via `/channel`, and retains the broker end.

2. **S1 Record Layouts and Message Sequence**:
   - Follows S1 contracts (`docs/E1_S1_CONTRACTS.md`):
     - Common header: `kind:u16, version:u16 (=1), length:u32, required_caps:u64 (=0x3ff)`.
   - **Message 1 (Broker -> Dialog: Dialog Init)**:
     - `kind: 7` (Registration / Init)
     - Passes request ID, attempt, round, index, 32-byte nonce, 32-byte binding digest, 32-byte request digest, 16-byte channel token, verified image path, requester account name, and desktop type.
   - **Message 2 (Dialog -> Broker: Credential / Account Submission)**:
     - Passes request ID, attempt, nonce, channel token, validated typed account name (1..63 B), and credential payload (forwarded by broker to PAM worker).
   - **Message 3 (Dialog -> Broker: Confirmation / S1 Answer Kind 2)**:
     - `kind: 2` (Answer), `version: 1`, `length: 156`, `required_caps: 0x3ff`.
     - `request_id: u64`
     - `attempt: u32`, `round: u32`, `message_index: u32`
     - `answer_kind: u32 = 2` (CONFIRM)
     - `nonce: [32]byte`, `binding_digest: [32]byte`, `request_digest: [32]byte`, `channel_id: [16]byte`
     - `payload_len: 0`
   - **Message 4 (Dialog -> Broker: Cancellation / S1 Answer Kind 2)**:
     - Same as above, with `answer_kind: u32 = 3` (CANCEL).

3. **Helper Exit Never Approves Rule**:
   - The broker verifies receipt of the exact S1 Answer record (Kind 2, version 1, `answer_kind = 2`) with matching request ID, attempt, nonce, digests, and channel token.
   - If the dialog child exits (exit code 0, non-zero, crash, or kill) without the broker having successfully received and validated the explicit Confirm record:
     - **The broker unconditionally treats the request as DENIED** (`ERROR_CANCELLED`).
     - Process exit status is NEVER an authorization grant.

---

### 2.3 Shared Serialized Prompt Queue (H3b)

1. **Concurrency and Serialization Boundary**:
   - Prompt presentation must be strictly serialized: **one active prompt at a time per desktop and session**.
   - Multiple callers requesting elevation simultaneously are placed in a FIFO queue for that session/desktop.

2. **Quota and Starvation Limits**:
   - **Per-caller queue limit**: At most **2** queued requests per caller (identified by caller primary token / logon LUID). If exceeded, subsequent requests are immediately rejected with `ERROR_TOO_MANY_CMDS` (or `ERROR_BUSY`).
   - **Per-session queue limit**: At most **8** requests total queued for a single session.
   - **Global broker queue limit**: At most **32** requests across all sessions.

3. **Timeout and Cancellation Lifecycle**:
   - Each queue entry inherits the request's monotonic deadline (e.g. 120 seconds total from admission).
   - While queued:
     - If deadline expires (`now >= deadline`): Entry is dequeued, marked `TIMED_OUT`, and fails closed (`ERROR_TIMEOUT`).
     - If caller disconnects or exits: Entry is dequeued, marked `CANCELLED`, and fails closed (`ERROR_CANCELLED`).
     - If broker receives `SERVICE_CONTROL_STOP`: All queued entries are cancelled.
     - A cancelled or timed-out queue entry NEVER reaches the active state and NEVER approves.
   - When the active prompt finishes (confirm, cancel, crash, or timeout):
     - The prompt slot is released.
     - The next waiting FIFO entry for that session is signaled (`ready_event`) and becomes active.

---

## 3. Implementation Plan & File Organization

1. **Broker Queue & Protocol Helpers**:
   - `src/programs/wexelevate/cred_dialog.h`: Pure interfaces for S4 account validation, memory wiping, S1 answer record construction/matching, and the serialized prompt queue.
   - `src/programs/wexelevate/cred_dialog.c`: Pure C implementation of account validation, wiping, queue operations, and conversation state machine.
2. **Consent Dialog UI**:
   - `src/programs/consent/resource.h`: Extended control IDs for typed account edit, explanation label, error text.
   - `src/programs/consent/consent.rc`: Updated `IDD_CREDENTIALS` resource containing the Wine-only explanation, typed edit control, masked password, and buttons.
   - `src/programs/consent/consent.c`: Implementation of credentials mode handling the bound conversation pipe, S4 validation, masked entry, buffer wiping, and bound confirm/cancel dispatch.
3. **Tests**:
   - `src/programs/wexelevate/test_cred_dialog_main.c`: Native C unit test exercising S4 account validation, buffer wiping, state machine, queue FIFO ordering, per-caller quota, cancel/timeout handling, and early exit denial.
   - `scripts/test_credential_dialog.sh`: Bash test harness running in `wexbuild` WSL checking production build exclusion (no test strings), lab build execution, and headless Xvfb rendering.
4. **Stock Changes**:
   - No stock Wine files modified; project files only.
