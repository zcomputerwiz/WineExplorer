# Changes to stock Wine / third-party code

## E1-S18e: registry.c reads admin_group like accounts.c (2026-11-08)

The server's administrator-group parser (`admin_group_name` in `server/registry.c`)
disagreed with the accounts-map parser (`read_policy` in `server/accounts.c`): it took
the first `admin_group=` line and accepted an empty or invalid name. Now a repeated
`admin_group=` line, or an empty or invalid name (the same `valid_name` rule as
accounts.c, copied not linked), makes the configuration invalid and nobody is an
administrator; a missing key still means `sudo`, and an unsafe file (not a regular
root-owned file, group/other writable, or a symlink) keeps the `sudo` default.

* `server/registry.c`: `valid_name`, `admin_group_name`, `user_is_administrator`.

## E1-S18e R13: elevated process does not inherit the caller's job or debugger (2026-11-09)

The filtered-admin broker names the caller as designated parent (console and window placement).
Stock `new_process` then also puts the child in the parent's job and attaches the parent's
debugger when it debugs its children, so a caller could stall or kill the elevated process
through them. Now, when the creating process is the verified elevation broker and the request
names a parent and an explicit token, the server skips job inheritance (and its breakaway
check) and debugger inheritance, and refuses such a request that also passes a debug object, a
job list or handle inheritance. Ordinary CreateProcess is unchanged. The broker is recognised
by the actual mapped file (device and inode of the machine-root `system32/wexelevate.exe`), the
system role and its SYSTEM primary token; this check is in production builds (the lab S6 hook
also accepts the test broker image under `WEX_E1S6_TRANSPORT_TEST`). No protocol change.
After review (docs/E1_S18E_R13_REVIEW.md): recognition is decided once, on the first executable
image, and holds only while the process keeps the primary token it had then; the syswow64 broker
image is recognised too; an isolated launch is refused from an impersonating broker thread and
gets no standard handles from the caller. Still inherited for placement: window station and
desktop, a console allocation the broker asks for, and processor affinity.

* `server/process.c`: `isolate` in `new_process`.
* `server/process.h`: `wex_broker_image`.
* `server/sysrun.c`, `server/sysrun.h`: broker image recognition in `sysrun_main_image`,
  `sysrun_process_is_broker`.

## E1-S18e broker storage-generation query (2026-11-08)

Adds broker-only `WEX_HKU_STORAGE_QUERY=12` to existing request 325 (review R3
and the storage-generation gap in `docs/E1_S18_REVIEW.md`). Protocol 979 and all
wire layouts stay unchanged. The broker calls it after S10 issuance and before
LOAD with the LOAD record, storage generation 0. The server checks the S6 broker
gate, the original S10 context and issued token (same checks as LOAD, without
burning the one-shot LOAD attempt), then discovers the S12 generation from its
own `config_dir_fd` with the same no-create validator LOAD uses. Discovery takes
no SID hive lock, never opens or creates `.hive.lock`, opens the hive read-only
and denies missing, unprovisioned, recovery or substituted profiles. LOAD keeps
its nonzero precondition and exact generation comparison.

* `include/wine/wex_profile.h`: op 12 and its contract.
* `server/wex_profile_storage.c`, `.h`: `discover` mode of the reopen and
  `wex_profile_query_generation`; `validate_profile` can open the hive read-only.
* `server/token.c`, `server/e1s_token.h`: `token_query_e1s_profile`.
* `server/registry.c`: op 12 handler.
* `dlls/ntdll/reg.c`: validate op 12's keyless managed=1/nonzero generation reply.
* `dlls/advapi32/tests/wex_hku.c`: normal-server op 12 denial.

## E1-S14b disabled production lease commit (2026-11-08; static only)

Adds broker-only `WEX_HKU_COMMIT_PROCESS=11` to existing request 325; protocol
979 and all wire layouts stay unchanged. It requires the LOAD S6 broker gate,
the original S10/LOAD context bound to the server's lease/request/generation,
state exactly 1, and a local alive child created with that exact S10-issued
token. Initial CREATE_SUSPENDED, never-resumed and single-thread proofs precede
atomic state-2/process-accounting publication. Every denial leaves state intact.
No production define, build enablement or installer policy changes.

Stock-Wine files changed in this package:

* `include/wine/wex_profile.h`: op 11 and four-byte LE child-handle input contract;
  retired op 10 stays reserved and denies.
* `server/token.c`, `server/e1s_token.h`: server-only lease binding in retained
  S10 contexts; read-only broker/context/token/request/server validation and
  installed-primary lineage/authentication/version/shape checks.
* `server/process.c`, `server/process.h`: retain/release the exact explicit
  creation token; validate local process accounting, alive/pre-entry state and
  no prior binding; committed SID lifetime remains busy after primary replacement.
* `server/thread.c`: track initial suspended creation and permanently clear
  eligibility after resume-to-zero or another thread; resuspension cannot restore it.
* `server/registry.c`, `server/e1s_profile.h`: bind LOAD's context, remove the
  unchecked exported internal transfer and lab TEST_TRANSFER implementation;
  validate then atomically publish the lease/process binding. Existing durable
  save, unload, last-user and descendant accounting remain authoritative.
* `dlls/ntdll/reg.c`: validate op 11's keyless managed=1/generation=0 reply.
* `dlls/advapi32/tests/wex_hku.c`: normal-server op 11 denial and cleared outputs
  through both wrappers, including a correctly sized payload.

Project S18 default_transfer_lease now uses op 11; successful LOAD retains its
S10 context until existing commit/rollback cleanup, failed LOAD closes it.
The export fake asserts the context and exact child payload. Harness changes
are exactly `scripts/e1s13_hku_checks.h`, `scripts/lib/e1s13-fixture.sh`,
`scripts/e1s10_token_probe.c`, `scripts/test_e1s10_token.sh` (force thread.o).
See [S14b gate table and complete unrun matrix](E1_S14_LEASES.md#e1-s14b-broker-only-production-lease-commit-2026-11-08).

No build, test, harness, generator, Wine, WSL, VM or service was run; no commit.
The 92 PASS baseline predates this patch. Fake tests and both-ABI old/new lab
cases, actual no-crash-define commit, normal-server Wine tests and S18 end-to-end
execution remain unverified. Other S18 authentication/enablement gaps remain.

## E1-S18 disabled broker Wine-only integration (2026-11-07)

Survey, implementation and verification for Work Package E1-S18 of [E1-S](E1_S_STANDARD_USERS.md).
See [survey, architecture, trust decisions and failure handling](E1_S18_BROKER_INTEGRATION.md). Disabled by default;
unreachable from production broker path (gated behind new lab define `WEXELEVATE_E1S18` and runtime policy `EnableStandardUserElevation`).

Zero stock Wine changes. No changes to `wine/server`, `wine/dlls`, protocol, or Wine submodule files.

Project changes:
* `src/programs/wexelevate/e1s18_broker.h` and `e1s18_broker.c`:
  - Separate supported standard-token branch for `TokenElevationTypeDefault` callers.
  - S1 Negotiated Capability Enforcement: all 10 S1 capabilities (`0x3ff`: `H2_OPENER`, `MESSAGE_PROVENANCE`, `BINDING_V1`, `DIALOG_V1`, `VERIFIER_V1`, `EVIDENCE_V1`, `ACCOUNT_MAP_V1`, `TOKEN_V1`, `PROFILE_V1`, `COMMIT_GUARD_V1`) must be present; missing any capability denies with `ERROR_NOT_SUPPORTED` (50), never falling back to filtered-admin or weaker paths.
  - Runtime policy check: `HKLM\Software\WineExplorer\Elevation\EnableStandardUserElevation` (DWORD, default 0/off).
  - Rejection of inherited environment: `WEXELEVATE_ENV_INHERIT` strictly rejected for standard users with `ERROR_INVALID_PARAMETER` (87).
  - Canonical SHA-256 digest calculations for binding digest (`"E1S1-BIND\0"`), request digest (`"E1S1-REQ\0"`), and environment digest (`"E1S1-ENV\0"`).
  - S17 Prompt Queue & Bound Credential Dialog: serialized queue per session/desktop, FIFO ordering, per-caller (<=2), per-session (<=8), global (<=32) quotas; dialog spawned on `WinSta0\Winlogon`; explicit S1 Kind 2 Confirm answer required; child exit or crash fails closed.
  - S8 Single-Use Evidence Verification & Redemption: verifies 220-byte S1 Kind 3 evidence record against frozen digests, nonce, and Bob host UID; burns reference atomically.
  - S10 Account Token Issuance: dispatches `NtCreateWexAccountToken(WEX_TOKEN_ISSUE)`; verifies returned token is Primary, Full elevation, High integrity, distinct nonzero AuthenticationId, caller session, and Alice SID.
  - S13/S14 Profile Load & Lease: calls `load_e1s_profile(WEX_HKU_LOAD)` to load `HKU\<Alice SID>`, acquires S14 profile lease in state 1 (setup), and publishes Volatile Environment with Alice's 9 variables.
  - S15 Launch Preparation: calls `wex_prepare_launch()`, enforces Bob host UID (`requested_uid == bob_uid`), verifies cwd containment without symlink traversal, builds clean Alice Windows environment and sanitized Bob Unix environment.
  - Suspended Child Creation: verifies held image identity and hash (`verify_image`), creates child suspended in caller's server and session with `CreateProcessAsUserW`, Bob parent attribute, and normal desktop `WinSta0\Default`.
  - Installed Child Verification: queries child token to confirm Alice SID and caller session; arms image read lease on child PID (`pLeaseArm`).
  - Limited Handle Duplication: duplicates `SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION` to caller; transfers profile lease to state 2 (`e1s_profile_commit_process`).
  - Guarded Resume: re-checks caller binding, stop event, and lease event under `table_cs`; calls `ResumeThread` exactly once.
  - Explicit Rollback / Cleanup: on failure at any stage, terminates suspended child, closes remote handle in caller table (`DUPLICATE_CLOSE_SOURCE`), unloads profile lease, closes token, wipes secrets with `e1s18_wipe`, and releases prompt queue slot.
  - Pluggable `struct e1s18_ops` operations table enabling mock injection and stage-by-stage testing without live wineserver.
* `src/programs/wexelevate/main.c`:
  - Hook in `identify_caller()`: standard users (`TokenElevationTypeDefault`) gated under `#ifdef WEXELEVATE_E1S18`, requiring policy enabled and all 10 S1 capabilities present; filtered admin (`TokenElevationTypeLimited`) byte-for-byte unchanged in behavior.
  - Hook in `handle_request()`: routes standard user requests to `e1s18_handle_standard_request()`.
  - Conditionally includes `e1s18_broker.h` and `e1s18_broker.c` when `WEXELEVATE_E1S18` is defined.
* `src/programs/wexelevate/test_broker_e1s18_main.c` and `scripts/test_broker_e1s18.sh`:
  - Native C unit test runnable as root in WSL `wexbuild`.
  - Verifies production binary disabled-by-default behavior and exclusion check.
  - Lab test suite verifies: disabled-by-default policy, capability enforcement across all 10 bits, inherited env rejection, exact 13-stage success ordering, single guarded resume, complete 13-stage failure & rollback matrix, and filtered-admin preservation.
* `scripts/test_sysrun_server.sh`:
  - Added `WEXELEVATE_E1S18` to the production build exclusion check next to existing test defines.

## E1-S17 disabled credential dialog, bound conversation and prompt queue (2026-11-07)

Survey, implementation and verification for Work Package E1-S17 of [E1-S](E1_S_STANDARD_USERS.md).
See [survey, architecture, files and results](E1_S17_CREDENTIAL_DIALOG.md). Disabled by default;
unreachable from the production broker path (gated for S18 integration).

Zero stock Wine changes. No changes to `wine/server`, `wine/dlls`, protocol, or Wine submodule files.

Project changes:
* `src/programs/wexelevate/cred_dialog.h` and `cred_dialog.c`:
  - Pure typed account name validation implementing S4 account-map rules (`wine/server/accounts.c:valid_name`): length 1..63, character set `[a-zA-Z0-9_.-]`, no leading dash, no `.` or `..`, trailing `$` permitted only as last char for machine accounts, rejection of path separators, control characters, quotes, spaces, and markup injection.
  - Secure memory wiping primitive `wex_secure_wipe` using `SecureZeroMemory` on Windows and `explicit_bzero` / non-elided volatile zeroing on POSIX, with buffer verification `wex_is_buffer_zeroed`.
  - Versioned S1 Answer Record (Kind 2, version 1, length 156, required_caps 0x3ff) serialization and verification: matching request ID, attempt, round, index, nonce, binding digest, request digest, and channel ID.
  - Conversation state machine (`INIT -> PROMPTING -> AUTHENTICATING -> CONFIRMED -> CANCELLED / TIMED_OUT / DENIED`). Helper exit never approves: process exit (code 0 or otherwise) without an explicit valid signed-by-binding Confirm message fails closed (`WEX_CONV_STATE_DENIED`).
  - Shared serialized prompt queue (H3b): strictly one active prompt per desktop/session, FIFO queueing, cancellation and deadline timeout handling (timed-out or cancelled entries never approve), and per-caller starvation DoS protection enforcing a limit of at most 2 queued entries per caller (`WEX_PROMPT_QUEUE_PER_CALLER_LIMIT = 2`), at most 8 per session, and at most 32 globally.
* `src/programs/consent/resource.h` and `consent.rc`:
  - Updated `IDD_CREDENTIALS` (260x215) with banner, program name, publisher, prominent Wine-only profile and host-access explanation text (`IDC_UAC_EXPLANATION`: "The administrator account's rights apply inside this Wine prefix only, with no host or root access. The requesting user's host account and files are unchanged."), insecure desktop policy warning label (`IDC_UAC_DESKTOP_WARN`), typed account name edit control (`IDC_UAC_ACCOUNT`), masked password edit control (`IDC_UAC_PASSWORD`), error label (`IDC_UAC_ERROR`), Yes (`IDYES`) and No (`IDNO`) buttons.
* `src/programs/consent/consent.c`:
  - Supported `/broker`, `/credentials`, `/request <id>`, `/token <token>`, `/channel <handle>`, `/pipe <pipename>`, and `/ppid <pid>`.
  - If `/credentials` is invoked standalone or without valid bound broker channel, fails closed with `ERROR_NOT_SUPPORTED` (50).
  - Handles `IDD_CREDENTIALS` with S4 account validation, password masking, immediate buffer wiping, S1 Confirm transmission over bound channel on `IDYES`, S1 Cancel transmission on `IDNO`/cancel, and helper exit fail-closed enforcement.
  - Test hook gated under `#ifdef WEX_CRED_DIALOG_TEST_HOOK` for lab-only input injection and status reporting. Production build contains zero test hook strings.
* `src/programs/wexelevate/test_cred_dialog_main.c` and `scripts/test_credential_dialog.sh`:
  - Native C unit test and test harness runnable as root in `wexbuild` WSL.
  - Verifies production binary disabled-by-default behavior and string-exclusion assertion (no test strings in production binary).
  - Exercises unit test suite in lab binary: account name validation, secure buffer wiping, conversation state machine, early exit denial, binding mismatch denial, cancel/timeout handling, prompt queue FIFO ordering, and prompt queue per-caller limit.
  - Exercises headless Xvfb runs with `scripts/cred_dialog_probe.c` verifying dialog rendering, Confirm action transmission, Cancel action transmission, invalid account rejection, and child kill denial.


Survey, implementation and verification for Work Package E1-S15 of [E1-S](E1_S_STANDARD_USERS.md).
See [survey, architecture, files and results](E1_S15_PROFILE_ENV.md). Disabled by default; nothing
active in normal builds, zero behavioral change for ordinary processes.

* `wine/server/registry.c`:
  When a managed profile is loaded in `e1s_load_hive`:
  Populates `Volatile Environment` subkey on `m->user` with Alice's 9 variables (`USERPROFILE`,
  `USERNAME`, `HOMEDRIVE`, `HOMEPATH`, `USERDOMAIN`, `APPDATA`, `LOCALAPPDATA`, `TEMP`, `TMP`),
  and standard session values (`CLIENTNAME`, `SESSIONNAME`, `HOMESHARE`).
  Marked with `KEY_VOLATILE` and without `KEY_E1S_PERSIST`, isolating it completely from durable
  hive serialization on disk via `e1s_write_keys`.
* `wine/dlls/userenv/userenv_main.c`:
  - `GetUserProfileDirectoryW` and `GetUserProfileDirectoryA`: gated under `WEX_E1S13_HKU_TEST`.
    Queries `NtLoadWexProfileEx(WEX_HKU_QUERY, ...)`. When and only when the effective/supplied
    token is a managed account token whose profile is loaded (`managed == 1 && gen > 0`), reads
    `USERPROFILE` from `Volatile Environment`. When managed but unloaded (`gen == 0`), fails
    closed with `ERROR_NOT_READY`. Unmanaged tokens execute stock Wine logic unchanged.
  - `CreateEnvironmentBlock`: gated under `WEX_E1S13_HKU_TEST`. When the token is managed, forces
    `bInherit = FALSE` (caller environment inheritance rejected) and populates Alice's 9 variables
    from Alice's profile and `Volatile Environment`, avoiding `profiles_dir + username` concatenation.
  - `ExpandEnvironmentStringsForUserW` and `ExpandEnvironmentStringsForUserA`: gated under
    `WEX_E1S13_HKU_TEST`. When token is managed, expands using Alice's managed environment block
    rather than the calling process environment.
* `wine/dlls/shell32/shellpath.c`:
  - `_SHGetUserProfilePath`: gated under `WEX_E1S13_HKU_TEST`. When the token is managed and
    loaded: for `CSIDL_PROFILE` (0x28), returns Alice's S12 profile path directly via userenv;
    for other CSIDLs, queries Alice's `User Shell Folders` and fails closed (`E_FAIL`) if
    missing, never falling back to HKLM or stock defaults.
  - `_SHExpandEnvironmentStrings`: gated under `WEX_E1S13_HKU_TEST`. When expanding `%USERPROFILE%`,
    checks if effective token is managed and expands to Alice's S12 profile path rather than
    appending `GetUserNameW()`.
* `wine/dlls/advapi32/tests/wex_hku.c`:
  Added normal-server test assertions for `GetUserProfileDirectoryW`, `CreateEnvironmentBlock`, and
  `ExpandEnvironmentStringsForUserW`, verifying ordinary process behavior is preserved and `.e1s`
  is never exposed to normal tokens.

Project changes:
* `src/programs/wexelevate/launch_prep.c` and `.h`: Pure, unit-testable launch preparation
  function `wex_prepare_launch` taking explicit same-server inputs, generating clean allow-list
  Windows environment, sanitized Unix environment (Bob's host identities retained, stripping
  forbidden variables `SSH_*`, `LD_*`, `WINE*`, `KRB5*`, `GPG_*`, `DBUS_*`), verifying
  descriptor-anchored working directory without symlinks/traversal, and hard rejection of
  real-UID transitions (`WEX_LAUNCH_PREP_ERR_REAL_UID_UNSUPPORTED`).
* `src/programs/wexelevate/test_launch_prep_main.c` and `scripts/test_launch_prep.sh`: Native C
  unit test harness runnable in `wexbuild` as root, covering allow-list, overrides rejected,
  inherited env rejected, real-UID rejected, sanitized Unix env, and CWD checks.
* `scripts/e1s13_hku_checks.h` and `scripts/test_e1s10_token.sh`: Extended with assertions that
  effective Alice token sees her profile path and variables, while peer sees Bob's.


## E1-S14 disabled managed hive leases and persistence (2026-11-07; static only)

Protocol **978 -> 979**, existing request 325 now has a 24-byte generation-bearing
reply; request remains 32 bytes, request count and 88-byte generator limit stay
unchanged. No production enablement or commit. See
[design, survey, statuses and unrun validation](E1_S14_LEASES.md).

* `wine/server/registry.c`: server-local canonical-key/exact-SID lease table,
  S1 fields and trusted ownership handoff; serialized phases and distinct
  conflicts; actual live-user/deleted-descendant/handle-safe unload, periodic
  last-user draining, checked managed save-branch registration and one durable
  saver for flush/shutdown/unload. Managed persistence remains isolated from
  Bob's branches; generic client-file save/unload deny. Detached generation
  proxies replace persistent references from client root caches. Managed save
  serialization preserves values/classes/explicit volatility and denies states
  whose descriptors or links the S13 format cannot restore.
* `wine/server/wex_profile_storage.c/.h`: stable per-SID flock shared with S12
  provisioners; load snapshot/lock/chain rechecks; descriptor-only marker/temp,
  file fsync, rename and directory fsync transaction; SID-wide interrupted-state
  denial without deleting recovery data. Additional crash hook is lab-only.
* `wine/server/process.c/.h`: retain an inventory including pending children and
  final native death; inspect actual primary/thread token references rather
  than token object refcounts. Managed process creation/primary replacement in
  the S13 lab denies absent registration before publication. Bob is unchanged.
* `wine/server/token.c`, `e1s_token.h`: obtain the lease request/server generation
  from the retained, revalidated S10 context, without new client authority.
* `wine/server/e1s_profile.c/.h`, `fd.c`, `file.h`: replace the unload TODO with
  actual manager integration; apply/query temporary-hive security, deduplicate
  file pins and retire unlinked pins only after server inode and anonymous/
  imported-descriptor references end; indeterminate reference checks retain pins.
* `wine/server/protocol.def`, `request_handlers.h`, `request_trace.h`,
  `include/wine/server_protocol.h`: consistent generation field/layout/trace and
  version; actual tools/make_requests regeneration remains unrun.
* `wine/include/wine/wex_profile.h`, `include/winternl.h`, `dlls/ntdll/reg.c`,
  `ntdll.spec`: cache and separate lab controls; private NtLoadWexProfileEx
  generation output with existing export ABI retained and all failure outputs
  cleared. No syscall/thunk table changes or generator-limit increase.
* `wine/dlls/kernelbase/registry.c`: include generation in the existing effective
  query/cache; bounded retired proxy entries avoid concurrent close/reuse and
  add no per-call IPC. Existing normal-client compile gates remain.
* `wine/dlls/userenv/userenv_main.c`: broker-gated managed profile proxies and
  checked successful unload; ordinary stubs retain their existing behavior.
* `wine/dlls/advapi32/tests/wex_hku.c`: normal-server denial/output clearing for
  all ten operations and both wrappers, including generation.

Project changes extend the existing S13/S10 harness, fixtures and probe checks,
update the shared `tools/wex-profile` descriptor cleanup, and add the S14 note
and decision-log status. All tests remain unrun in this sandbox.

## E1-S13 disabled managed HKU load (2026-11-07; static only)

S13 lab-failure repair (2026-11-07, uncommitted, static only):

* `wine/dlls/kernelbase/registry.c`: make `RegOpenCurrentUser` validate/clear its
  output and call `RtlOpenCurrentUser`. Remove self-impersonation and the empty
  predefined-HKCU fallback. Under enforced security, the former manufactured an
  Identification token that could not open itself; the latter then returned
  `HKEY_CURRENT_USER` as success, causing native key queries to fail. The RTL
  path resolves the effective user without changing impersonation and retains
  its compile-gated S13 QUERY/CURRENT checks. Ordinary callers use the existing
  RTL `NtCreateKey` path. Server authorization and managed caches are unchanged.
* `wine/dlls/advapi32/tests/registry.c`: query the returned current-user handle
  with `NtQueryKey(KeyNameInformation)` so pseudo-handle success is detected.
* `wine/server/accounts.c`: root-tool-only protection diagnostics identify the
  rejected directory/file and metadata predicate, including actual/expected
  map GID. Server replies/logging and all protection predicates remain intact.
  The shared provider also serves the project `wex-accounts` tool.

Project repairs add both-access-mask/no-impersonation/Identification-denial S13
probe checks; distinguish writer-lock contention from unsafe metadata in
`tools/wex-accounts`; and prevent the S10/S13 fixture from enabling automatic
account writers during user creation. Setup retains autosync by default; the
fixture passes `WEX_ACCOUNTS_AUTOSYNC=0`, uses its newly built account tool for
initialization, and refuses active foreign account units. See
[cause, evidence and unrun validation](E1_S13_LAB_FAILURE.md#static-repair-2026-11-07).

S12/S13 independent-review repair (working tree only, static review):

* `wine/dlls/kernelbase/registry.c`, `wine/dlls/ntdll/reg.c`: compile automatic
  current-user/classes hooks only with `WEX_E1S13_HKU_TEST`, including the
  kernelbase WoW64 union helper and explicit-token classes path. Normal registry
  calls no longer issue S13 queries. Keep query status separate from the managed
  flag: successful unmanaged replies and disabled-server replies may use stock
  paths; indeterminate token/query failures in the lab still deny. No permanent
  process verdict that could survive impersonation or token changes is cached.
  A missing private export reports PROCEDURE_NOT_FOUND and malformed successful
  replies report DATA_ERROR, keeping both out of the disabled-server fallback.
* `wine/dlls/kernelbase/registry.c`: cached managed roots request explicit
  `KEY_ALL_ACCESS`, retrying only ACCESS_DENIED with `KEY_READ`. Below-High
  read-only access can pass existing DACL/MIC checks without implicit maximum
  rights. Server `registry.c` and its maximum-access denial are unchanged.
  Record the raw-handle lifetime/close-reuse limitation for S14 in the cache
  comment; no invalidation/unload implementation is introduced.
* `wine/server/e1s_profile.c`: cache one verified unmanaged ancestry, with at
  most 129 pinned directory descriptors. Each hit rechecks every `..` edge by
  device/inode/mount identity, including the terminal root edge. Replacement,
  new security registration and failed verification discard the cache; a cold
  walk that cannot prove absence of a managed ancestor denies. Repeated opens
  avoid ancestor opens/closes and repeated secured-inode scans, while retaining
  per-edge statx work. No lexical profile-path bypass is added.

Project handoff: `scripts/test_e1s10_token.sh` forces both ntdll/kernelbase PE
registry objects with the S13 test define and relinks both ABIs only for S13.
It checks the wide union marker in lab/normal kernelbase DLLs. Existing dirty
S10/S11 fixture work is preserved. No build, runtime, benchmark or acceptance
claim; follow-up cases and limitations are in [the S13 report](E1_S13_HKU_LOAD.md#s12s13-review-repair-2026-11-07).

Protocol **977 -> 978**, appended request **325** `load_e1s_profile`.
Normal builds deny every operation. No installer, production caller, service
or environment switch enables it. See [survey, ABI and test handoff](E1_S13_HKU_LOAD.md).

* `wine/server/protocol.def`, `request_handlers.h`, `request_trace.h`,
  `include/wine/server_protocol.h`: fixed 32/16-byte request/reply, redacted
  48-byte storage reference, tables, unions and layout assertions. Existing
  88-byte protocol/generator limits stay unchanged; hand edits need regeneration.
* `wine/include/wine/wex_profile.h`, `include/Makefile.in`,
  `include/winternl.h`, `dlls/ntdll/reg.c`, `ntdll.spec`: pointer-free private
  `NtLoadWexProfile` PE wrapper, cleared outputs, managed current-user open
  without absent-key creation. No syscall/WoW64 thunk-table slot added.
* `wine/server/token.c`, `e1s_token.h`: server-only S10 account provenance,
  copied through token duplication, retained original issuance object, shared
  unchanged S10/S6 broker gate, one-shot profile attempt and repeated current
  S4/caller/deadline/endorsement validation. No ordinary token identity changes.
* New `wine/server/wex_profile_storage.c`, `.h`, `server/Makefile.in`: move
  S12's existing native implementation into shared server sources, preserving
  the tool via thin `tools/wex-profile/profile.c/.h` includes. Add a no-create,
  no-seed, no-cleanup reopen/recheck API with distinct absent-storage result
  and provisional held-inode exclusion. S12 provisioning remains separately
  gated; no server request invokes it.
* `wine/server/registry.c`: validated exact-SID HKU and `_Classes` registration,
  explicit Alice/SYSTEM/Administrators owner/DACL/High descriptors, inherited
  managed defaults, strict bounded relative text-hive load and failed-load
  rollback. Dynamic private HKCR union reads user-over-machine values/names;
  writes select Alice's classes. No Bob-default or startup/save-branch edits.
  Loaded roots reject deletion/unload; flush reports unsupported until S14.
  Union rename/notifications fail explicitly; managed implicit maximum-access
  opens below High integrity deny without changing Bob's default MIC path.
* New `wine/server/e1s_profile.c`, `.h`, plus `file.c` and `fd.c`: descriptor
  anchored Windows SD application/fsync/query-back on S12 storage and existing
  children, private inherited descriptors on later Wine opens/creates under
  registered parents, held-inode authority rather than standalone xattr trust,
  and open checks before truncation/disposition. Preserve Bob's Unix ownership
  and private modes. Native same-UID access remains the accepted limit.
  Internal `e1s_profile_unload_s14()` fails closed and keeps state until exit.
* `wine/dlls/kernelbase/registry.c`: separate managed HKCU/HKCR cache keyed by
  SID, TokenId and ModifiedId; effective-token view selection, explicit-token
  classes opens and shared WoW64 classes-cache handling. Bob's legacy cache
  and ordinary machine HKCR behavior are retained.
* New `wine/dlls/advapi32/tests/wex_hku.c`, tests `Makefile.in`: normal-server
  denial/output-clearing tests for every operation and malformed API lengths.
* `wine/dlls/userenv/userenv_main.c`: managed tokens open only pre-registered
  hives through the broker gate; ordinary default-build load stubs stay intact.
  Managed unload returns S14's unsupported result, never a false success.

Project lab additions: `scripts/test_hku_load.sh`, `lib/e1s13-fixture.sh`,
`e1s13_hku_probe.c`, `e1s13_hku_checks.h`; conditional S13 extensions in the
existing S10 probe/harness and updated protocol pin. Existing S10/S6/sysrun
work was preserved. This S13 pass made no build/runtime claim or commit; another
writer committed the in-progress tree (see the S13 note). S14/S15/S18 still gate
managed launching and durable profiles.

## E1-S12 disabled prefix profile storage (2026-11-07; static only)

Zero additional stock Wine changes. New `tools/wex-profile/profile.h`,
`profile.c` and `wex-profile.c` define a default-denied native storage API and
an explicitly compiled `WEX_E1S12_PROFILE_TEST` lab driver. They consume S4's
existing account snapshot type without changing accounts.c, tokens, registry,
userenv, shell32, protocol, server startup or Bob's ordinary profile behavior.
No production installer, service, capability or runtime switch enables it.

SID/incarnation storage, descriptor/mount/mapping checks, Bob-only Unix modes,
bounded offline default seeding, private temp recovery, no-replace atomic
publication and Alice/SYSTEM Windows ACL intent are documented in
[E1_S12_PROFILE_STORAGE.md](E1_S12_PROFILE_STORAGE.md). S13 still must apply and
verify Windows ACLs and load HKU; S14 owns leases/persistence. The S1 lease
fields are reserved and zero. `scripts/test_profile_storage.sh` supplies a
root-only temporary-root lab with crashes, contention and substitution cases.
Only static review, shell syntax parsing and whitespace checks here; Qwen must
compile and run the fixture in wexbuild. No Wine source was changed or commit made.

## E1-S6 acceptance diagnostics (2026-11-07; static only)

Round-3 follow-up: the control import failure is a project-side zero access
mask passed to Wine's existing `alloc_file_handle` path. Both wexhost imports
now request only `FILE_READ_ATTRIBUTES`, attributes zero. No further stock Wine
files, protocol layouts or access checks change. Project-only diagnostics
capture actual NTSTATUS/syscall errno (including private child failures),
D-Bus errors and bounded escaped native exception text/method context; late
child injection validates kill/wait instead of accepting their failure.
See the round-3 section of [E1_S6_ACCEPT_REVIEW.md](E1_S6_ACCEPT_REVIEW.md)
for evidence and the static re-review of all later cases. Runtime remains open.

`wine/server/named_pipe.c` and `wine/server/sysrun.c`: add distinct denial-site
audits under WEX_E1S6_TRANSPORT_TEST. Split existing combined comparisons without
relaxing authority, consumed-writer, image, token, generation or FD checks.
No protocol or token factory changes. Project fixture corrections, complete
route inventory and unresolved original failure attribution are recorded in
[E1_S6_ACCEPT_REVIEW.md](E1_S6_ACCEPT_REVIEW.md). Force rebuild named_pipe.o with
the test define too. No build, runtime acceptance or commit.

Follow-up: `sysrun.c:e1s6_get_fd` additionally distinguishes control/pin handle
lookup, FD-object lookup and F_DUPFD_CLOEXEC failure, recording NTSTATUS/errno
under the same test gate. Project broker I/O/image failures and native child
exit/proc/packet failures have finer gated diagnostics too. The exact failure
output is now read; error 13 still does not identify its underlying denial site.

## E1 combined-build/review repairs (2026-11-06; static only)

`wine/server/sysrun.c`: cast the S6 retained `grab_object` result to
`struct token *`, use `F_DUPFD_CLOEXEC` for lab transport fd duplicates and make
lab read/send/format-size comparisons explicitly signed/unsigned as appropriate.
All endorsement implementation remains behind WEX_E1S6_TRANSPORT_TEST/Linux.
`wine/server/token.c`: the restricted null-SID group allocation now reserves
at least `sizeof(struct group)` (the original allocation ended at one SID
subauthority, smaller than its declared C object), addressing the combined
build's array-bounds warning without changing the logical SID. The S10 factory,
context and fixture remain behind BOTH S10/S6 test defines; no further uncast
object-reference assignments were found in those gated blocks.

Project repairs: collision-free S7 `RES_*` names; worker executable-pin closure;
fixed prompt and bounded four-second failure/decoy policy, with S8's shared
attempt/concurrency path; locked CANCEL and per-request cancellation audit;
explicit D-Bus reply fd lifetime and pidfd-checked retained proc-directory
executable validation; MinGW-only S10 probe headers/GetProcAddress casts.
Native regression cases, production-exclusion assertions and clean-wexbuild
handoffs updated. No defines added to build/install files; no build/runtime
acceptance or commit. Full per-item verdicts and Qwen handoff:
[E1_BATCH_REPAIR.md](E1_BATCH_REPAIR.md).

The S4 review removal of `sync_account_map()` from wexhost `user_create()` is
intentional production behavior: its unprivileged fork could not run the
root-only tool. Do not re-add it. Enabled provisioning runs sync; the root path
unit, retry timer and oneshot service maintain it later. Non-systemd/custom-path
hosts need equivalent explicit root lifecycle integration, as S4 documents.

## E1-S10 disabled account-specific server token (2026-11-06)

S11 prerequisite review repair (2026-11-07, static only):
`server/token.c:e1s_install_consumed` now releases and clears its caller/primary
references immediately if the post-grab caller validation fails. The context
stays consumed and validator-cookie ownership still transfers only on success.
Existing lab guards are unchanged. Runnable matrix and remaining evidence:
[E1_S11_TOKEN_TESTS.md](E1_S11_TOKEN_TESTS.md).

`wine/server/token.c`, new `server/e1s_token.h`, `server/Makefile.in`: constrained
Primary/Full/High account token, explicit S4 user/owner SID and primary GID,
fresh administrator membership, fixed supported administrator privileges and
SYSTEM/account default DACL. Fresh H2a AuthenticationId and derived logon SID;
caller's session, no host identity change. No linked/getuid-admin/SYSTEM-SID-swap
factory. Re-resolve through `lookup_account` twice and compare incarnation,
generation and trusted snapshot. S4 supplies primary GID and mapped admin
membership, not arbitrary supplementary-group SIDs. Existing duplicate/child
propagation and standard-session linked-token/grant denials are unchanged.

Require elevation enforcement, protected system role, privileged sysrun
connection, SYSTEM, enabled SeTcbPrivilege, no impersonation, S6's actual broker
image/registration and unchanged retained primary/TokenId/ModifiedId. Negotiated
contexts are broker-process/server-connection scoped. Installed records retain
the original caller/primary, exact S1 request/reference and expiry; issuance
burns them before validation/lookup/allocation. Internal consumed-record handoff
requires a trusted authority/lifecycle validator. S18's real S8 redemption and
producer/validator are unwired. The only current producer is explicitly
synthetic and requires both WEX_E1S10_TOKEN_TEST and WEX_E1S6_TRANSPORT_TEST on
Linux. Normal builds deny all operations; no production capabilities or caller.

`wine/server/protocol.def`, `request_handlers.h`, `request_trace.h`,
`include/wine/server_protocol.h`: append `create_e1s_account_token` (number 324),
protocol **976 -> 977**, fixed 32-byte request/24-byte reply. Existing 88-byte
maximum and `tools/make_requests` limit remain sufficient and unchanged.
Canonical kind-4 input is 188 bytes plus 16-byte connection-local evidence ref;
kind-5 output is 136 bytes on success or 108 on failure (empty SID, zero result
fields). Test installation is exactly 268 bytes. Generated artifacts updated
by hand; regeneration remains unverified. `server/trace.c` adds byte-count-only
redaction selected by the protocol's `e1s_token` varargs type, preserved by the
generator rather than dumping account/nonces/references.

`wine/dlls/ntdll/sec.c`, `ntdll.spec`, `include/winternl.h`: private
`NtCreateWexAccountToken` export via the existing WoW64-aware server transport;
bounded byte buffers, output clearing and unexpected-handle cleanup. New
`include/wine/wex_token.h` and `include/Makefile.in` define/inventory byte sizes,
operations and LE helpers. Token handles are non-inheritable broker-only
TOKEN_ASSIGN_PRIMARY|TOKEN_DUPLICATE|TOKEN_QUERY; no handle goes to the caller.
`dlls/advapi32/tests/wex_token.c` and its `Makefile.in` add ordinary/unnegotiated
denial and failure-clearing tests. New project probe
`scripts/e1s10_token_probe.c` runs as the explicitly staged broker test image,
using real S6 endorsement and a retained standard peer for identity/session,
duplicate/fresh-logon and negative issuance controls.

No S5–S8 broker/wexhost/native transport or existing server process/sysrun/
named-pipe/account-provider edits were changed by S10. No accounts.c hook was
needed. Full wire/authority details, exact file inventory, Qwen WoW64 build and
lab handoff, probe matrix and unverified items:
[E1_S10_TOKEN.md](E1_S10_TOKEN.md). Static review, protocol table/layout checks
and Git whitespace checks only. No generator, build, WSL, VM or runtime tests;
no S9/S11 acceptance evidence, production enablement or commit.

## E1-S8 disabled native evidence and quotas (2026-11-06)

Zero additional stock Wine changes. Connection-owned, single-use S1 evidence,
service-wide bounded attempt/deadline/failure/concurrency accounting and generic
errors live in the new native S6 component `tools/e1s6/evidence.py`. The root-only
component client and `scripts/test_evidence.sh` remain isolated lab work. No
existing broker, server, wexhost, S4 or S7 source is edited; no production startup,
capability or installer enables the component. S18's required native calls and
endorsed-channel framing, test coverage and unverified items:
[E1_S8_EVIDENCE.md](E1_S8_EVIDENCE.md). Static review only; no build/runtime tests.

## E1-S7 disabled native PAM worker (2026-11-06)

Zero additional stock Wine changes. The worker compiles the existing S4
`wine/server/accounts.c` provider unchanged. New native worker/protocol,
supervisor and disposable PAM tests live under `tools/e1s7` and
`scripts/test_pam_worker.sh`; the only transport hook is in the existing native
`tools/e1s6/e1s6-lab.py` fixture. No broker/wexhost/server edits, PAM installation
or production enablement. Static review only; Qwen build/runtime handoff and
limits: [E1_S7_PAM_WORKER.md](E1_S7_PAM_WORKER.md).

## E1-S6 disabled endpoint/metadata transport, statically reviewed (2026-11-06)

Independent post-import review follow-up (2026-11-07; uncommitted/static only):

- Finding 1, real: `server/sysrun.c` independently requires control/data
  SOCK_SEQPACKET peers to have root UID, the same positive kernel PID and two
  live SO_PEERPIDFD pins before group raise; unsupported options deny. Wexhost
  checks both against its unique root bus owner and imports a third minimal,
  non-inheritable metadata handle at previously reserved sideband input offset
  16. The 96-byte sideband and native endorsement output size are unchanged;
  broker PID/session overwrite 16..23 on output. Rebuild both components together.
- Finding 2, real: `server/sysrun.c` stages server/broker generations and the
  grabbed primary/TokenId/ModifiedId locally; publish only after full send
  success and group drop. Failure preserves prior process state and releases
  the staged reference. This follow-up changes no other stock Wine file.
- Finding 3, not real: existing `named_pipe.c:1861-1868` closes all partially
  allocated result handles on error. No change to that file.
- Finding 4, real, project lab only: cap native server history at 4096; close
  dead pidfds on tick/insertion, retaining bounded replay tombstones. Check
  server and broker history capacity at insertion as well as Open; final cleanup
  skips closed pins.
- Finding 5, unpolled read confirmed, flakiness unproven, project lab only:
  poll control for at most one second within the existing absolute deadline;
  timeout/error denies. Normal broker data follows synchronous endorsement
  send success, so the report's scheduling-jitter explanation is not established.

Existing test defines and GPL-compatible notices remain. No production
enablement, protocol/generated layout or acceptance assertion changes. Static
review/whitespace checks only; Qwen must rerun the complete transport wrapper
as root in clean wexbuild (302 PASS expected), including both ABIs and normal
shipping controls. Evidence, cleanup reasoning and extra failure/stress probes:
[E1_S6_TRANSPORT.md](E1_S6_TRANSPORT.md#independent-post-import-review-fixes-2026-11-07-static-only).

`wine/server/sysrun.c`, `sysrun.h`: `sysrun_e1s6_endorse` is enabled only with
WEX_E1S6_TRANSPORT_TEST. First mapped executable device/inode must match the
machine-root broker (including the harness's exact test-broker name). Require
the existing protected system role, privileged sysrun connection, SYSTEM
service account, enabled SeTcbPrivilege and no thread token; S3 checks precede
the helper. Kernel socket creator UID/PID must match this process's native
identity. Server/broker CSPRNG generations are server-owned; re-registration
revokes the old broker generation. Renewals compare the retained primary object
and TokenId/ModifiedId. The server sends bounded metadata directly on a private
FD, with kernel-checked effective-wexsys SCM_CREDENTIALS, then drops its group
and closes local duplicate FDs. No credentials, authentication Boolean, token/
profile creation or launch operation is added.

`wine/server/process.h`, `process.c`: initialize first-image/role markers,
broker generation and retained registration primary-token reference; release
that reference on destruction. Existing process/token creation is unchanged.

`wine/server/named_pipe.c`: optional 96-byte lab sideband on the existing
get_pipe_caller_binding request. Require actual opener == consumed writer,
equal unchanged primary objects/IDs and no open/write impersonation, then
endorse. Shipping builds deny this sideband. Empty-input query, reply layout
and four non-inheritable handle rights are unchanged. No protocol.def,
generated protocol, ntdll, export or request-number changes; an old server's
binding reply cannot replace the absent native endorsement and fails closed.

Project wexhost adds a private system-D-Bus handshake, unique-owner/kernel
process pins, metadata-only FD channel and native child stub behind the same
define. Native/WoW64 slots append in matching order. Root verifier/bus policy
are temporary lab fixtures; no production startup enables them. Complete
protocol/file list, Qwen VM B procedure and outstanding verification:
[E1_S6_TRANSPORT.md](E1_S6_TRANSPORT.md). Static review only: no build, Wine,
WSL, VM, bus or kernel validation. Standard-user error 50 remains; no S1
readiness capability is advertised.

## E1-S4 protected account/SID provider implemented, statically reviewed (2026-11-06)

`wine/server/accounts.c`, `accounts.h`, `Makefile.in`: compile the private,
currently unused `lookup_account(name, uid, negotiated_caps, result)` provider.
No ordinary logon/SID/token path changes, protocol additions, capability
advertisement or standard-user elevation enablement. The API requires the S1
ACCOUNT_MAP_V1 bit, returns canonical local/NSS identity, incarnation, SID,
primary GID, fresh one-group administrator membership and map generation, and
zeros output on failure. Distinct private statuses cover unknown/tombstoned,
NSS mismatch/error, SID collision, unreadable/unprotected/invalid map, missing
administrator group, unsupported account, legacy conflict and absent capability.
The existing sysrun raise/drop helpers scope reading of root:wexsys data.

The root-only `tools/wex-accounts/wex-accounts.c` shares that parser/provider,
allocates random nonzero 128-bit incarnation IDs and persistent monotonic RIDs,
retains tombstones, serializes writers and publishes by fsync/atomic rename.
Authority is `/etc/wineexplorer/accounts.map` (0640 root:wexsys) beside protected
sysrun.conf: the group-writable machine root cannot protect an authority from
replacement. An explicit initial legacy user keeps the current local_user_sid;
no automatic migration or reuse of that SID occurs. Explicit retirement and
rename operations supply lifecycle assertions NSS cannot infer. Same-name/UID
deletion/recreation between syncs still requires retirement/absence notification;
this and policy/endpoint readiness continue to gate production advertisement.

`scripts/setup-machine-root.sh` installs/syncs at the end of enabled system-role
provisioning and preserves the protected administrator-group setting;
`installer/package-wine.sh` stages the native production tool and root systemd
path, timer and oneshot units. Provisioning enables them for default paths;
the ineffective unprivileged wexhost hook was removed. Automatic sync is
asynchronous and explicit root lifecycle coordination is still required.
Lookup checks only the selected NSS identity, with reentrant growing buffers;
the map retains its 1 MiB limit and sync has a bounded snapshot. First publication
uses a durable pending map for recovery, access ACLs are rejected, and stale
writer temporaries are removed under the lock. Test-only CLI/NSS/self-UID controls
are compiled only with WEX_ACCOUNTS_TEST, with no production environment hook.

Format, API/status numbers, lifecycle limits, provisioning inputs and Qwen
handoff: [E1_S4_ACCOUNTS_MAP.md](E1_S4_ACCOUNTS_MAP.md).
`scripts/test_accounts_map.sh` supplies the disposable root fixture and negative
cases. Only source/cleanup inspection, whitespace checks and Git Bash `bash -n`
on setup, packaging and the new fixture were performed here.
Unverified: all builds/linking and test execution, real NSS/group expansion,
server sysrun/group permissions, both client architectures and regression
controls, setup/package/update and AccountsService integration, writer races,
crash/durability/error recovery, actual ACL behavior, production hook exclusion
and complete lifecycle notification coverage. No guest or WSL execution.

## E1-S3 consumed-message writer binding implemented, statically reviewed (2026-11-06)

This extends E1-H2b below under `E1_S_STANDARD_USERS.md` section 7 row 3
and `E1_S1_CONTRACTS.md`. No build, generator run or runtime validation was
possible in this sandbox; none of the historical H2 results validate S3.

`wine/server/named_pipe.c`: `queue_message`, shared by ordinary writes and
FSCTL_PIPE_TRANSCEIVE, captures the submitting process, its primary-token
reference, TokenId/ModifiedId and the presence of an explicit thread token.
Capture is limited to client-to-server traffic whose client end has the H2
opener retention. It does not change ordinary pipe impersonation policy.
The server end retains that record before freeing the consumed message, in
both read fast/copy paths. Only its first accepted read, in message read mode,
consuming exactly one complete message, establishes valid provenance. Partial,
byte-stream/mixed, missing or superseded reads cannot establish it; a subsequent
accepted read invalidates it even if cancelled. Queuing another write does not
replace the consumed record. Disconnect/destruction clears consumed references
and buffered writer references; listener reuse resets read state. Cancelled,
discarded and destroyed messages release their references through `free_message`.

The private `get_pipe_caller_binding` gate and server-end FILE_READ_ATTRIBUTES
check are unchanged. It now requires valid consumed provenance and allocates
four checked, non-inheritable handles: opener and writer processes with the
existing query/synchronize rights, and their primary tokens with TOKEN_QUERY.
Every partial allocation is unwound; no handles are published before all four
succeed. The reply retains the original opener fields and appends writer
process/token handles, captured writer TokenId/ModifiedId, version 1 and writer
flags (VALID=1, IMPERSONATED=2, TOKEN_CURRENT=4). TOKEN_CURRENT compares the
retained writer primary object and IDs with its process at query time. The
opener current flag also checks TokenId, in addition to object and ModifiedId.
Flags report facts for broker policy; a mismatched writer does not globally
change pipe behavior.

`wine/server/protocol.def`, `wine/include/wine/server_protocol.h`,
`wine/server/request_handlers.h`, `wine/server/request_trace.h` and
`wine/tools/make_requests`: request remains 16 bytes; reply grows from 56 to
88 bytes (including the existing alignment gap at offset 20). New offsets are
56/60 for writer handles, 64/72 for writer LUIDs and 80/84 for version/flags.
The generic request/reply limit grows from 72 to 88 bytes (`pad[22]` and
`$max_req_size = 88`). SERVER_PROTOCOL_VERSION increases from 975 to 976.
Generated declarations, offset/size assertions, constants and trace fields
were edited by hand using the generator's layout rules; all protocol users
must be rebuilt together.

`wine/dlls/ntdll/sec.c`, `wine/dlls/ntdll/ntdll.spec` and
`wine/include/winternl.h`: add the private
`NtQueryWexPipeCallerBindingEx(HANDLE, WEX_PIPE_CALLER_BINDING *)` export.
The caller initializes Size to sizeof the native-layout output, then owns
all four handles on success. Version/required handles are checked; failed
requests or unsupported replies leave zero output for a correctly sized
buffer, and unsupported successful replies have their handles closed.
This structure is not the complete E1-S1 wire record. WoW64 uses the existing
PE-side server transport and explicit handle conversion. The old nine-argument
export retains its ABI, delegates to Ex and closes the extra writer handles;
it now shares the consumed-read requirement. The broker resolves only Ex,
so an old ntdll denies; protocol negotiation and the reply version also deny
old servers. The broker's external-SDK lab build mirrors the native structure
in `src/programs/wexelevate/main.c`.

`wine/server/token.c`: successful `token_assign_label` and default-DACL
replacement (including clearing) allocate a fresh ModifiedId. DACL allocation
precedes replacement, so allocation failure preserves the previous DACL/version.
Successful token object security-descriptor updates, filtering/standard-token
mutation helpers and authentication-ID copying also advance ModifiedId.
Existing privilege adjustment/disable-all tracking remains. Construction of
an unchanged duplicate preserves the source version after label copying, as
before; a different token object still has its own TokenId. The LUID allocator
now carries low-part overflow into the high part. No working session/owner/
primary-group setter is added; the public TokenSessionId stub remains outside
this package and cannot establish a different logon/session.

Broker policy now requires exact version/flags, no impersonation at open or
write, equal retained opener/writer process and primary-token objects, equal
captured TokenId/ModifiedId at open and write, current token flags, and matching
current primary statistics. Existing nonzero AuthenticationId, pipe/primary
logon/session equality, PID/object and liveness checks remain. Standard/full
callers remain unsupported. S5 still owns checked reversion and complete
precommit revalidation; S3 does not enable standard-user credential elevation
or claim the full S1 record, host/session generations or endpoint registration.

Lab-only `scripts/wexelevate_probe.c` adds bounded `provenance` modes for
self/transceive controls, post-open impersonation through write/transceive,
an inherited duplicate written by a different live process through both routes,
and default-DACL change-then-restore. `scripts/test_sysrun_server.sh` runs each
in both client architectures and checks mismatches never reach the consent
stub. These client-controlled sequences need no broker/server mismatch barrier;
no production hook was added. The probes retain their existing GPL headers.

Static checks: whitespace checks in both repositories, Git Bash `bash -n` on
the lab harness, all 648 protocol request/reply layouts and 1,242 field offsets,
size/limit consistency, native helper/broker layout agreement and cleanup/path
inspection. Unverified: compilation/linking (server, ntdll, broker, probes and
both architectures), exact generator reproduction, all new/existing lab tests,
actual transceive routing, prompt exclusion, token-version change/restore,
partial/oversized/mixed reads, queued second-message attribution, cancellation,
disconnect/reconnect/instance reuse, caller death, reference/handle leak counts,
each partial handle/allocation failure, wrong-object/old/short-reply injection,
installed binary/source identity and production-image hook exclusion. A trusted
fixture must still cover integrity-label change/restore and post-read token
changes; the ordinary limited caller cannot restore a raised label itself.

## E1-H2 caller binding: authentication id and opener binding implemented (2026-11-06)

Static review and the complete proposed contract are in `E1_H2_CALLER_BINDING.md`.
The authentication id (logon LUID) part was already implemented and committed:
`struct token` carries `auth_id`, `get_token_info` returns it, and
`TokenStatistics` reports the real `AuthenticationId` instead of zero.
That change increased `request_max_size` (`server/protocol.def`,
`include/wine/server_protocol.h`) and `$max_req_size` in `tools/make_requests`
from 64 to 72 bytes and bumped `SERVER_PROTOCOL_VERSION` from 973 to 974.
System tokens use the fixed id 0x3e7, the first interactive token gets a new id,
and the linked full token copies its source's id. The id is propagated through
trusted creation, duplication, filtering, inherited primary tokens and pipe
impersonation. The privileged create-token authentication-id input is honored.
Previously verified for that authentication-id change: full WoW64 build and
`advapi32 security` runs in both architectures, with 10 file/registry ACL
failures per run (lines 4005-4057, 6510, 6522, 6962; no baseline comparison).
Those results do not validate the opener-binding changes below.

E1-H2b now retains the opening `struct process` directly in the client
`pipe_end` at `create_pipe_client`, along with a reference to its primary
token, captured TokenId/ModifiedId and whether the opening thread had a token.
The server end queries this record through its connected client, rather than
looking up a saved PID. Disconnect (including either end's destruction) releases
and clears the process/primary-token references; failed setup uses the same
cleanup, and each replacement client has a fresh record. Cleanup tolerates a
missing pseudo-fd on allocation failure, and client-creation failure returns
NULL without dereferencing it. The existing effective-token pipe impersonation
behavior is preserved. `server/token.c` and `server/security.h` add only a
small token-id accessor; this slice does not change token mutation tracking.

The project-private `get_pipe_caller_binding` request takes a server-end
`pipe` handle with FILE_READ_ATTRIBUTES. It requires the same gate used to
select a usable primary linked token: elevation enforcement, LocalSystem token
role, `sysrun_process_privileged(current->process)` and enabled SeTcbPrivilege
on the process primary token. Otherwise it returns STATUS_ACCESS_DENIED.
Invalid/wrong-type handles, including client ends, return
STATUS_INVALID_PARAMETER_1; missing handle access returns STATUS_ACCESS_DENIED;
an unconnected server end returns STATUS_PIPE_DISCONNECTED. Normal checked
handle allocation returns non-inheritable SYNCHRONIZE |
PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_QUERY_INFORMATION access to the
retained process and TOKEN_QUERY only to the retained primary token. The reply
also includes the opener PID/start_time, captured TokenId/ModifiedId,
`impersonated` and `token_current`. The last flag means the retained token
is still the process's primary-token object and its ModifiedId still matches.
It does not assert liveness or absence of changes that do not update ModifiedId.
Partial handle allocation is unwound before returning an error; reply handles
are published only after both allocations succeed.

`NtQueryWexPipeCallerBinding` is a private ntdll export, implemented on the
PE side in `wine/dlls/ntdll/sec.c` using the existing `wine_server_call`
transport and its WoW64 marshalling. No new syscall/unixlib entry or edit to
`dlls/ntdll/unix/server.c` is needed. Its internal declaration is in
`include/winternl.h`; callers can resolve the export with GetProcAddress:

```c
NTSTATUS WINAPI NtQueryWexPipeCallerBinding(
    HANDLE pipe, HANDLE *process, HANDLE *token, ULONG *pid,
    LARGE_INTEGER *start_time, LUID *token_id, LUID *modified_id,
    BOOL *impersonated, BOOL *token_current);
```

All output pointers are required. On request failure the outputs are zero;
on success the caller must close both returned handles. The flags are reported
for the broker to enforce its policy; an impersonated open or a changed token
does not itself make this query fail. The broker must separately check caller
liveness and the rest of the E1 authorization contract.

Protocol artifacts `server/request_handlers.h`, `server/request_trace.h`
and `include/wine/server_protocol.h` were hand-updated to match
`tools/make_requests` output conventions because the generator could not be
run in this sandbox. `SERVER_PROTOCOL_VERSION` is now 975; the new request
is 16 bytes and its reply is 56 bytes including the header and alignment
padding. PID and start_time both fit; the existing 72-byte maximum is unchanged.
Client/server protocol users must be rebuilt together. No build, generator run
or runtime test was possible for E1-H2b; WoW64 build and focused lifecycle,
access-denial, partial-allocation and token-change fixtures remain unverified.

Historical H2b deferrals (per-message provenance and version tracking are now
implemented by the S3 entry above): per-message writer provenance, including transceive
and the identity of the exact consumed request message. This query establishes
only the opener binding and cannot detect another process or impersonating
thread writing through a transferred handle. It is not the complete section
(c) authorization facility. Server-generation/capability metadata, host-peer
UID validation and broker integration are also outside this slice. Still
needed: successful security-relevant token mutations must consistently update
ModifiedId (integrity-label/default-DACL paths currently do not), with
change-then-restore tests; provision one interactive identity per supported
trusted user/server-session lifetime and separate service identities, reject
older/stub capabilities, and implement trusted host-logon/session mapping
before claiming multi-session support. Existing process-information and
token-session queries need no new implementation.

## dcomp: rectangle-clip and 2D transform animation setters, transform objects (2026-11-05)

`wine/dlls/dcomp/device.c`: implement the remaining animation setters whose
property already exists in our state, and the five 2D transform object classes.

`IDCompositionRectangleClip`: `SetLeft/Top/Right/Bottom` and the eight
`Set*RadiusX/Y` animation setters (previously `E_NOTIMPL`) now accept an
animation when the matching property exists, mirroring the offset-animation
contract: `E_INVALIDARG` for NULL, a not-yet-ended animation, or an animation
from another device; `E_NOTIMPL` for a foreign vtable. The plain float setters
validate finiteness only (`E_INVALIDARG` for NaN/inf), matching Windows, which
accepts out-of-range values. `Commit` samples each animated property once at
the commit reference time through the shared animation sampler.

`IDCompositionTranslateTransform`, `IDCompositionScaleTransform`,
`IDCompositionRotateTransform`, `IDCompositionSkewTransform` and
`IDCompositionMatrixTransform`: new refcounted transform objects. Each stores
its animated properties and exposes the plain float setters plus the matching
`Set*Animation` setters with the same validation rules as the offset
animations. `SetMatrix` validates the six elements for finiteness
(`E_INVALIDARG` for NaN/inf); `SetMatrixElement` validates the row/column
bounds (`E_INVALIDARG` outside row 0..2, column 0..1) and the value for
finiteness. `Create*Transform` on the device, device2, device3 and
desktop-device interfaces now creates the object (`E_INVALIDARG` for a NULL
output pointer). `CreateTransformGroup` and the 3D transform factories remain
`E_NOTIMPL`.

`IDCompositionVisual::SetTransformObject`: stores the transform object pointer
without taking a reference (Windows does not add a reference when a transform
is attached to a visual), clears the plain `SetTransform` matrix (the two are
mutually exclusive), and validates `E_INVALIDARG` for a transform from another
device and `E_NOTIMPL` for a foreign vtable. `Commit` builds the visual's
matrix from the transform object's current (sampled) values using the standard
D2D row-vector affine formulas (translate, scale/rotate/skew about the center,
matrix as-is). The retained-scene renderer uses the resulting matrix unchanged.

`IDCompositionEffectGroup::SetOpacity`: validate finiteness only
(`E_INVALIDARG` for NaN/inf); Windows accepts values outside [0,1].

`wine/dlls/dcomp/tests/dcomp.c`: add `test_clip_animation`,
`test_effect_group_opacity`, `test_transforms` and
`test_visual_SetTransformObject` in the existing style. `dcomp` runs 1112
tests, 0 todo, 0 failures on x86_64 and i386 (up from 1002).

Native behaviour was pinned with MSVC probes on Windows 11 (x86_64 and i386,
identical results): all probed HRESULTs match the implementation, including
`SetOpacity(-1.0)` -> `S_OK`, `SetMatrixElement` out-of-bounds ->
`E_INVALIDARG`, and the no-reference semantics of `SetTransformObject`. The
full test exe cannot run to completion natively because the pre-existing
`test_surface_interface` calls a Wine-only `IDCompositionSurfaceUnknown`
vtable slot that does not exist in the native interface; native verification
is therefore via the probes.

## combase: LocalService name buffer and ServiceParameters (2026-11-05)

`wine/dlls/combase/rpc.c` `create_local_service` read the AppID `LocalService` value into a GUID-sized buffer (39
characters), so service names longer than 38 characters (the Chrome updater's `GoogleUpdaterInternalService157.0.8089.0`)
failed with `ERROR_MORE_DATA` and activation fell through to a `dllhost.exe` surrogate; the buffer now holds a full
service name (256 characters). It also reads the documented `ServiceParameters` value first and keeps `ServiceParams` as
a fallback. Verified in the QEMU guest with the Chrome MSI trace (the service is now started with `--com-service`;
the install still stops later, see `CHROME_INSTALLER_ANALYSIS.md` section 8) and with the ole32 `compobj` test
(1229 tests, 0 failures, both architectures). An earlier opposite-WOW64-view retry was a misdiagnosis and was removed.

Known deviation: the surrogate fallback remains unchanged. Wine still may launch the default `dllhost.exe` when
`DllSurrogate` is absent; the proposed surrogate-semantics fix was deliberately not applied because its Windows
behavior is unverified and the change is risky.

## P13 flat8 opaque accent frame (2026-11-05)

Wine files touched: `wine/include/wine/dwm_protocol.h`, `wine/dlls/dwmapi/dwmapi_main.c`,
`wine/dlls/dwmapi/tests/dwm_attr.c`, `wine/dlls/uxtheme/window.c`, `wine/dlls/win32u/defwnd.c`,
and `wine/dlls/winex11.drv/window.c`.
The bridge/theme integration is in `src/dlls/wexshell/dwm_bridge.c` and `src/dlls/themeui/theme_engine.c`.
Static-only implementation of the default-off `DwmFrameStyle=2` / `FrameStyle=flat8` gate; no build, runtime,
fresh-prefix, X11/Wayland screenshot, or presented-pixel result is claimed.

## dxgi/dwmapi: frame statistics and shared colorization semantics (2026-11-04)

Wine changes: `wine/dlls/dxgi/swapchain.c` implements D3D11 `GetFrameStatistics` from the existing successful-Present
counter and a QPC/display-refresh estimate, returning `DXGI_ERROR_FRAME_STATISTICS_DISJOINT` before the first Present
and for windowed non-flip swapchains; `wine/dlls/dxgi/tests/dxgi.c` adds the focused assertions, based on Wine-Staging
patch `dxgi_getFrameStatistics`. `wine/dlls/dwmapi/dwmapi_main.c` makes `DwmGetColorizationColor` and
`DwmGetColorizationParameters` read the same ARGB registry color and opaque flag, with theme-provided
`[VisualStyles] ColorizationColor` continuing to flow through that registry source. `wine/dlls/dwmapi/tests/dwmapi.c`
extends argument and getter-consistency validation. No Open-Shell code was copied.

## dcomp: IDCompositionAnimation objects and animated offset/opacity properties (2026-11-04)

`wine/dlls/dcomp/device.c`: implement `IDCompositionAnimation` as a refcounted
object holding a validated segment list (cubic polynomial
`c0+c1*t+c2*t^2+c3*t^3`, sinusoidal, repeat and end segments) with the Windows
validation rules: `E_INVALIDARG` for NULL, not-yet-ended, out-of-order
(`beginOffset` earlier than the previous segment's end), negative repeat
duration, repeat before any segment, and NaN/inf arguments; `E_NOTIMPL` for a
foreign vtable; `E_INVALIDARG` for an animation from another device. `End`
before any segment succeeds, matching the staging contract.
`CreateAnimation` on the device, device2, device3 and desktop-device
interfaces now creates the object. `IDCompositionVisual::SetOffsetXAnimation`
and `SetOffsetYAnimation` and `IDCompositionEffectGroup::SetOpacityAnimation`
accept an animation when the matching property exists in our pending/active
state: the setter validates the object, takes a reference and stores it in the
pending state; `Commit` copies it to the active state and samples it once at
the commit's reference time (system time in 100ns units, so elapsed seconds
are measured from the `SetAbsoluteBeginTime` `LARGE_INTEGER`) to fill the
plain float property,
so the existing retained-scene renderer uses it unchanged. There is no
per-frame animation engine: the value is sampled at `Commit` only. Setting the
plain float afterwards replaces the animation, as Windows does. All other
animation setters (transform, rectangle clip, etc.) keep `E_NOTIMPL` where our
state has no such property.

`wine/dlls/dcomp/tests/dcomp.c`: port `test_visual_SetOffsetXAnimation`
(staging patch 0044, `todo_wine` removed since it now passes) and
`test_animation` (Add*/End error codes) with attribution to Zhiyi Zhang.
`dcomp` runs 991 tests, 3 todo, 0 failures on x86_64 and i386.

## dcomp: device and visual conformance contracts (2026-11-04)

`wine/dlls/dcomp/device.c` matches the selected Wine-Staging conformance contracts for device
creation and QI/version gating, NULL-output validation, desktop-HWND target rejection, visual
creation, swapchain/surface content acceptance and reference retention, target-root ownership and
reparent rejection, visual-child ordering/reparent rejection, and enum validation for bitmap
interpolation, border mode and back-face visibility. `SetOffsetX` stores the pending value and
returns `S_OK`. These are HRESULT, QI, ownership and setter-contract changes only; the retained
scene and transaction design is unchanged. The corresponding test provenance is recorded beside
each ported function in `wine/dlls/dcomp/tests/dcomp.c` (staging patches 0002, 0004, 0007, 0009,
0014, 0039, 0045 and 0050). Static inspection only; no runtime result is claimed here.

`wine/dlls/winewayland.drv/wayland_surface.c`: publish `window.scale`, rounded
to thousandths, in the `__wine_wayland_scale` HWND property on surface creation,
role scale initialization and fractional-scale changes; remove it on surface
destruction. Preserve the existing `__wine_wayland_scale_120` property and its
notifications for current shell consumers. The DWM shell bridge uses the new
property to convert thumbnail and effect geometry from Wine pixels to surface
units. No protocol change. Static validation only; user builds and Wayland
runtime checks remain. See `DWM_P5_WAYLAND.md`.

## G4 unbound composition frame retention (2026-10-21)

Wine `4315be8`, `dlls/dxgi/swapchain.c`, `dlls/wined3d/swapchain.c` and
`include/wine/wined3d.h`: capture-only internal Present retains a composition
producer frame submitted before DComp target binding, without drawing it.
Normal command-stream ordering/latency/rotation and retained ownership remain;
later Commit cannot accidentally substitute an unpresented backbuffer. GL/Vulkan
only, same bounded formats/sizes. All29 correctness groups both PEs/backends,
G0-G3, pacing and two real frozen Electron lifecycle runs per backend pass.
Actual settled frames inspected; early resize transients remain negative and
no installed-shell/native-acceleration or G4 merge-ready claim is made. See
CHROMIUM_GRAPHICS_G4_FROZEN_RESIZE_FIXED_20261021.md. Compositor unchanged.

## G4 native empty parent clip (2026-10-21)

Compositor `94faa651`, `src/protocols/wex-window.c`: explicitly disable a render
subtree when its intersection with the parent is empty; wlroots interprets an
empty clip as no clipping. Re-enable on nonempty intersection. Independent
outside-child and parent-shrink leaks reproduced before repair;24 exact native
pixel/geometry/lifetime controls pass after it. WoW64 own/foreign pixels and
real Electron GL/Vulkan live125%→150% lifecycle pass scoped controls. Wine571e907
unchanged. See CHROMIUM_GRAPHICS_G4_NATIVE_GEOMETRY_20261021.md for remaining
installed-theme/native-accelerated/frame-timing gates; no master merge.

## G4 native Chromium cross-process rendering (2026-10-21)

Wine `571e907`, `dlls/winewayland.drv/{wayland.c,wayland_surface.c,wayland_wex.c,
waylanddrv.h,wex-window-v1.xml}` and `dlls/wined3d/swapchain.c`: private v11
cross-connection rendering role for a producer child beneath a foreign UI root,
published parent identity/logical scale, cached attachment geometry and narrow
opaque-cover background-capture elision. Partial/translucent backgrounds are
not substituted. Matching compositor `1b9df94e` on its dedicated
`codex/g4-native-render-bridge` branch adds clipped input-passive render roles
and parent/producer revocation. Source is original, no proprietary payloads.
Both PEs/GL/Vulkan exact own/foreign full-target pixels;16 native lifecycle
controls,28 regression groups both backends/both PEs, G0–G3, pacing and real
Electron/WebView2 controls pass scoped checks. Fractional scale, attachment-role
migration, native accelerated/installed-shell and transient resize-border
checks remain; generic foreign GDI/partial underlay remain unsupported.
Evidence: CHROMIUM_GRAPHICS_G4_NATIVE_BRIDGE_20261021.md. No master merge.

## G4 X11 startup caption clipping (2026-10-21)

Wine `54bc44a`, `dlls/winex11.drv/window.c`: apply legacy window-region origin
adjustment only without client-side graphics. Surface shapes are already in
drawable coordinates; shifting them again clipped real Chromium startup frames.
Both PEs pass default/open Luna and explicit unmanaged Classic caption checks;
all28 correctness groups pass GL/lavapipe. Evidence and remaining desktop gates:
CHROMIUM_GRAPHICS_G4_CAPTION_FIXED_20261021.md.

## G4 R2 event and WoW64 Vulkan allocation ownership (2026-10-21)

Wine `2c1e3ef`, `dlls/d3d11/{device.c,d3d11_private.h}`: pending fence waiters
retain event objects through duplicated handles, close them on all notification/
teardown/rollback paths, and use distinct CS command identity for cancellation.
Native Windows x64/x86 comparison reproduces the lost-event bug in old Wine.
Wine `1dcd479`, `dlls/win32u/vulkan.c`: retain/release Wine-synthesized low-address
host imports on normal/failure paths, without freeing caller-owned host pointers.
Repairs an observed64MiB/device WoW64 Vulkan leak and full-suite address-space
exhaustion. Evidence, limits and open gates: CHROMIUM_GRAPHICS_G4_R2_LIFETIME_20261021.md.

## G4 inherited border modes and coverage (2026-10-21)

Wine `61929ac`, `dlls/{dcomp/device.c,dxgi/swapchain.c,wined3d/swapchain.c}` and
`include/wine/{dcomp_layer.h,wined3d.h}`: validate/retain/inherit border mode,
transactional publication, independent soft-edge reference coverage with hard
default and preserved bitmap interpolation. Scoped evidence and limitations:
CHROMIUM_GRAPHICS_G4_BORDER_VERIFIED_20261021.md. Not exact native-AA certification.

## G4 dirty-flush repaint notifications (scoped verification completed)

Wine `bc7011f`, dlls/win32u/dce.c, dlls/dxgi/{factory.c,swapchain.c,
dxgi_private.h}, include/wine/dcomp_underlay.h: experimental asynchronous
backing-flush notification and retained redraw, with guarded notification-window
lookup/Release and bounded message coalescing. No production timer/poller.
Historical repeated-paint/GetMessage failures are preserved in the resume note;
the timeout was a zero HWND timer-ID/success-value mismatch in the probe. An
explicit identifier and separate watchdog repair the test without changing Wine
or relaxing assertions. All 22 groups now pass both PEs/GL/lavapipe, including
pending-state preservation, real WM_PAINT/observed obscure-expose, idle unchanged
path counters and queued teardown. G0-G3, pacing and real consumer controls pass
scoped checks. No merge: full R1-R4 remain incomplete. Current identities and next
steps: CHROMIUM_GRAPHICS_G4_REPAINT_VERIFIED_20261020.md.

## G4 ordinary GDI underlay retention (2026-10-20, second continuation)

Wine `48000ca`, `dlls/{dcomp/device.c,dxgi/swapchain.c,wined3d/swapchain.c,
wined3d/wined3d_private.h,win32u/dce.c,win32u/window.c,winex11.drv/bitblt.c}` and
private `include/wine/dcomp_underlay.h`: retain the ordinary HWND GDI background
below composition content. Cache a reference-counted DIB with prepared/active
target surfaces, sample the separate GDI backing (never the previous screen
output), and restore it when the last visual detaches without consuming an
already prepared scene or rotating an unpublished producer buffer. Explicitly
deliver that restoration to the application HWND, not the hidden producer.

X11's direct-drawing optimization formerly discarded the GDI backing when a GL
client drawable existed, causing resize feedback. A private HWND-lifetime policy
keeps its normal backing alive and win32u keeps GDI DCs on it; live registrations
remain separately owned/released per top/bottom target layer. Duplicate live
layers return WINDOW_ALREADY_COMPOSED; foreign-process HWNDs are denied. These
properties are coordination, not a security mechanism. No polling is added.

Cost: cached target DIB plus ordinary HWND backing, capture/copy and underlay
upload where needed. A fully covering eligible opaque GL leaf skips the underlay
work; the four old producer counters do not count GDI traffic. Not zero-copy or
a power/performance result. All 21 groups pass both PEs on software GL/lavapipe;
G0-G3, pacing, real WebGL1/2, Electron lifecycle and inspected frozen front-window
controls pass scoped checks. Automatic expose, general DirectX/layered/child
ordering, soft AA, R2/R3 and actual Classic/Luna/Wayland/physical hardware remain
open. See CHROMIUM_GRAPHICS_G4_BACKGROUND_20261020.md for negative controls and
failed intermediate approaches; no master merge approval.

## G4 unrestricted dirty-list assembly (2026-10-20, resumed)

Wine `273cacd`, `dlls/dxgi/swapchain.c`: supersedes the initial 64-rectangle
complement-slicing implementation below. Assemble pending history in order:
copy the coherent previous frame, apply clipped scroll, overlay every dirty
rectangle from the producer, then copy the completed image to the producer's
presentation buffer. Dirty writes therefore win without interval sorting,
quadratic complement fragmentation, fixed arrays or an arbitrary count cap.
All metadata is validated before enqueueing blits. No caller pointer survives
the call. Full Presents still require only the history snapshot copy.

Partial-frame cost is two full-frame GPU copies plus the scroll and dirty
regions; two history allocations remain. This is not zero-copy or a measured
performance win. The real 65-rectangle negative control fails on both PEs
before this change; all 19 required correctness groups then pass on software
GL/lavapipe for both PEs. Real WebView2 WebGL1/2 pixels and Electron input/window
lifecycle remain passing. Baseline startup flakiness and inherited upstream
failures remain recorded in the continuation report; no merge approval.

## G4 transactional bitmap interpolation (2026-10-20, resumed)

`dlls/dcomp/device.c`, `dlls/dxgi/swapchain.c`, `dlls/wined3d/swapchain.c`,
`include/wine/{dcomp_layer.h,wined3d.h,winedxgi.idl}`: validate NEAREST/LINEAR/
INHERIT, retain pending/active interpolation and resolve inheritance at Commit.
Keep the documented root default nearest-neighbor. Reference composition now
supports texel-center bilinear interpolation with clamped taps and premultiplied
channels; integer-aligned unit-scale opaque GL scenes retain their existing
GPU route because both filters sample identical texel centers there. Scaling,
fractional transforms and alpha still use the labeled CPU reference fallback.

The private scene layout changes, so its builtin binding IID is now
`bc193c8b-df4f-4c75-8a59-0ab8ffccdc0b`; matched builtin DLLs required. No DXVK
interop, linear-light blending, soft-border antialiasing or acceleration claim.
Both PEs fail the new exact quarter-texel pixel test before the fix; afterward
all 18 correctness groups pass on software GL/lavapipe, including inheritance,
override/reset, pending edits, retained property-only Commit, both-axis sampling,
edge clamping and premultiplied alpha. Real Electron lifecycle remains passing.

## G4 coherent Present1 damage preservation (2026-10-20, resumed)

`dlls/dxgi/{swapchain.c,dxgi_private.h}`: retain two private GPU history
textures for single-sampled sequential / flip-sequential D3D11 swapchains.
Reconstruct pixels outside the caller's dirty rectangles from the last
successfully presented coherent frame, then apply scroll from that immutable
history, excluding dirty pixels. Record full Presents too. Serialize Present
and ResizeBuffers per swapchain; successful resize invalidates history and
final release frees it before the owning device/swapchain. Invalid metadata,
TEST, minimized/unbound presentation and failed Presents do not advance history.
Scroll is flip-sequential only, destination-bounded, clipped using 64-bit math.

This uses existing ordered GPU blits, not CPU image readback or a new per-frame
completion wait. Cost is two additional full-size GPU images and a snapshot
copy per eligible Present, plus reconstruction blits on partial updates; it is
not a zero-copy or power/performance claim. More than 64 dirty rectangles is
explicitly E_NOTIMPL, not silently ignored. Unsupported multisampled / discard
dirty presentation is rejected. D3D12/native DXVK are not changed.

The original two-architecture negative control presents BLUE, RED, then only a
GREEN dirty patch: pixels outside it incorrectly return to BLUE. The repair
preserves RED and survives retained visual movement, subsequent damage,
overlapping dirty rectangles, scroll overlap/priority, invalid metadata, TEST
and extreme offsets. Real WebView2 and Electron front-window controls no longer
show the observed stale trails/background patches. These are isolated Xvfb
software-driver controls, not full G4 or shell/theme acceptance.

## G4 available multisample render targets (2026-10-20, resumed)

`dlls/d3d11/device.c`: report MULTISAMPLE_RENDERTARGET when a renderable format
has an actually supported sample count, independently of the previous combined
4x-and-8x resolve/load check. Keep exact per-count quality queries unchanged and
do not expand resolve/load flags just because a render target exists. Add a
TRACE of returned format flags for reproducible capability diagnosis.

The owned Mesa software drivers support 4x RGBA/BGRA but not 2x/8x. Previously
the bit was absent; ANGLE's checked multisample table became empty and Skia
rejected ordinary framebuffer formats. Both architectures' negative controls
fail before the fix; all 16 custom groups pass afterward on GL and lavapipe,
including real 4x RTV clear/resolve/staging pixels and unsupported-count checks.
The unchanged WebView2 host now progresses without its prior GPU/Skia fatal
errors, and an opt-in WebGL1/2 diagnostic reads correct red default/FBO pixels.
Actual front-window output still shows moving-content trails; no G4 acceptance,
performance, physical GPU or feature-level-conformance claim is made.

## G4 honest newer video-interface exposure (2026-10-20)

`dlls/d3d11/device.c`: do not advertise ID3D11VideoDevice1 while the matching
VideoContext1 is absent and VideoDevice1-only entry points are unimplemented.
The base ID3D11VideoDevice remains available. This repairs a real Chromium
startup invariant, not video decoding/HDR support. A before/after regression
fails on both architectures before the change and passes afterward; all 15
custom correctness groups pass on both software GL/Vulkan architectures.
The default WebView2 run gets past its initial video-interface CHECK and
reaches a different Skia backend-format fatal error. G4 is still incomplete;
no consumer acceptance or security/sandbox enforcement is inferred.

## G4 opaque retained OpenGL delivery (2026-10-20)

`dlls/wined3d/{swapchain.c,wined3d_private.h,wined3d.spec}`,
`include/wine/wined3d.h`, `dlls/dxgi/swapchain.c`: retain the last presented
opaque BGRA/RGBA image in an independently owned GPU texture. Ordinary frames
copy in the ordered command stream; only allocation/resize drains it. Eligible
non-layered HWND scenes (unit opacity, no clips, integer translation, unit scale)
blit retained producers back-to-front into the real window drawable. Capability
gates require an FBO-attachable source and glBlitFramebuffer. No Vulkan dependency
or new per-frame CPU readback/completion loop is introduced for this route.

Other scenes still use the explicit CPU reference renderer. Entering that path
lazily reads the last PRESENTED GPU snapshot, never an unpresented producer
buffer; returning from a newly presented CPU frame preserves it until the next
GPU capture. A private diagnostic GetPrivateData GUID exposes capture/readback/
composition counts, not public frame/scanout statistics. Both architectures pass
14 correctness groups on llvmpipe GL and lavapipe fallback, G0-G3 regressions and
five pacing controls. This is not zero-copy, accelerated cross-process sharing,
physical GPU proof, full alpha/clip GPU rendering or browser certification.

## G4 multi-target preparation and safe reparenting (2026-10-20)

`dlls/dcomp/device.c`: prepare independent target snapshots before changing any
active visual; deduplicate producers within a target and reject unsupported
cross-target producer routing, duplicate-HWND targets and repeated rooted visual
identity before publication. Scenes may use distinct D3D devices, but a single
scene still cannot mix devices. Bounds remain 128 visuals / 32 content entries
and at most 32 rooted targets per device. Old active nodes are retained while
ALL old child edges are removed, then the new graph is built. This prevents a
reparented child's new sibling links from corrupting its old parent's traversal.
Eleven groups PASS for both software-GL architectures: multi-target pending vs
active pixels, second-target allocation failure rollback, unsupported routing
failure isolation, target detach, reparented pixels and final COM edge release.
This does not claim simultaneous physical scanout or recovery from a backend/
window/device failure during final publication.

## G4 device lifetime and pending-state synchronization (2026-10-20)

`dlls/dcomp/device.c`: visuals, targets, clips and effect groups retain their
creating device. Target/visual teardown and pending setters share the device's
recursive critical section with Commit; live clip/opacity setters use that same
lock. Cross-device clips/effects reject before changing pending state. NULL
transform-object assignment now resets an earlier matrix; non-NULL unsupported
transform objects/parents no longer return a successful no-op. Nine regression
groups pass on both software-GL PE architectures, including concurrent setters,
Commit and Present, followed by releasing the caller's device before children.
This bounded stress test is not exhaustive fault-injection/lifetime proof.

## G4 hierarchical reference composition (2026-10-20)

`dlls/dcomp/device.c`, `dlls/dxgi/swapchain.c`, `dlls/wined3d/swapchain.c`,
and `include/wine/{dcomp_layer.h,wined3d.h,winedxgi.idl}` now retain contentless
visuals as bounded preorder scene nodes (128 visuals, still 32 content entries).
Each node carries its own transformed clip and opacity mode. The CPU reference
renderer isolates LAYER subtrees per pixel and propagates MULTIPLY opacity,
instead of flattening both into leaf opacity. Ancestor clips mask descendants
in the ancestor's coordinates. Scene intervals are validated before publication;
the builtin private binding IID is versioned for the changed scene layout.
New rectangle clips start unbounded, as required by the public API contract.

Both architectures pass eight target-pixel/observer groups on software OpenGL,
including rotated nested clips, all four independent rounded corners, live
clip/effect mutation at Commit, and LAYER-versus-MULTIPLY overlap colors.
This extends the earlier reference slice; it does not establish efficient GPU
delivery, arbitrary HWND background preservation, multi-target routing, real
consumers, hardware or desktop/theme acceptance. G4 remains in progress.

## G4 R1 retained-reference repair (2026-10-20)

`dlls/dcomp/device.c`, `dlls/dxgi/{factory.c,swapchain.c,dxgi_private.h}`,
`dlls/wined3d/{swapchain.c,wined3d_private.h,wined3d.spec}`, and
`include/wine/{dcomp_layer.h,wined3d.h,winedxgi.idl}`: preserve Gemini's unfinished
retained renderer, then repair frame ownership and real delivery. Each producer
retains a reference-counted copy of its last PRESENTED frame, not another
producer's mutable rotating back buffer or an unowned swapchain pointer. Scene
preparation allocates before active-state application; shared content is bound
once. Commit repaints retained frames without consuming a producer back buffer.
Target release revokes bindings; resize reallocates bounded reference surfaces.

Composition preserves BGRA/RGBA and premultiplied-versus-ignored alpha metadata.
CPU reference blending feeds Wine's real GL back drawable or Vulkan presentation
image; scratch textures never impersonate a window drawable. Vulkan recreates a
resized target before blitting instead of scaling into its stale extent. Leaf
fractional/rounded masks and bounded affine transforms are retained at Commit.
Preflight rejects ancestor clips, isolated non-unity LAYER groups, cross-device
scenes and multiple rooted targets rather than accepting incorrect rendering.
The extended private COM binding has a new IID; matched builtin DLLs are required.

This is CPU readback/reference composition, NOT zero-copy/GPU composition,
arbitrary application-background transparency, full R1/G4 acceptance, native DXVK
interop or Chromium certification. Nearest/binary sampling and remaining gates
are documented in CHROMIUM_GRAPHICS_G4_R1_REPAIR_REVIEW.md. Classic/Open Luna and
real X11/Wayland/hardware regressions are not inferred from unchanged theme code.

## G4 independent correctness boundary (2026-10-19)

Wine f3ecadd3 on codex/g4-correctness repairs shared fence notifications in
d3d11/device.c and d3d11_private.h; direct builtin composition binding in
wine/winedxgi.idl, dxgi/{swapchain.c,factory.c,dxgi_private.h} and dcomp/device.c;
and composition-only opaque drawable clears in wined3d/swapchain.c and
wine/wined3d.h. Unsupported properties/multiple content leaves reject honestly.
This is not a complete retained compositor, browser, hardware or desktop pass.
See CHROMIUM_GRAPHICS_G4_REPAIR_REVIEW.md and the G4 completion work order.

## G3 independent correctness repair (2026-10-18)

On `codex/g3-correctness`, D3D11 CopyResource checks resource dimensions before
the inherited shared-Texture2D hook; ordinary buffer/1D/3D copies retain the
normal WineD3D path.
DXGI initialization likewise queries only the matching resource type while its
outer D3D object is under construction, avoiding NULL-inner forwarding crashes.
Fence Wait/Signal use a new ordered WineD3D CS packet, with an event-based CS
wait and backend completion before publishing a signal.
OpenGL finishes all device contexts; Vulkan submits and waits its graphics
queue; no-3D has no GPU work. Public/private fence refs retain queued objects
without destroying the device on its own CS thread. This conservative barrier
is not a zero-copy or performance claim. No-CS-thread mode returns E_NOTIMPL
rather than pretending to implement nonblocking queued waits. See
`CHROMIUM_GRAPHICS_G3_REPAIR_REVIEW.md` for verification and remaining gates.

## G4 DirectComposition, Retained Visual Tree, and Pacing (2026-10-18)

On `gemini/chromium-graphics-g4`:
- `dlls/dcomp/device.c`: Implemented retained visual tree operations (`AddVisual`, `RemoveVisual`, `RemoveAllVisuals`, reparenting with cycle rejection and pre-detach reference visual validation), visual properties (`SetOffsetX`, `SetOffsetY`, `SetTransform` with affine 2D scale/translation, `SetClip`, `SetContent`, `SetEffect`, `SetOpacityMode`), `IDCompositionEffectGroup` (`SetOpacity`), and `IDCompositionRectangleClip` (radii and bounds). Evaluates clip and effect objects dynamically on `Commit()`, unbinds detached visuals from their target HWNDs, and utilizes cross-thread safe parameter posting (`WINE_DCOMP_SET_TARGET_PARAMS_OWNED`).
- `include/wine/dcomp_layer.h` & `dlls/dxgi/factory.c`: Defined explicit `WM_WINE_DCOMP_SET_TARGET` modes (`LEGACY`, `PARAMS`, `PARAMS_OWNED`). `dcomp_layer_compute_placement` proportionally calculates source and destination rectangles for clipped composition visuals, preventing squashing while tracking `fully_clipped`. Frees thread-posted parameter blocks safely.
- `dlls/dxgi/swapchain.c` & `dlls/dxgi/dxgi_private.h`: Synchronized composition placement parameters (`target_hwnd`, `dst_rect`, `src_rect`, `fully_clipped`) under `wined3d_mutex` and passed to `wined3d_swapchain_present`. Removed artificial backbuffer index rotation so `IDXGISwapChain3::GetCurrentBackBufferIndex()` consistently returns 0 matching the actual presented buffer in WineD3D flip model. Mapped `WINED3DERR_INVALIDCALL` to `DXGI_ERROR_INVALID_CALL` in `SetMaximumFrameLatency` and `GetMaximumFrameLatency`. Implemented window occlusion detection checking `swapchain->target_hwnd` for minimized state (`IsIconic`), returning `DXGI_STATUS_OCCLUDED` for proper application frame pacing. Added `IDXGISwapChain3::ResizeBuffers1` forwarding.
- Verified on Debian WoW64 under isolated Xvfb + llvmpipe: 11/11 G3 entry gate tests PASS, 6/6 G4A composition probe tests PASS, 5/5 G4B presentation and pacing tests PASS on both `x86_64` and `i686`. See `docs/CHROMIUM_GRAPHICS_G4_ACCEPTANCE.md`.

## Independent token-security correctness repair (2026-10-18)

`codex/token-security-correctness` retains deny-only groups in the deny-ACE
matching path even when ENABLED is clear, while excluding them from allow ACEs.
Shell elevation preserves helper cancellation versus actual startup/unsupported/
timeout errors and never returns a target handle on helper rejection. Duplicate
fork `programs/consent` sources were removed (recoverable in Git); the owned
`src/programs/consent` module is the single assembly source. It has no shipping
fixture/direct-password-helper bypass and fails unsupported credential requests
closed. This is functional repair, not complete host authorization/containment.
See TOKEN_SECURITY_REPAIR_REVIEW.md and the checkpoint-B work order.

## Checkpoint B: elevation helper audit and token restriction (2026-10-22, corrected)

- `server/token.c`:
  - Added an explicit `is_restricted` flag in `struct token`.
  - Replaced the synthetic Null SID (`S-1-0-0`) inserted for a disjoint restriction with an empty restricting
    set that stays restricted, so every access check is denied.
  - `filter_token` fails with `STATUS_NO_MEMORY` when allocating a restricting SID fails, without publishing
    a partial token.
- `dlls/shell32/shlexec.c`:
  - Three-state helper status: unset (key or value absent, or an empty string), configured, invalid (wrong
    type, odd or oversized data, whitespace, control characters, read error). Invalid fails closed with the
    exact error and no process handle.
  - With the helper unset, `ERROR_ELEVATION_REQUIRED` from `CreateProcess` returns `ERROR_BAD_FORMAT` as before.
- `dlls/kernelbase/process.c`: `is_elevation_required` reads the helper first. Unset returns before the
  manifest is parsed; anything else falls through to the manifest check.
- `dlls/ntdll/loader.c`: `is_elevation_helper_unset` replaces the boolean check; only an unset helper lets the
  loader elevate silently.
- `programs/conhost/window.c`: `update_window` skips the maximum window size computation when the console
  font cell size is zero (no usable console font), which raised an unhandled division by zero.
- `programs/explorer/systray.c`: no change. The checkpoint B edits were reverted.
- `dlls/advapi32/tests/security.c`: `test_restricted_sid_intersection` expects 0 restricting SIDs for a
  disjoint intersection.


## Elevation boundary, plan steps 2 and 3 (2026-10-22)

Everything below applies only while `HKLM\Software\Wine\Elevation\Helper` (64-bit view) is configured, using
the same unset rule as the ntdll loader. With the helper unset nothing changes. Native values come from
`TOKEN_SECURITY_REFERENCES.md`.

- `server/registry.c`: `elevation_helper_unset()` reads the helper from the in-memory registry;
  `object_is_registry_key()`.
- `server/security.h`: `elevation_enforced()` and the declarations below.
- `server/token.c`:
  - tokens carry a `lineage_id` shared by a token and its duplicates and filtered copies;
  - `token_check_assign_primary()`: a non-primary token below `SecurityImpersonation` is refused
    (`STATUS_BAD_IMPERSONATION_LEVEL`); a token outside the caller's lineage needs an enabled
    `SeAssignPrimaryTokenPrivilege` or `SeTcbPrivilege` (`STATUS_PRIVILEGE_NOT_HELD`), re-implementing ReactOS's
    child/sibling rule;
  - `token_duplicate` will not raise an identification-level token to a primary token;
  - `check_object_access` fails every open made while impersonating an identification-level token
    (`STATUS_BAD_IMPERSONATION_LEVEL`, a registry key `STATUS_ACCESS_DENIED`).
- `server/process.c`: `new_process` and `set_process_info(SET_PROCESS_INFO_TOKEN)` call
  `token_check_assign_primary()`; `grant_process_admin_token` needs an enabled `SeTcbPrivilege`.
- `dlls/advapi32/security.c`: `CreateProcessWithTokenW` needs `SeImpersonatePrivilege`
  (`ERROR_PRIVILEGE_NOT_HELD`).
- `dlls/shell32/shlexec.c`, `dlls/kernelbase/process.c`, `dlls/ntdll/loader.c`: read the helper from the 64-bit
  registry view, as the server does. 32-bit processes previously read `Wow6432Node`.
- `dlls/advapi32/tests/security.c`, `dlls/shell32/tests/shlexec.c`: conformance tests for the above, and the
  approved-helper cases now expect `ERROR_BAD_IMPERSONATION_LEVEL` until the elevation broker exists.

## Elevation boundary, plan step 2b: the limited token (2026-10-22)

Wine commits `4fced06b`, `839c0f31`, `4fa6f4ac` (hashes of the working branch; the pushed ones differ).

- `server/token.c`, `server/security.h`: the `LUA_TOKEN` and `DISABLE_MAX_PRIVILEGE` bodies of
  `DECL_HANDLER(filter_token)` moved into `token_apply_filter()`; `filter_token` behaves as before.
  `token_make_filtered()` applies the filtering to the root token. No behaviour change on its own.
- `server/process.c`: while a helper is configured, the root token (the one every process inherits) goes through
  `token_make_filtered()`: Administrators deny-only, `SeChangeNotifyPrivilege` only, Medium integrity, elevation
  Limited. Helper unset: unchanged. The registry is loaded in `init_registry()` before the first client can
  connect, so the setting is known when the first process is created.
- `server/token.c` (`token_make_filtered`): the filtered root token keeps the native filtered-admin privilege set that
  Wine defines (D1): `SeChangeNotifyPrivilege` enabled, `SeShutdownPrivilege` and `SeUndockPrivilege` disabled.
  `filter_token` with `DISABLE_MAX_PRIVILEGE` still keeps only `SeChangeNotifyPrivilege`, as on Windows.
- `server/token.c` (`create_linked_token`): the linked (full) token is the same token, but its handle is now granted
  with the access the caller's integrity level allows (`check_object_access` with `MAXIMUM_ALLOWED`) instead of an
  unconditional `TOKEN_ALL_ACCESS`. A Medium caller otherwise cannot get a handle to a High-labelled token (error 5).
  For a High caller the granted mask is the same as before.
- `server/registry.c` (`key_get_sd`): the default key descriptor now has two variants. Keys under `\Registry\User`
  grant the user, SYSTEM and Administrators full control; other keys grant Users read on the key itself, SYSTEM and
  Administrators full control, and keep the old inherit-only Users ACE last (`advapi32` test `test_GetNamedSecurityInfoA`
  inspects the last Users ACE). Unconditional. **Note:** `check_object_access` treats a key with no explicit
  descriptor as unprotected, and `key_get_sd` is only used when a descriptor is queried, so this change alters
  what `GetSecurityInfo` returns, not what a limited process may do. See the step 2b work order, "Results".

## Elevation boundary: registry and service manager access checks (2026-10-22)

Applies only while `HKLM\Software\Wine\Elevation\Helper` is configured (`elevation_enforced()`), except where
marked unconditional. Work order: `ELEVATION_REGISTRY_ENFORCEMENT_WORKORDER.md`. Wine commits (working branch
`ccr-work`): `91b53ca8`, `c28aafba`, `437525d3`, `2487739e`, `9bb835d1`, `a252e52b`, `d668329f`.

- `dlls/win32u/sysparams.c` (`reg_empty_key`): stops at the first failed delete instead of enumerating index 0
  forever. **Unconditional**, upstreamable.
- `server/token.c` (`check_object_access`): a registry key without an explicit descriptor is checked against its
  default one (`key_get_sd`) instead of being granted.
- `server/registry.c`:
  - `create_key` checks `KEY_CREATE_SUB_KEY` on the parent before it creates a new key (a lookup of the parent
    first, so a denied create leaves no key behind). The handle of a new key is still granted without a check;
    opening an existing key already checked the requested access.
  - **Deviation, accepted by the user (work order item 2):** the keys of the display device cache default to
    Users full control (`display_cache_paths`, `key_in_display_cache`): `Enum\DISPLAY`, `Enum\PCI`, the display
    and monitor `Control\Class` GUID keys, `Control\Video`, `Control\DeviceClasses`, `Hardware Profiles\Current`,
    `HARDWARE\DEVICEMAP`, `Software\Microsoft\DirectX`, with their subkeys. win32u's `prepare_devices` rewrites
    them in every GUI process, where Windows has SYSTEM components do it. A new key above one of them (for example
    `HARDWARE\DEVICEMAP`) may be created too. Effect: a limited process can change the cached display configuration.
  - **Deviation, accepted by the user, temporary:** `Control\ServiceCurrent` (the control pipe number services.exe
    writes for each service process it starts) and `Control\ComputerName\ActiveComputerName` (written by
    `wineboot --init`) are in the same table. Both writers run on the limited token until they get the system
    role; that step removes the two entries (`shortcut:` comment in `server/registry.c`).
- `server/named_pipe.c`: the client end of a named pipe records the token of the thread that opened it, and
  `FSCTL_PIPE_IMPERSONATE` impersonates a copy of that token (stock Wine impersonates the caller itself, with a
  FIXME). Needed so a service can check the access its client really has.
- `programs/services/rpc.c`: `OpenSCManager`, `OpenService` and `CreateService` check the requested access against
  the documented default descriptors of the service control manager and of a service, with the RPC client's token
  (`check_client_access`). The checks are skipped while no helper is configured.
  The default service descriptor gives LocalSystem Wine's `SERVICE_SET_STATUS` (0x8000) too: plugplay in
  `winedevice.exe` (SYSTEM) opens the driver service of `ZwLoadDriver` (for example `winehid`) with it, and no process
  record of `services.exe` names that process for a driver, so the own-process rule did not apply and the load failed.
- `server/named_pipe.c`, `server/token.c` (`token_impersonation_level`): the level a pipe server gets is capped like
  Windows' SeTokenCanImpersonate. Without `SeImpersonatePrivilege` in the server's process token it is
  identification, unless the client has the server's user, is not more trusted (integrity) and is not elevated
  while the server is not. The client's requested SQOS level is not sent to the server (`shortcut:` comment), so
  the cap is the client token's own level. `open_token` with `OPEN_TOKEN_AS_SELF` checks access against the
  process token, so an identification-level caller can still read its client's identity.
- `programs/services/rpc.c`, `dlls/rpcrt4/rpc_transport.c`, `dlls/sechost/service.c`: `OpenService` also grants
  the process the SCM started for a service the rights it uses to report status
  (`SERVICE_SET_STATUS`, query, `READ_CONTROL`), found by comparing `I_RpcBindingInqLocalClientPID` with the
  recorded process id. rpcrt4 implements that call for `ncacn_np` (stock has it for `ncalrpc` only). sechost opens
  the second handle of its own service with `SERVICE_QUERY_STATUS|SERVICE_QUERY_CONFIG` instead of
  `GENERIC_READ|GENERIC_WRITE`. `RpcSs` is openable for start and status by anyone, because Wine starts it on demand
  (`shortcut:` comment).
- Callers that asked for `KEY_ALL_ACCESS` on `HKLM` only to read: `dlls/ntdll/version.c` (Windows version values,
  `KEY_READ`), `dlls/kernelbase/locale.c` (NLS and time zone keys, `KEY_READ`). Upstreamable.
- `dlls/shell32/shellpath.c` (`_SHOpenProfilesKey`): when the create with `KEY_ALL_ACCESS` is denied, opens
  `ProfileList` for `KEY_READ`, so shell folders resolve for a limited token.
- `programs/explorer/desktop.c`: the display driver key is created with `MAXIMUM_ALLOWED`; creating a missing
  parent chain opens the existing parents with the requested access, which `KEY_SET_VALUE` is not on `System`.

OOBE final-correctness repairs (2026-10-19) change owned backend/test code only.
The regedit fallback selects existing matching-architecture installed Wine PEs;
it does not replace stock DLLs or alter the Wine gitlink. See
`OOBE_COMPONENTS_FINAL_CHECKS_REPORT.md` for scope and remaining acceptance gates.

Anything under `wine/` that is not new project code, and what we changed. Keep this list short: every entry makes
rebasing on upstream Wine harder. Refactor baseline = the real `wine-11.19` tag
(`455e3509b98a6919fd4ad1def4803e08c41c03b2`); see `REFACTOR_MAP.md` for the audit.
Commit `40c17e1` is the imperfect upstream import, which also includes post-tag upstream changes.

P4 established Wine as a gitlink to d0a0af0681 on wineexplorer/11.19.
Current pin is ed5fe96, integrating the L2 topic through the real-program
checkpoint in WAYLAND_SESSION.md, visual-style support, prefix-split S2/S3
and the independent Fluent-theme changes. The native tray bridge is owned
wexhost/wexshell code and adds no stock-Wine patch.
Fork-only ed5fe96 keeps hosted groups active through logical client-rectangle
changes and orders the layout request before frame commits. Layout replies
do not replay first-adoption activation. The private protocol wire format is
unchanged. Together with compositor acc46fe6 and the owned shell's stale-scale
observation change, maximized foot passes 100%→150%→100%: hosting,
focus/input, client alignment, work area and taskbar. Restored-window scaling
remains intermittent and open; the initial passing run is not a complete
fix. Two follow-up owned-shell trials were unsuccessful and removed. Both
Wine architectures build; fresh-ISO installs, application and tray checks
pass with the restored-scale failure recorded in WAYLAND_SESSION.md.
The polish topic `upstream/win32u-surface-shape` (bcf842a) fixes win32u's
single-byte surface-shape spans: intersect the left mask with the complement
of the right mask, matching the multi-byte path. This fixes volumeicon
Preferences' missing side borders under Wayland. The full 32/64-bit build and
real-program frame matrix at 100%/150% pass. It is also on wineexplorer/11.19.
The earlier "topic, not production" descriptions below are historical: those
commits are now included. Fresh ISO installation acceptance passed: default-X11
Alice login, selected-Wayland Bob login, 32/64-bit applications, real native
applications and the prefix-split assertions (see WAYLAND_SESSION.md).
See WAYLAND_L1.md;
X11 remains the default.
The final C/D same-ISO regression also passes at b1d3ab0 in fresh default-X11
and selected-Wayland installations, including tray and X11 effects/dialog
checks. See WAYLAND_SESSION.md review point 2 for exact image and evidence.
Wine topic 0f25ee1 fixes the existing shell-view default icon spacing call on
64-bit Wine: MAKELPARAM(-1,-1) zero-extends to 0xffffffff, so comctl32 treats
it as literal 65535x65535 spacing. Passing (LPARAM)-1 restores the intended
default. The actual installed Wayland Control Panel's 13 entries now draw;
both architectures build.
The L2 topic c49ebd8 keeps pointer motion outside the old surface bounds during
explicit private compositor capture. A real foot window on the installed
Wayland session could otherwise never grow past its original right/bottom edge.
Ordinary uncaptured motion retains Wine's rounding clamp. Physical QEMU edge
grow/shrink, corner grow and subsequent typing pass. This is still on the L2
topic, not the production pin; see WAYLAND_SESSION.md for the active work order.
Topic 6b3435c coordinates real foot work-area maximize/restore using private
scalar capture and maximize requests in the existing Wayland driver path.
Topic cc59531 draws caption buttons within Wine's metric hit slots: real foot's
drawn maximize glyph previously hit HTMINBUTTON. Topic ac2cc1e restores held
modifiers from wl_keyboard.enter before native xterm's Alt+Tab reaches Wine,
and keeps private-shell topmost overlays independent so Explorer's switcher
is not clipped into the hosted client hole. No driver ABI hook or protocol
version was added. These remain topic commits, not the production Wine pin.
Topic 4fb0940 publishes the existing preferred wl_surface scale as a private
scalar property with an owning-thread notification, so native inventory's
logical rectangles can be reconciled into Wine pixels. It refreshes zone
placement and reserved edges at a scale change. Actual foot resize,
maximize/restore and typing at 150% pass; live maximized scale change remains
unresolved. The owning bridge uses the same scale conversion as the driver.
Topic 055abb3 reuses the existing transparent dummy-content helper when an
all-client hosted CSD proxy changes size without GDI damage. Actual gedit at
150% otherwise loses its taskbar item after startup work-area clamping;
typing and taskbar minimize/restore pass with the fix. Visible SSD pixels and
minimized proxy geometry do not use this path.
The 6b3435c requests preserve the compositor's physical focus/capture checks. Controlled
hosted views acknowledge xdg state without allowing an obsolete
NOSENDCHANGING configure size to overwrite the native-ready frame, and flush
pending frame content after acknowledgement. No generic Wine hook or private
protocol version was added. Exact work-area maximize, taskbar minimize/restore,
menu Restore, and post-restore typing pass on the installed test overlay at 100%.
The L2 topic's cf365e5 binds private protocol v10 and adds a scalar resize
request/commit/cancel transport. Each compositor transition is queued separately
and delivered on the HWND's owning UI thread with a scoped property snapshot;
no Unix pointer enters WoW64 PE and no application call holds win_data_mutex.
Surface generations protect destruction/replacement during reentrant delivery.
Accepted transactions preserve hosting while the frame catches up; only matching
serial completion updates the accepted client hole. Protocol availability is
distinct from native-client eligibility and from enabled sizing controls.
This is fork-only and does not add a driver ABI hook or enable interactive
resize/maximize. See WAYLAND_L2_RESIZE_REVIEW.md for the next boundary review.
The subsequent owned wexhost WH_MSGFILTER handles pending transaction dispatch
and terminal-input deferral without another stock Wine change. The unhosted
64/32 keyboard-loop baseline reproduces production 71572805's first-timer exit;
existing L2 topic commits 445b580 and 903ac75 already fix the generic pre-press /
capture-cancellation loop behavior. Post-release pending publication is handled
in the owned hook, not by adding private shell checks to generic win32u.
Wine topic 07eb6e0 adds a private RESIZE_ENABLE lab property and routes SC_SIZE
to the Win32 sizing loop only for an explicitly opted-in v10 hosted frame.
Stock/old peers and ordinary Wine windows retain their existing xdg handling.
The owned wexhost controller records desired rectangles separately, applies
native-ready frames and publishes only after terminal input. Production controls
do not set this property; live drag previews, DPI and broader-client acceptance
remain pending. No generic win32u hook or new cross-architecture ABI was added.
The next topic commit a061cea queries SPI_GETDRAGFULLWINDOWS for top-level
size/move loops as it already did for child windows, instead of forcing live
dragging. It introduces no private-property check or driver hook in win32u.
The owned controller now publishes matching live previews, retains loop-original
Escape geometry, quarantines FAILED hosting and latches constrained final
geometry in drag-off mode. All of this remains opt-in; DPI and remaining
cancellation/regression acceptance are required before enabling controls.
Compositor 8cb6e315 adds only delayed/ignored/client-selected test publishers;
the compositor runtime is unchanged at e8e5a083.
Wine 7efbcbb subsequently aligns event client rectangles to the existing
surface-local frame grid when absolute rounding would produce a different hole
or origin. It keeps the usual physical rectangle when that maps exactly.
Compositor bea783b6 allows a one-logical-pixel outer-border quantization at
fractional scales only, while keeping native/client-hole dimensions exact,
requiring the hole inside the frame and preserving strict integer-scale
extents. It also reasserts retained pixels' visible primary output before frame
callbacks. No request, ABI or protocol version was added. Actual small-step foot
acknowledgement-only buffer reuse remains an acceptance blocker; review it before
changing readiness semantics or exposing controls. See WAYLAND_L2_RESIZE_REVIEW.md.
Project CPLs/programs, wexhost and theme definitions live under src.

The L2 topic merged production d92d7e4 in 0607ece without changing the shared
registry implementation. Compositor eca5e619's rollback fix preserves the
forward content-response requirement: only an acknowledged rollback surface
commit may confirm the identical retained original buffer at its original
dimensions. Bare acknowledgements, unacknowledged commits and different buffers
cannot use this exception. This restores safe cancellation for foot's unchanged
buffer responses; it does not declare them successful forward resizes or enable
production controls. See the 2026-10-16 section in WAYLAND_L2_RESIZE_REVIEW.md.
L2 opt-in topic codex/wayland-l2 (07eb6e0, including merge 2c3be3b of production
859a1b9, not the production pin) extends the
private protocol through v10, starting with v4 native/controller atom properties and an
asynchronous reservation status. winewayland serializes requests and maps owned
native proxy HWNDs as independent toplevels while keeping ordinary owned-window
behavior. The standard wlr XML is vendored only to satisfy private protocol
interface definitions, with its original notice. Reservation leaves native
composition/focus unchanged. Explicit HOST opt-in now uses v7 numbered group
configuration and focus acknowledgements, preserving logical foreground during
native input and guarded temporary frame focus. Client size changes detach
before retry; callers must exclude the client area from Win32 hit testing.
Private hosting changes are fork-only. Compositor topic d859bdf1 provides v5 active scene grouping
with frame-ring render/input exclusion and disconnect recovery, plus v6 observed
physical focus acknowledgements and guarded frame/native restoration, plus v7
numbered configuration and native size/policy recovery. Driver fallback for old
servers remains. Owned wexhost now binds explicit SSD hosts through the v7
properties with UI-thread lifetime, region and focus-loop management; Explorer
adoption is now gated by WEX_WAYLAND_NATIVE_WINDOWS=1 for isolated tests; AUTO
remains disabled. The compositor also publishes replacement decoration policy
atomically, avoiding a false CSD notification during activation. It now observes
mapped xdg-toplevel parent changes with a lifetime-managed listener and publishes
them through the standard foreign-toplevel interface. Explorer detaches native
child proxies before destroying their owner frame, preserving native lifetimes.
These owner-lifetime fixes add no further Wine fork changes. See WAYLAND_L2.md
for 32/64 verification and worktree paths; L2 remains separate from production.

L2 dd18609 adds a fork-only in-process uxtheme/bridge region contract. A borrowed
outer HRGN and a scoped mask-update flag preserve automatic theme-region ownership
while wexhost excludes the native client. The HRGN is read only on the owning UI
thread; neither property is serialized to the driver/compositor. Existing explicit
application regions keep precedence. Owned code retains compatibility definitions
so master still builds at its production Wine pin. That pin does not gain this
runtime feature until the L2 topic is integrated. In 3fd4865, hosted minimized
frames retain normal content instead of uploading iconic-caption buffers; group
geometry ignores iconic dimensions, readiness can retry after a buffer-size
change, and activation clears the preceding epoch's focus observation. Restore
is explicit before requesting native focus. Compositor e7be9560 rejects focus
requests for minimized groups, preventing a delayed request from reopening them.
An active group's state is driven once through its Wine frame; wexhost's separate
control connection remains the fallback outside an active group. No new protocol
version, WEX_NATIVE_WINDOW field, Unix-call index or public Windows API is added.

The subsequent b971d96 synchronizes the v9 constraint XML. V9 adds a maximum-size
event to native observation, not native resize requests; the driver keeps binding
v8. Compositor 2e88585f includes maximum dimensions in shared size hints, reports
them only to v9 observers, and detects committed XDG constraint changes without
requiring a move/repaint. For wlroots 0.18's missing WM_NORMAL_HINTS notification,
XWayland uses the public XWM event hook and a per-view deferred notification after
wlroots parses the property; pending notifications are removed on destruction.
No X event is consumed or parsed independently, and existing window-size policy
is unchanged. Owned wexhost fills existing max_size fields, preserves state
transitions when coalescing geometry events, and wexshell applies independent
axis limits in WM_GETMINMAXINFO. See WAYLAND_L2.md for compatibility checks.

180d405 synchronizes the v10 private XML across Wine, wexhost and compositor.
Wine's driver still binds v8 and bridge observers bind v9; there is no new Wine
runtime behavior at this checkpoint. Compositor dd65ccef adds serial-specific
XDG hosted resize requests, constraint acceptance, configure acknowledgement,
native-buffer readiness and matching-frame publication. Public scene/buffer
APIs retain old root buffers until atomic handover; supersession and bounded
rollback retain the original geometry. Focus/capture loss starts rollback;
failed rollback restores ordinary native composition. Snapshot listeners and
references are released on either window's destruction. XWayland resize requests
are explicitly unavailable, pending a separate native completion path. The
production pins, WEX ABI, adoption gate and sizing affordances remain unchanged.
The following synchronization merges Gemini's production 71572805 into the L2
topic as c6dac92; it adds no further Wayland behavior. Master keeps the incoming
prefix-split S0/S1 merge and its new pin. Those installer/session/stock changes
are synchronized from their owner rather than edited by this L2 work.
Compositor e8e5a083 requires a content-bearing native commit after acknowledgement
(buffer, damage, viewport, scale or transform); committing only acknowledged xdg
state cannot incorrectly report native readiness. A deterministic before/after
probe covers that separation. b41b65f fixes shared-header dependencies in the
compositor test-client build so window-layout changes rebuild every client.

Wine ac4a1b3 fixes an observed win_data_mutex/USER lock inversion during themed
frame creation/readoption. The private Wayland handle and association callbacks
update scalar state, unlock and post an internal WM_WAYLAND_WEX_UPDATE; the
owning UI thread refreshes current styles/z-order/hosting. The new message is
appended without changing existing message numbers and carries no surface pointer
or stale geometry. Focus/group notifications also post after releasing window
data. Epoch guards and private protocol versions remain unchanged. A CSD/SSD
stress probe fails before the fix, passes 100 cycles in each Explorer architecture
afterward. Compositor 9e4e3d4a only increases the test fixture's stable record
capacity for long stress runs; it adds no runtime compositor behavior.

The L2 move checkpoint adds fork-only v8 frame pointer capture and its Wine
SetCapture hook, guarded by the current focus epoch and active HOST state.
Hosted Move uses Wine's loop only with v8; zone placement echoes are ignored
for these proxies because native observation owns reconciliation. Generic
Wine fixes retain keyboard/menu move loops before a button press, terminate
them on capture loss, and commit pure zone moves without waiting for repaint.
Those generic fixes can be extracted for upstream review; no MR is opened.
The compositor routes pointer warps through normal hit testing with xdg surface
geometry offsets. Explorer activates task-menu Move/Size targets and separates
requested geometry from older observations. Production 2e8bcfa carries only
the shared property definitions needed to compile the existing owned bridge;
the L2 topic merges that header fix. Wine's consumption of resize negotiation
remains pending; the compositor's XDG transaction is implemented separately.
Display Properties, wireless UI and media player replace stock stub modules
through explicit assembly entries, with pristine originals preserved.
Those implementations are project modules rather than source patches in
the retained fork. VM sync resolves the gitlink separately from owned source.

P3b project modules now include wexres (shared resources), themeui (shared theme
engine, used directly by desk.cpl) and msgina (Turn Off/Log Off, called directly
by logonui), plus stobject's registry-driven COM volume/power tray service.
Netshell now lives under src/dlls, owns network tray/status/properties/backend,
and exposes the shared UI to ncpa.cpl. Wireless selection lives in wzcdlg and
is loaded through a typed export by the shell/netshell. Wine's boot DLL registration list includes
stobject and netshell; the shell enumerates
ShellServiceObjectDelayLoad and tolerates missing optional DLLs/services.
Their implementation and interfaces live under src/. Configure
and header installation entries are retained on wineexplorer/11.19 through
d0a0af0681. Search now lives in optional wexsearch with an opaque UI interface;
its class/resource ownership, file search, cancellation and absent-module
checks pass. Browser windows now live in optional wexbrowse behind one
string/flag entry point; taskbar toolbars remain core. See refactor/P3B.md
for extraction acceptance. The complete repository/build/install acceptance
is in refactor/P4.md; P5 assessment keeps unsupported extension replacements
as X commits. The separately pushed P6 candidate passes full build/runtime
smoke without changing the production pin (REBASE.md).

## Elevation boundary: administrator identity (C1, 2026-10-22)

Decision S3 of `ELEVATION_SYSTEM_ROLE_WORKORDER.md`. Applies only while `HKLM\Software\Wine\Elevation\Helper` is
configured (`elevation_enforced()`); with the helper unset nothing changes. Wine commit (working branch
`ccr-work`): `f180417289b`.

- `server/registry.c` (`user_is_administrator`, `admin_group_name`): the server's user is an administrator when the
  group database says so (`getpwuid(getuid())`, `getgrouplist`) for the group `sudo`, or the group named by
  `admin_group=` in `/etc/wineexplorer/sysrun.conf`. That file is read only if it is a regular, root-owned file that
  is not group- or other-writable (checked on the open descriptor, `O_NOFOLLOW`); otherwise `sudo`. The environment
  is never consulted: the user controls it and could name a group they are in. Cached for the server's lifetime, so a
  membership change applies at the next logon.
- `server/process.c` (`create_process`), `server/token.c` (`token_make_standard`): the first token of a session
  made while enforced is filtered as before; for a user outside the group it also loses the Administrators group
  and becomes `TokenElevationTypeDefault` (Medium integrity and the filtered privileges are the filtered token's).
  `standard_user_session` is then set for the server's life.
- `server/token.c` (`create_linked_token`): fails with `STATUS_NO_SUCH_LOGON_SESSION` once `standard_user_session`
  is set (provisional: the native error of `GetTokenInformation(TokenLinkedToken)` on a token without a split is not
  documented; Microsoft only says `TokenElevationTypeDefault` means no linked token).
- `server/process.c` (`grant_process_admin_token`): `STATUS_ACCESS_DENIED` once `standard_user_session` is set.
- The `wex-sysrun` launcher (`tools/wex-sysrun`) ignores the `admin_group` key instead of refusing the file.
- Not done here: the local Administrators group view of netapi32 (`NetLocalGroupGetMembers`, a stub that reports the
  current user), see the work order's "Results, C1".

## Elevation boundary: system role server and launcher wiring (C2, 2026-10-22)

Constraints a to d and f of `ELEVATION_SYSTEM_ROLE_WORKORDER.md`. Active only when the root-owned, not
group/other-writable `/etc/wineexplorer/sysrun.conf` (regular file, checked on the open descriptor, `O_NOFOLLOW`)
names the machine root, and, for the server, only in a process that the setgid launcher started (real gid differs
from the saved gid, effective gid equals the saved gid, and the saved gid is the configured group). Otherwise nothing
changes. The launcher is installed by default (since 2026-10-23; `WEX_SYSRUN_ENABLE=0` opts out). Wine commits (branch `ccr-work`): `0e98e935c36`,
`71fe07ea021`.

- `server/sysrun.c`, `server/sysrun.h` (new), `server/Makefile.in`, `server/main.c` (`sysrun_init`): the server drops
  its effective gid to the real one at once and keeps the system group as saved gid, and sets the process
  non-dumpable (`PR_SET_DUMPABLE 0`) after every `setegid`, so that `fs.suid_dumpable` cannot make it dumpable.
  `sysrun_raise` and `sysrun_drop` raise and drop the effective group.
- `server/process.c`, `server/process.h` (`create_process`): `SO_PEERCRED` of the connection (pid, uid, gid) is
  recorded per process when the system role is active. The `unix_pid` the client sends is not used for this.
- `server/fd.c`: every open, create, mkdir, rename, link and unlink that the server does for a client goes through
  `sysrun_open`, `sysrun_mkdir`, `sysrun_rename`, `sysrun_link` and `sysrun_unlink`, which raise the effective group
  only when the requesting client has the system group (its connection credentials: uid equal to the server's and
  gid equal to the system group). The unlink or rmdir of delete-on-close and disposition runs raised only for a file
  that such a client opened (`closed_fd::sysrun`, inherited by `dup_fd_object`). Everything else, including `ftruncate`
  and `fchmod` on an already opened descriptor, runs with the user's group.
- `server/registry.c`: the machine hives (`system.reg`, `userdef.reg`) come from the fixed machine root of the
  configuration, not from the prefix: `O_NOFOLLOW` on the open, `fstat` must show a regular file of root and the
  system group that others cannot write, else the server refuses to start. They are saved with the raised group
  (`faccessat(AT_EACCESS)` only there) into the existing file, in place (`save_hive_in_place`; a new file would belong
  to the user's uid); a symlink at the hive is never followed on save; a missing file is not created.
  The group is raised for the save only if elevation was enforced by the machine hive loaded at start
  (`machine_hive_saved`, not the live helper value, which a user could set in memory); otherwise the hive keeps the
  stock rule (saved only if the real user can write it), so with the launcher but no helper nothing persists.
- `dlls/ntdll/unix/virtual.c` (`kernel_writewatch_init`): a failed open of `/proc/self/pagemap` with `EACCES` or
  `EPERM` in a non-dumpable process (`PR_GET_DUMPABLE` is 0: the system group of the launcher) logs at WARN, not ERR;
  the fallback is unchanged. `/proc/self/*` of a non-dumpable process belongs to root.
- `server/registry.c` (`key_in_user_tree`): while elevation is enforced and the system role is active, only the
  subtree of the token user's own SID and its `_Classes` count as the user's tree. `HKU\S-1-5-18`, `-19`, `-20` are
  created at start and, like `.DEFAULT`, get the machine default descriptor.
- `dlls/ntdll/unix/sysrun.c` (new), `dlls/ntdll/Makefile.in`, `unix_private.h`: reads the configuration, spawns the
  launcher, and checks a server for a client whose own effective gid is the system group
  (`sysrun_client_is_system`, `sysrun_server_ok`). Such a client refuses a server whose saved gid (from
  `/proc/<pid>/status`) is not the system group, whose ids are not the user's, or whose connection uid differs.
  The image path (`/proc/<pid>/exe`) cannot be compared: it is not readable for the non-dumpable server.
- `dlls/ntdll/unix/server.c` (`server_connect`): the check above; `dlls/ntdll/unix/loader.c` (`start_server`): with the
  configuration and the launcher the server starts through `wex-sysrun wineserver`; if the launcher refuses (exit
  77) a warning is printed and the stock start follows.
- `dlls/ntdll/unix/env.c` (`run_wineboot`): with the launcher a `wex-sysrun wineboot-init` runs first (machine root
  prefix, system group, connected to the user's server by the launcher), then the normal wineboot still does the
  user's part; a refusal prints a warning and only the normal one runs. A client with the system group does not start
  wineboot itself. `programs/wineboot/wineboot.c` (`start_services_process`): does not start `services.exe` again when
  the `SVCCTL_STARTED_EVENT` is already signaled.

## Elevation boundary: the system role, SYSTEM and service tokens (C3, 2026-10-22)

Phases C and D and constraint g of `ELEVATION_SYSTEM_ROLE_WORKORDER.md`. Everything below is active only while the
system role is active (the server was started by the setgid launcher, `sysrun_active`) **and** elevation is enforced
(`HKLM\Software\Wine\Elevation\Helper` set); otherwise stock behavior. Exception: `programs/services` also checks
whether its own token is SYSTEM, which only the system role gives it.
Wine commits (branch `ccr-work`): `edf575a2260`, `9ab7dee645e`, `bd22d5ee0d9`.

- `server/token.c`, `server/security.h` (`token_create_service`, `token_service_account`): primary tokens of
  LocalSystem (S-1-5-18: groups Everyone, Users, SERVICE, CONSOLE LOGON S-1-2-1, Authenticated Users, This
  Organization S-1-5-15, LOCAL S-1-2-0, Administrators (enabled, owner and primary group, not mandatory)),
  LocalService (S-1-5-19) and NetworkService (S-1-5-20: the same without Administrators; the account owns what it
  creates), System integrity (S-1-16-16384), `TokenElevationTypeDefault`, no
  linked token. Default DACL: SYSTEM and Administrators for LocalSystem, SYSTEM and the account for the others. The
  privilege and group lists are the native ones measured in docs/ELEVATION_LOCAL_VALIDATION_WORKORDER.md; SeImpersonate
  stays enabled on the service accounts (documented default) and CONSOLE LOGON is provisional (possible task
  artifact); per-task SIDs and the unknown S-1-5-32-* SIDs are not copied (docs/TOKEN_SECURITY_REFERENCES.md).
- `include/winternl.h`, `dlls/advapi32/security.c`, `server/token.c`: privileges 31 to 36 are known (names and LUIDs
  of SeIncreaseWorkingSet 33, SeTimeZone 34, SeCreateSymbolicLink 35, SeDelegateSessionUserImpersonate 36;
  `SE_MAX_WELL_KNOWN_PRIVILEGE` is 36; 31 and 32 have no name), so LookupPrivilegeNameW and LookupPrivilegeValueW
  resolve them. `token_make_filtered` adds SeIncreaseWorkingSet and SeTimeZone (disabled), as the native filtered
  token has. `LookupPrivilegeNameW` fails with ERROR_NO_SUCH_PRIVILEGE for the unnamed LUIDs 31 and 32 instead of
  dereferencing a NULL name. `token_create_admin` is unchanged.
- `server/token.c` (`token_duplicate`): a token whose owner is its user keeps that owner in a copy.
- `server/protocol.def`, `server/token.c` (`create_service_token`), `include/winternl.h`,
  `dlls/ntdll/unix/process.c` (`ProcessWineCreateServiceToken` = 1003, input account number 0 to 2, output the
  token handle): creates such a primary token. Refused with `STATUS_PRIVILEGE_NOT_HELD` without an enabled
  SeTcbPrivilege, with `STATUS_ACCESS_DENIED` for a caller that is not the SYSTEM process of the system role (Wine's
  administrator token holds SeTcbPrivilege too), `STATUS_NOT_SUPPORTED` without the system role.
- `server/token.c` (`create_token`): while elevation is enforced, `NtCreateToken` needs an enabled
  SeCreateTokenPrivilege, which no Wine token has (native: the same privilege, held only by the LSA). Without this
  check a limited process could make an administrator token, impersonate it, and pass every access check.
- `server/sysrun.c`, `server/process.c`, `server/process.h`, `server/thread.c`, `server/mapping.c`
  (`sysrun_process_created`, `sysrun_thread_created`, `sysrun_main_image`): a root process whose connection has the
  system group is protected from the start (process and thread descriptor as below, so a limited
  or standard token cannot open it with any dangerous right) and gets the SYSTEM token when its main image is verified: the
  mapped file's device and inode (`fstat` of the server's descriptor) must equal `services.exe`, `wineboot.exe` or
  `winedevice.exe` of the machine root's `system32` (or `wineboot.exe` and `winedevice.exe` of `syswow64`). The NT
  path is never used, it goes through the user's prefix links. Any other image: its raised file access ends at once and
  the process is killed from the main loop (a timer, 10 ms later: killing it inside its own request crashed the server). A child gets its parent's token or the one it is given and a
  descriptor from it when that token is SYSTEM, LocalService or NetworkService. The connection credentials of a
  child are those of the process that made the socket pair, so the server clears `peer_gid` for a child with a
  service-account token: it never gets the system group's file access.
- `server/token.c` (`set_token_integrity_level`): while elevation is enforced, raising a token's integrity level
  needs an enabled SeTcbPrivilege in the caller's process token (`STATUS_PRIVILEGE_NOT_HELD`), lowering does not (as
  on Windows; measured natively, docs/TOKEN_SECURITY_REFERENCES.md).
- `server/sysrun.c` (`protect_object`): the descriptor of a system role process or thread is SYSTEM (and the service
  account) full control, Administrators query + read the descriptor, Users and Authenticated Users the limited query +
  SYNCHRONIZE (thread: `THREAD_QUERY_LIMITED_INFORMATION`), as Windows does for service processes. Full access needs
  an enabled SeDebugPrivilege.
- `server/token.c` (`check_object_access`): an enabled SeDebugPrivilege opens any process or thread whatever its
  descriptor says (Windows does the same), while the system role is active. A filtered or standard token has no
  SeDebugPrivilege; an elevated administrator token has it disabled until the program enables it.
- `server/sysrun.c` (`sysrun_check_process_access`), `server/hook.c` (`set_hook` with a thread or process),
  `server/queue.c` (`attach_thread_input`), `server/console.c` (`AttachConsole`): the requests that name another
  process or thread by id instead of by handle are checked against its descriptor.
- `server/registry.c` (`boot_state_paths`): `Control\ServiceCurrent` and `Control\ComputerName\ActiveComputerName` are
  Users-writable only while the system role is **not** active (no launcher: services.exe and `wineboot --init` run
  limited); with it they are written as SYSTEM and the grant is gone.
- `dlls/ntdll/unix/sysrun.c`, `process.c`, `loader.c`, `unix_private.h` (`sysrun_must_drop_group`,
  `sysrun_prepare_child`, `sysrun_early_init`): a client with the system group that starts a process with a token
  other than SYSTEM (or none) drops the group in the forked child (`setresgid(gid, gid, gid)`, the child cannot get
  it back) before the loader is executed. The child marks itself not dumpable first thing (`WINESERVICENODUMP`), and
  so do its children, so the user cannot ptrace it. Shortcut: a short window between the exec and the flag.
- `programs/services/services.c` (`has_system_role`, `get_service_token`, `service_start_process`,
  `process_create`): with the SYSTEM token services.exe starts every service with `CreateProcessAsUserW` and the token
  its `ObjectName` names (empty or `LocalSystem`; `NT AUTHORITY\LocalService`; `NT AUTHORITY\NetworkService`).
  Any other name fails with `ERROR_SERVICE_LOGON_FAILED` for now (a user account needs a logon). The control pipe of a
  service has a descriptor for SYSTEM and the account. services.exe sets its own token's default DACL to
  SYSTEM, Administrators and Authenticated Users (read, write, execute): objects with no descriptor (the
  `\pipe\svcctl` endpoint, `__wine_SvcctlStarted`) are created from it. Without the system role nothing changes.
- `programs/services/rpc.c`, `services.c`, `services.h` (`load_service_security`, `check_client_access`,
  `svcctl_QueryServiceObjectSecurity`, `svcctl_SetServiceObjectSecurity`): the descriptor of a service is the
  binary `Security` value of its registry key, else the native default; `SetServiceObjectSecurity` needs `WRITE_DAC`
  on the service handle, takes the DACL only (shortcut) and writes the value; `QueryServiceObjectSecurity` returns
  owner, group and DACL (it was `ERROR_CALL_NOT_IMPLEMENTED`). `dlls/sechost/service.c`
  (`QueryServiceObjectSecurity`, `SetServiceObjectSecurity`) call these over RPC; they were a semi-stub (an empty
  DACL) and `return TRUE`. **Ungated**: this is the documented behavior, for every services.exe, with or without the system role or enforcement.

## Elevation boundary: the elevation broker (plan step 4, 2026-10-23)

`ELEVATION_BROKER_WORKORDER.md`, items 1 and 2. Everything below is active only while the system role is active and
elevation is enforced; otherwise stock behavior (the build files and the service entry are the only ungated parts).

- `configure.ac`, `include/Makefile.in`, `loader/wine.inf.in`: the project program `programs/wexelevate` (source in
  WineExplorer `src/programs/wexelevate`, header `include/wexelevate.h`) is built and installed; `wine.inf` registers the
  demand-start LocalSystem service `wexelevate` ("Application Information", `[WexElevateService]`, all four
  `[DefaultInstall*.Services]` lists) with a `Security` value that lets Authenticated Users query and start it (a
  binary self-relative descriptor, `O:SYG:SYD:(A;;0x2019d;;;AU)(A;;0x2a1fd;;;SY)(A;;0xf01ff;;;BA)`, the default
  descriptor plus `SERVICE_START` for users). Always.
- `server/token.c` (`create_linked_token`): while elevation is enforced, a caller that is the system role's SYSTEM
  process (SYSTEM user, system group, enabled SeTcbPrivilege) gets the linked token as a **primary** token
  (native N6); everybody else still gets the identification-level token. Stricter than N6: Wine's administrator token
  holds SeTcbPrivilege too.
- `server/token.c`, `server/security.h` (`token_is_elevated`, `token_protect_elevated`), `server/sysrun.c`
  (`sysrun_process_created`, `protect_object`): **B8.** The server applies mandatory integrity labels to token
  objects but not to process and thread objects (no label, no descriptor, any access granted), so a process whose
  token is the full half of a split token gets the descriptor of a system role process: SYSTEM and the full token's
  Administrators full control, Users and Authenticated Users the limited query and SYNCHRONIZE; so do its threads.
  **The owner is Administrators, not the user**: an owner always gets WRITE_DAC, so a limited token of the user
  would rewrite the descriptor. The token object of an elevated process gets a descriptor of SYSTEM and
  Administrators only. Any other new process whose connection has the system group loses the raised file access
  (`peer_gid` cleared): its Unix process dropped the group.
- `server/token.c` (`open_token`): while the system role is active and elevation is enforced, opening the token of
  another process needs `PROCESS_QUERY_INFORMATION` (a thread: `THREAD_QUERY_INFORMATION`), as on Windows. Stock Wine
  asks for no right at all, so a limited process could open the full token of an elevated process of its user and
  impersonate it.
- `server/sysrun.c` (`system_images`): `drive_c/windows/system32/wexelevate.exe` may be the main image of a system
  group root process (B1). Dormant until the launcher accepts the broker as a mode; the broker is started by
  services.exe as a LocalSystem service and does not need it.
- `dlls/ntdll/unix/sysrun.c` (`sysrun_must_drop_group`): **B7.** A client with the system group keeps it for a child
  whose token has `TokenElevationTypeFull` (an elevated process), in addition to the SYSTEM token. The process stays
  non-dumpable (its effective group differs from its real group).

## Elevation boundary: consent work order, Part A, the read lease (plan step 6, 2026-10-24)

`ELEVATION_CONSENT_WORKORDER.md`, Part A. **No stock Wine file is changed.** Project modules only: `dlls/wexhost`
(new `lease.c`, three new exports `WexLeaseTake`, `WexLeaseArm`, `WexLeaseRelease`; the Unix side uses the stock ntdll
exports `wine_server_handle_to_fd` and the `list_processes` server request, both unchanged) and `programs/wexelevate`.
The broker holds a Linux read lease on the frozen image until the elevated process exits; a write open ends that
process. Active only inside the broker, which runs only with the system role.

## Elevation boundary: consent work order, Part B, click consent (plan step 6, 2026-10-24)

`ELEVATION_CONSENT_WORKORDER.md`, Part B. **No stock Wine file is changed.** Project programs only: `programs/wexelevate`
(starts the consent window itself when `ConsentPromptBehaviorAdmin` is 2 or 5; otherwise unchanged, `ERROR_NOT_READY`) and
`programs/consent` (new `/broker` and `/image` arguments, a 32768-character command line, details shown in full; the test hook
exists only in a copy that the test harness compiles). The existing server protection of system-role processes covers the consent
window; nothing was added to it.

## Elevation boundary: the secure desktop (plan step 5, 2026-10-24)

`ELEVATION_SECURE_DESKTOP_WORKORDER.md`, phases 1 to 4. Stock Wine files changed:
- `server/winstation.c`, `server/user.h`: `SwitchDesktop` (`set_input_desktop`) needs `DESKTOP_SWITCHDESKTOP` on the handle and
  a desktop of the caller's own window station (**not gated**: Windows behavior; the user32 and win32u suites give the same
  result before and after). While the secure desktop is the input desktop only a process of the system role may switch;
  the winstation records the process that switched to it and the desktop to return to (`input_owner`, `return_desktop`), and
  `winstation_process_killed()` gives the input back when that process dies. `create_desktop`: the names `Winlogon` and
  `Screen-Saver` are reserved for the system role, a desktop named `Winlogon` made by the system role is marked secure,
  and `WINSTA_CREATEDESKTOP` is checked on the window station handle (gated, see below). `cross_desktop_denied()`.
- `server/queue.c`: hardware input (`send_hardware_message`, also `NtUserCallHwndParam(SendHardwareInput)`) is accepted only
  when the sender's desktop, the target window's desktop and the input desktop are one, for every desktop; a host event no
  longer flips the input desktop (**not gated**: Windows behavior, see the work order's "Decisions"). `send_message`
  (which also posts) refuses a receiver on another desktop (gated).
- `server/hook.c`: a thread hook on a thread of another desktop is refused (gated).
- `server/window.c`: `set_window_pos`, `set_window_info`, `set_parent`, `set_window_owner`, `get_window_text` and
  `set_window_text` refuse a window of another desktop (gated; the caller survey is in the work order).
- `server/sysrun.c`, `server/sysrun.h`, `server/process.c`: `sysrun_process_is_system()`, `sysrun_desktops_enforced()` and the hook
  in `process_killed()`.
- `dlls/winex11.drv/secure.c` (new), `x11drv_main.c`, `x11drv.h`, `window.c`, `Makefile.in`: a process on the desktop named
  `Winlogon` (other than `explorer.exe`, the desktop manager) takes a dimmed snapshot of the screen when the driver loads
  and, before its first window, covers the screen with a full-screen override-redirect window and grabs the keyboard and
  the pointer; its windows are not managed. Nothing else changes in the driver.
"Gated" means: only while the launcher-started server has the system role and elevation is enforced (`sysrun_desktops_enforced()`);
otherwise stock behavior. Project programs: `programs/wexelevate` (makes `WinSta0\Winlogon`, switches, policy values) and
`programs/consent` (`/secure`, `/ppid`).

## Elevation boundary: callers use the broker (plan step 7, 2026-10-24)

`ELEVATION_CALLERS_WORKORDER.md`. Every change is active only while the elevation helper is configured (the "not unset" rule of
`kernelbase` and `ntdll`); with it unset each caller behaves as before, byte for byte. Stock Wine files changed:
- `dlls/shell32/shlexec.c`: `run_elevation_helper()` (run the helper program, trust its exit code) is gone. `SHELL_ExecuteW`, with
  the helper configured, sends the `runas` verb (when the caller is not elevated already) and a `CreateProcess` that fails with
  `ERROR_ELEVATION_REQUIRED` to the broker through `execute_with_broker()`: it loads `wexhost.dll` (system directory only), calls
  its export `WexElevateRun` with the resolved image, the full command line, the working directory, the environment and the show
  state, and returns the broker's process handle for `SEE_MASK_NOCLOSEPROCESS`. If `wexhost.dll` or the export is missing the
  launch fails with `ERROR_NOT_SUPPORTED`; nothing falls back to a linked token. An invalid helper value still fails closed as before.
  Error mapping (`ShellExecuteEx`): the broker's Win32 error is the last error; `hInstApp` is `SE_ERR_FNF`, `SE_ERR_PNF`,
  `SE_ERR_SHARE`, `SE_ERR_OOM` for the matching file and memory errors and `SE_ERR_ACCESSDENIED` for everything else
  (cancel 1223, standard user 50, no policy 21, timeout 1460, consent window 1062, changed image 1392).
- `dlls/shell32/tests/shlexec.c` (`test_elevation`): the helper-exit-code cases (approving and declining helper programs) are
  replaced by the new rule: with a helper configured and no broker that approves, `ShellExecuteEx` fails and returns no handle.
- `dlls/msi/custom.c`, `dlls/msi/msipriv.h`: `msi_install_through_broker()` starts `msiexec.exe /i <package> <UI switch> <properties>` once
  through the broker for a per-machine install by a limited administrator and returns the exit code of the elevated `msiexec`
  (cancel becomes `ERROR_INSTALL_USEREXIT`); `msi_package_file_is_machine()` (reads ALLUSERS and the registered context from the package file, read-only) is in `package.c`. `get_admin_token()` (custom action
  server) no longer returns a linked token while the helper is configured: a per-machine install runs wholly in the elevated
  `msiexec`, a per-user install does not elevate.
- `dlls/msi/msi.c` (`MsiInstallProductW`), `dlls/msi/action.c` (`MSI_InstallPackage`): the two call sites. The first runs before the
  package is opened, because opening copies the package into `C:\Windows\Installer`, which a limited token cannot write.
- `programs/msiexec`: not changed.
Project modules: `dlls/wexhost` (`elevate.c`, export `WexElevateRun`, imports `advapi32`), `include/wexhost.h`, `include/wexelevate.h` unchanged.

## In place of stock behavior (deliberate)
| Area | Change | Gate |
|---|---|---|
| `dlls/winewayland.drv` xx-zones-v1 | optional shared per-socket coordinate zone; volatile registry token supports independently launched processes; map/move positioning and feedback with surface scaling and tracked xdg geometry offset | only when compositor advertises xx_zone_manager_v1; upstream/winewayland-zones aed8b9599c + 01607e166c; retained 853c5b02c7 + 1bab2dda87 on codex/wayland-integration; seven placements match X11 exactly at 100% and within 1 px at 150% |
| `dlls/winewayland.drv`; `include/wexwayland.h` | privileged layers, shared-handle cross-process relative stacking, native activation hook and minimized restore; generic scalar role/edge/size properties, no class checks | optional wex_window_manager_v1; ce019d67c7 + header correction 996f87f33f; activation/configure a83412b0ed; maximized work-area borders e3be0174be; shared atoms 97eb62958c; focus acknowledgement 89735ec1ef; background restore/logical stacking 624720e2b6; native focus loss with stale-event guard ea0654efdb; v1 same-connection fallback; v2 compositor 9a47d74 passes 26/26 |
| `dlls/win32u/message.c` | skip implicit click activation of a WS_EX_NOACTIVATE root | generic Win32 style fix 8f1717c8f3; verified private Wayland and X11; upstream baseline incorrectly activated it |
| `programs/explorer/desktop.c`, `explorer.c` | /shell without /desktop starts the plugin on the driver's desktop and skips display mode changes | explicit /shell only; d7d46937a3 included in accepted 05286f7c90; native Wayland shell passes |
| `programs/explorer/desktop.c`, `explorer.c`; `include/wexshell.h` | load the versioned wexshell.dll plugin for the Windows-style shell and route its browser/applet commands; implementation lives in project modules under `src/dlls` (wexshell, wexres, themeui, msgina), while the stock host retains desktop creation, clipboard, COM registration and its message loop | shell startup only when `EnableShell=1` (`HKCU\Software\Wine\Explorer\Desktops\<name>`), named `shell`, or requested with `/shell`; missing/incompatible plugin uses stock behavior |
| `programs/explorer/desktop.c` | `/shell` switch (before the program to run) enables the shell for that desktop; used by the Winlogon-style session script | only when given |
| `programs/explorer/explorer.c` | `/wlan`, `/netstatus`, `/netprops` helper switches; `::{CLSID}` paths and `/e` accepted | token-based, only when given as arguments |
| `dlls/desk.cpl` | theme/background/appearance/settings pages added in front of Wine's Settings page; installed theme discovery, isolated style previews, direct shared themeui Apply, host-supported modes and keep/revert confirmation (see DISPLAY.md); themeui now persists Classic colors before cross-process reload | always |
| `dlls/uxtheme` (`msstyles.c`, `msstyles_v4.c`, `draw.c`, `property.c`, `msstyles.h`) | Binary Vista-format VARIANT parsing, typed properties, WIC PNG decoding, DPI variants and atlas regions; ApplyTheme reads typed SysMetrics | always; reviewed fork through f0108dc; standard-only four-commit upstream candidate c16f0bda39..43c139255b, including fixes; full MIT notice, no unused generated tables; source-built tests pass both architectures |
| `dlls/uxtheme/system.c` | MUI/filename documentation and fabricated scheme display-name fallbacks for installed styles lacking localized strings | fork-only convenience behavior; excluded from the cleaned upstream series and its assertions |
| `dlls/uxtheme` (`system.c`, `msstyles.c`, `metric.c`) | implement private ordinal 16 `OpenThemeDataFromFile`; metrics follow the supplied theme handle; activating a preview applies metrics even when its classes are already parsed | always |
| `dlls/uxtheme/msstyles.c` | convert non-client theme font heights from points to 96-DPI pixels; trim token whitespace before a comma (`MSSTYLES_GetNextToken`) | always; token trimming is an independent upstream candidate |
| `dlls/uxtheme/system.c`, `uxthemedll.h` | watch ThemeManager registry changes and reload the active theme in other processes without reapplying global metrics | theme queries/opening theme data |
| `dlls/winex11.drv/window.c` | virtual desktop resize does not wait for fullscreen hint acknowledgements; standalone shell sessions have no host window manager to supply them | virtual desktop root window only |
| `programs/wmplayer` | Wine's stub `main.c` replaced by ReactOS mplay32 | always |
| `dlls/shdoclc` | icon 190 (Wine's `iexplore.ico`): `iexplore.inf` names it as the Internet Explorer desktop icon, stock Wine has none | always |
| `programs/calc`, `programs/mspaint` | new programs (ReactOS calc; own mspaint) | always |
| `dlls/ntdll/unix/system.c` | `SystemPowerCapabilities.SystemBatteriesPresent` uses Wine's existing `fill_battery_state` helper with zero-initialized state; `SystemBatteryState` and its sysfs implementation are already stock in wine-11.19 | always (replaces the hard-coded FALSE capability; no-battery state remains FALSE) |
| `server/fd.c` | recompute the DST-adjusted timezone bias when DST starts/ends (a long-running session never saw it before) | always (bug fix, upstreamable) |
| `dlls/ntdll/unix/system.c` | drop the cached zone info when `/etc/localtime` changes | always (bug fix) |
| `dlls/kernelbase/locale.c` | preserve a locale selected through Regional and Language Options while the remembered Unix locale remains unchanged; a changed Unix locale becomes the new default | always; upstream suitability requires review in P0 |
| `dlls/user32` (`cursoricon.c`, `user_private.h`, `sysparams.c`, `defwnd.c`) | load system cursor schemes from `HKCU\Control Panel\Cursors` and cache the file-backed cursors, falling back to built-ins; `reset_scheme_cursors()` flushes cached cursor handles on `SPI_SETCURSORS` and `WM_SETTINGCHANGE` for live scheme updates | system cursor resource loads; cache/refresh semantics require review in P0 |
| `dlls/win32u` (`window.c`, `input.c`, `defwnd.c`), `dlls/user32/misc.c` | `RegisterShellHookWindow`/`DeregisterShellHookWindow` implemented: the desktop's taskman window gets the registered `SHELLHOOK` message for window created/destroyed/activated/redraw/flash | always (no cost without a registered window except one server call per event) |
| `dlls/mmdevapi` (`audiovolume.c`, `unixlib.h`), `dlls/winepulse.drv` | `IAudioEndpointVolume`: master volume and mute of the default device through the driver (PulseAudio/PipeWire); two new unix calls `get/set_endpoint_volume`; other drivers (alsa, oss, coreaudio) return E_NOTIMPL; the wow64 table of winepulse has stubs, so 32-bit callers get E_NOTIMPL for now | always |
| `dlls/winex11.drv/window.c` (`sync_window_position`), `event.c` (`set_input_focus`) | in a virtual desktop, a window that is not the top one is restacked in X directly below the window it follows in the Win32 z-order (stock only ever raises, for the sake of window managers), and focusing a window no longer raises it in X; without it an activated window covers topmost ones such as the taskbar | only in a virtual desktop, unmanaged windows |
| `dlls/winex11.drv/event.c` (`X11DRV_ActivateWindow`) | in a virtual desktop the window that becomes active is raised to the top of the Win32 z-order, as Windows does on activation (Wine leaves it to the X window manager, and the row above removed the X raise); the X stacking follows, below topmost windows. Before, clicking a window or its taskbar button activated it without bringing it to the front | only in a virtual desktop, unmanaged windows |
| `dlls/win32u/sysparams.c` (`SPI_SETWORKAREA`/`SPI_GETWORKAREA`) | the work area is the monitor's and every process sees it, like on Windows and ReactOS (stock kept it per process, so windows maximized in other processes covered the taskbar): it is written to the monitor's registry key, the monitor serial is bumped so other processes reload, and a volatile copy in `HKLM\Software\Wine\WorkArea` (by monitor path) is applied again whenever the monitors are rebuilt from the driver, which every process entering a virtual desktop does; it is dropped once it no longer fits the monitor. `SPI_GETWORKAREA` reads the primary monitor every time. As on Windows, the rectangle has to lie on one monitor, and a non-zero uiParam (the taskbar passes TRUE) fits the maximized windows of that monitor to the new work area | always (only matters once something sets a work area) |
| `dlls/shell32` (`shell32.rc`, `shresdef.h`, `shell32.rgs`) | Windows XP names in the namespace: string 8964 "Recycle Bin" (was "Trash", with the delete prompts worded like XP), new string 9227 "My Documents" as the `LocalizedString` of CLSID_MyDocuments (string 46 stays the folder name "Documents") | always |
| `dlls/shell32/shell32.rc` | icon 32 (`IDI_SHELL_EMPTY_RECYCLE_BIN`, Wine's own `trash_file.ico`): stock Wine defines the id and returns it for `SIID_RECYCLER`, but has no such icon, so an empty Recycle Bin was drawn blank | always (bug fix, upstreamable) |
| `dlls/shell32/iconcache.c`, `shellord.c` (`FileIconInit`) | shell32's own icons can be replaced through `HKLM\...\Explorer\Shell Icons` like on Windows and ReactOS (stock Wine only did it for overlays): value `n` replaces icon index n, an icon asked for by resource id stands for index id - 1 for the first 54 icons (no gaps there), and beyond that a value named after the negative id (`-137`) is used, which Windows has no use for. A replacement that cannot be loaded falls back to shell32's icon. WineExplorer addition: the icons of a module in the system directory (Control Panel applets such as `desk.cpl`) are taken from the file named by the value of that module's name (no extension) in `HKCU\Software\WineExplorer\Module Icons`, with the module's own icon ids; other modules are never replaced. `FileIconInit(TRUE)` (was a stub) reloads the cached icons of shell32 and of system modules in place so a theme change shows without new image indexes. Explorer writes both keys from the theme's icon pack (`src/dlls/wexres/shellres.c`) | always (only matters when Shell Icons or Module Icons has values) |
| `server/winstation.c` (`remove_desktop_user`), `include/ntuser.h` | new Wine desktop flag `DF_WINE_SHELL_DESKTOP`: a desktop with it is not timed out when only its owner is left (stock wineserver unlinks the desktop's name and posts `WM_CLOSE` then; with a shell that happened whenever the shell was alone and one of its threads ended, and new programs could no longer open the desktop). Explorer sets it in shell mode | only on desktops that have the flag |
| `dlls/shell32/folders.c` (`IExtractIconW::GetIconLocation`) | a shortcut without an icon of its own shows the icon of its target, as on Windows and ReactOS (stock Wine showed the blank file icon, e.g. for the Start menu shortcuts in search results); shortcut targets use the default fallback to prevent recursive icon lookup through cycles | always (bug fix, upstreamable) |
| `dlls/comctl32/propsheet.c` (`PROPSHEET_SetWizButtons`) | when the focused wizard button ends up disabled or hidden (Next swapped for Finish), the focus moves to the default button; before, nothing had the focus on a wizard's last page and Enter did nothing | always (bug fix, upstreamable) |
| `dlls/comctl32/imagelist.c` (`add_dib_bits`) | an image without alpha clears the item's alpha flag; before, replacing a 32-bit image with one without alpha (`ImageList_ReplaceIcon`) kept the flag and the new image drew fully transparent | always (bug fix, upstreamable) |
| `dlls/shell32/shellord.c` (`SHFindFiles`) | opens the Search Results folder (`explorer.exe ::{E17D4FC0-...}`, whose window shows the Search Companion) like Windows and ReactOS, honoring `REST_NOFIND`; was a stub returning FALSE. The folder to look in and saved searches are not passed yet | always |
| `dlls/user32/user32.rc` (`SYSMENU`) | the window menu ends with Close, as on Windows: Wine's "About Wine" item and its separator are removed (the `SC_ABOUTWINE` handler stays, unused) | always |
| `dlls/shell32` (`shlview_cmenu.c`, `new_menu.c`, `shlfileop.c`, `shlview.c`, `shell32.rc`) | Windows XP folder background menu (View, Arrange Icons By, Refresh, Paste, Paste Shortcut, New, Properties); New lists Folder and the ShellNew file types and starts renaming the new item; `FOF_RENAMEONCOLLISION` names copies "Copy of X"; Create Shortcut / Paste Shortcut implemented; items added by path notifications are read again for sizes and dates; notifications for items outside the folder (they are recursive) are ignored and a move in or out shows as an add or a removal; the background menu takes the "pastelink" verb | always |
| `dlls/shell32` (`shlfolder.c` `SHELL32_BindToChild`, `folders.c`) | folder shortcuts work as on Windows: a folder whose desktop.ini names `CLSID_FolderShortcut` opens the folder its `target.lnk` points to; a desktop.ini `IconFile` is used (it was read and dropped) | always |
| `dlls/shell32` (`shfldr_fs.c` display names, `shlview_cmenu.c` Send To) | a folder is shown by its desktop.ini `LocalizedResourceName` (not for parsing or editing), as Windows shows "Documents" by another name; the item menu has Send To (Desktop (create shortcut), My Documents, SendTo-folder shortcuts, removable drives) | always |
| `dlls/shell32` (`brsfolder.c`) | SHBrowseForFolder returns the target of a folder shortcut unless `BIF_NOTRANSLATETARGETS`, as Windows does | always |
| `dlls/shell32` (`shlview.c` keys, `shlfileop.c`, `shell32.rc` confirmations) | Ctrl+A, Ctrl+C, Ctrl+X and Ctrl+V in the shell view; the delete confirmations use the XP captions (Confirm File Delete, Confirm Folder Delete, Confirm Multiple File Delete, Confirm File Replace) and name the item by its display name | always |
| `dlls/shell32` (`shfldr_mycomp.c`, `shell32.rc`) | drives without a label are named "Local Disk (C:)", "CD Drive (D:)" and so on, with that Type; columns "Total Size" and "Free Space"; `HideMyComputerIcons` (set for Control Panel in `wine.inf`) hides an item from the My Computer window but not from folder trees | always |
| `dlls/shell32` (`pidl.c`, `shell32_main.c`) | file system folders have the type "File Folder" (Windows wording) | always |
| `dlls/shell32` (`shell32.rc`, `shresdef.h`) | string 21785 "Shared Documents" (the Windows XP id), the LocalizedResourceName Explorer gives Public\Documents | always |
| `dlls/shell32` (`shell32.rc`, `shresdef.h`) | strings 0x7143-0x7145 "Shared Music", "Shared Video", "Shared Pictures" (XP ids), the names Explorer gives Public Music, Videos and Pictures | always |
| `dlls/shell32` (`undo.c`, new; `shlfileop.c`, `shfldr_fs.c`, `recyclebin.c`, `shlview.c`, `shlview_cmenu.c`) | Undo as on XP: SHFileOperation with FOF_ALLOWUNDO and renames in the view record their items; the background menu shows "Undo Rename/Move/Copy/Delete" (verb "undo", Ctrl+Z in the view); undoing a delete restores the item from the trash. The view finds notified items by name, resets edited labels and appends notified items at the end | always |
| `dlls/comctl32` (`listview.c`, also built as comctl32_v6) | LV_VIEW_TILE (v6 only): LVM_SET/GETTILEVIEWINFO (tile size, lines), LVM_SET/GETTILEINFO (per item columns); a tile has the normal icon at the left and the name with its detail lines (gray) next to it, laid out like the small icon view | always |
| `dlls/comctl32` (`listview.c`) | groups (v6) in the icon, small icon and tile views: LVM_ENABLEGROUPVIEW, LVM_INSERTGROUP, LVM_REMOVE(ALL)GROUP(S), LVM_HASGROUP, LVM_SET/GETGROUPINFO, LVM_MOVEITEMTOGROUP, LVIF_GROUPID; each group is laid out in rows under an XP header (blue bold name over a fading line). Not in the details view | always |
| `dlls/comctl32` (`listview.c`) | type-ahead: a space continues a search in progress ("my p" finds My Pictures) | always |
| `dlls/comctl32` (`listview.c`) | LVM_SETBKIMAGE offsets: the image stays at its place in the window (Explorer's folder watermarks in the bottom right corner) and the list draws it again when it scrolls or is resized | always |
| `dlls/comctl32` (`listview.c`) | a selected item keeps its highlight box when the text background is CLR_NONE, as on Windows | always |
| `dlls/shell32` (`shlview.c`, `shell32.rc`, `shell32.rgs`, `shfldr_fs.c`, `shfldr_mycomp.c`, `shellpath.c`, `shell32_classes.idl`) | My Computer lists XP's Files Stored on This Computer (Shared Documents, a new namespace folder with XP's CLSID {59031A47-...}, and My Documents named "<user>'s Documents"), then its drives in XP's groups: Hard Disk Drives, Devices with Removable Storage, Network Drives, Other; drives and folders have no detail lines in Tiles | always |
| `dlls/shell32` (`shlview_cmenu.c`, `shell32.rc`), `dlls/comctl32` (`comctl32.rc`) | file Properties as XP: "test Properties" title (comctl32's PSH_PROPTITLE caption "%s Properties"), Type of file, Opens with (registered program name) and Change... (Open With, always), Size and Size on disk with exact bytes, XP date format, Read-only; the page keeps its icons | always |
| `dlls/shell32` (`shfldr_fs.c`, `changenotify.c`) | drag and drop: a folder item is its own drop target (files take none); Drop moves, copies or makes shortcuts (CF_HDROP from the shell or other programs) with XP's default effect (move on the same drive, else copy; Ctrl copies, Shift moves, Ctrl+Shift links), undoable; change notifications of a rename or move reach the watchers of either item's folder | always |
| `dlls/shell32` (`shlview.c`) | the view watches its folder on disk (FindFirstChangeNotification) and takes the folder's items again when other programs change it: new items at the end, gone ones removed, the selection kept | always |
| `dlls/shell32` (`new_menu.c`), `loader/wine.inf.in` | New > Shortcut: ShellNew Command handlers (HKCR\.lnk\ShellNew "rundll32.exe appwiz.cpl,NewLinkHere %1") come after Folder, before the separator | always |
| `dlls/appwiz.cpl` (`createlink.c`, new; `appwiz.rc`, `appwiz.cpl.spec`, `shortcut.bmp`) | NewLinkHere(A/W): Windows XP's Create Shortcut wizard (location with Browse..., then the name; .lnk for items, .url for Internet addresses); the picture is XP's from the resource pack, else ReactOS' | always |
| `dlls/shell32` (`shlview.c`, `shell32.rc`) | View > Tiles: 48 pixel icons (SHIL_EXTRALARGE) with the type and size of files (none for folders) under the name; icons and tiles are laid out in rows (no LVS_ALIGNLEFT), as on XP | always |
| `dlls/shell32` (`shlview.c`, `shell32.rc`) | View > Thumbnails: 96 pixel framed pictures of images (GDI+), the 48 pixel icon for other items, folders as XP's large folder (the Shell Icons folder icon) holding up to four of their pictures; the list is laid out again after it is filled; a selected thumbnail keeps its picture and gets XP's thick frame of the selection color | always |
| `dlls/shell32` (`shlview.c`, `shell32.rc`) | View > Filmstrip (FVM_THUMBSTRIP) in picture folders: the selected picture large above one row of thumbnails, with previous/next and rotate buttons; rotating saves the picture in its own format (GDI+) | always |
| `dlls/shell32` (`openwith.c`, new; `dialogs.c`, `shlexec.c` stubs removed) | Open With dialog (`SHOpenWithDialog`, `OpenAs_RunDLL`), ReactOS-based: programs from `HKCR\Applications` (FriendlyAppName or version FileDescription), recommended from the type's open command, OpenWithList and OpenWithProgids; "Always use" sets the type's default. The item menu of one file has Open With ▸ (or Open With... when nothing else is recommended) | always |
| `loader/wine.inf.in` (file types) | `Folder\ShellNew`, Bitmap Image (`.bmp` → `Paint.Picture`, opened by mspaint) and Rich Text ShellNew entries; `HideFileExt=1` (no-clobber) as on Windows XP | prefix install/update |
| `programs/iexplore/iexplore.inf` | CLSID_Internet gets its display name "Internet Explorer" (Windows' default value); Wine had none | always |
| `loader/wine.inf.in` | file-type associations (.txt/.bat/.cmd/.log, folder open command); `HKCR\Applications` Notepad, Paint, WordPad and OpenWithList entries for .txt/.bmp/.rtf; the Windows icons of text, configuration, setup and batch files (shell32 -152, -151, -153) | always |
| `programs/control/control.c` | `printers` opens the Printers folder; no argument opens the Control Panel folder in Explorer like Windows (Wine's own Control Panel window only if that fails) | always |
| `programs/shell32`-visible: none | Run dialog placement is done by explorer (CBT hook), not by shell32 | - |
| `loader/wine.inf.in` | `.msc` files open with `mmc.exe` (`MSCFile`); new `[MscFiles]` copies `devmgmt.msc` (a data file of `programs/mmc`) into system32 | always |
| `dlls/ntdll` (`unixlib.h`, `loader.c`, `ntdll.spec`, `unix/unix_private.h`, `unix/loader.c`, `unix/file.c`) | `wine_create_symlink` internal unix call with target canonicalization (`realpath`) to support fake DLL symlinks; client-side copy-on-write symlink break (`check_cow_break_symlink`) in `NtCreateFile` gated on Wine DLL directories (`is_in_wine_dll_dirs`) resolving relative symlink targets against the link directory for data writes, truncate, overwrite, or supersede, and adding `S_IWGRP` to the replacement file mode when `S_IWUSR` is set on the target so POSIX default ACLs in shared machine roots retain group write access | internal call / writes to built-in DLL symlinks |
| `dlls/setupapi` (`fakedll.c`) | symlinked fake DLL generation replacing copied fake DLLs with symlinks to Wine's installation tree while preserving native DLLs and WinSxS assemblies; WOW64 path redirection in `get_real_link_dest()` | `WINELINKFAKEDLLS=1` env var or `HKLM\Software\Wine\LinkFakeDlls` (DWORD 1) |
| `server/fd.c` | leaf symlink preservation in `dup_fd_name` / `open_fd()`: ensures `fd->unix_name` retains the leaf symbolic link when opening a symlink, so closing with `FILE_DELETE_ON_CLOSE` unlinks the link rather than the package target | symlinks opened with delete-on-close |
| `loader/wine.inf.in`, `programs/wineboot/wineboot.c` | `[UserInstall]` section (`UserRegisterDlls` for `shell32.dll`, `UserRegistry` for 19 per-user `HKCU` settings, and `ThemeManager`) in `wine.inf.in`; when `C:\windows` is a reparse point (`FILE_ATTRIBUTE_REPARSE_POINT`) pointing to a shared machine root, `update_wineprefix()` runs only `[UserInstall]` and `update_user_profile()` so per-user `wineboot --init` / `wineboot -u` skips machine-level DLL creation and `HKLM` device installation | only when `C:\windows` has `FILE_ATTRIBUTE_REPARSE_POINT` |
| `server/registry.c` | Phase S3 shared machine hive handling: (1) `save_branch()` checks target write permissions (`EACCES`/`EROFS`), clearing dirty state, resetting `mod_seq` and freeing tombstones, returning success without modifying the file or replacing symlinks (Option a1 in-memory writes for limited users); (2) saves to temporary `reg<pid><count>.tmp` in target hive's directory, fsyncs, and executes atomic `rename()` over realpath target; (3) Administrator merge-on-save under `flock(LOCK_EX)` on `<target_dir>/.reg.lock` replaying session dirty keys/values (`mod_seq > 0`) and tombstones (`reg_tombstone` deleted nodes) onto on-disk hive base loaded into `tmp_root` without resurrecting deleted keys; (4) resets branch `mod_seq` and frees tombstones after each successful save so past mutations are not replayed indefinitely; (5) limits per-session filtering and merge strictly to branches loaded as symbolic links into the shared machine root (`info->is_shared_machine`), preserving stock Wine behavior for ordinary unlinked prefixes; (6) limits `OBJ_OPENLINK` in `create_key_recursive()` strictly to merge operations (`in_merge`) | shared machine registry hives (`system.reg`, `userdef.reg`) |
| `dlls/advapi32/security.c` (`SetNamedSecurityInfoW`, `GetNamedSecurityInfoW`), `dlls/advapi32/tests/security.c` | an object is opened with `WRITE_OWNER` (set) or `READ_CONTROL` (get) also when only `LABEL_SECURITY_INFORMATION` is asked for, as Windows does; stock Wine asked for those rights only for the owner, group and DACL, so setting a label failed with `ERROR_ACCESS_DENIED` (the server's `set_security_object` needs `WRITE_OWNER` for a label). Chromium's AppContainer profile creation calls `SetNamedSecurityInfoW(dir, SE_FILE_OBJECT, DACL \| LABEL \| UNPROTECTED_DACL)` on its profile directory; in a prefix without the AppContainer registration that failed, Chromium rolled the profile back and restarted the network service without its sandbox, and the fixture page never reached the network stack. New conformance test `test_named_security_info_label` fails without the change (x86_64 and i386); Wine keeps no label on files, so the read-back is `todo_wine`; see `docs/CHROMIUM_APPCONTAINER_LABEL_20261101.md` | always (bug fix, upstreamable; upstream master has the same conditions) |

## D2 compositor effects and capture

- `dlls/winex11.drv/window.c`, `x11drv.h`: a private `_WEX_DROPSHADOW` CARDINAL on class-shadow windows carries
  `SPI_GETDROPSHADOW` to the compositor at creation/show. Explorer's out-of-context hook cannot reliably read another
  process's XID. The hint is only written when `_NET_WM_CM_S<screen>` has an owner; no input/shape/rendering changes
  without one. The shared internal `X11DRV_has_compositor` queries ownership on the actual display's default screen.
- `dlls/user32/win.c`: `AnimateWindow(AW_BLEND)` steps temporary `WS_EX_LAYERED` uniform alpha at roughly 16 ms
  intervals for `dwTime`, then restores the layered bit/attributes and the requested visibility. As allowed by the
  D2 spec this path runs without an X11 selection query in user32; intermediate alpha is visible only with a
  compositor (Wine's X11 driver only publishes opacity otherwise). Other animation flags retain the partial-stub
  behavior. Existing per-pixel layered windows fall back to immediate show/hide to preserve their bitmap; uniform
  alpha and color-key layered windows retain their attributes. Automatic popup menu fades are deferred: win32u's
  `show_popup` cannot call user32's `AnimateWindow`, and a shared fade/compositor gate would broaden the stock change.
- `dlls/winex11.drv/bitblt.c`: direct `X11DRV_StretchBlt` source copies and `X11DRV_GetImage` readback substitute the
  real X root for a virtual-desktop source drawable and add its root offset to the source DC rectangle. Gate:
  source equals Wine's `root_window`, that window differs from `DefaultRootWindow`, and `_NET_WM_CM_S<screen>` has
  an owner at read time. The DC is copied locally; destination rendering/GCs and ordinary window reads stay as before.
  `dlls/winex11.drv/xrender.c`: under the same gate, scaling/format conversions fall back to that core path (and GDI
  image readback if needed), avoiding a cached desktop picture that lacks redirected children. Ownership is queried
  per read, so already-open screen DCs recover immediately after composition is disabled.

## Backported from upstream Wine (drop when rebasing past them)

| Upstream | Change |
|---|---|
| MR !12231 (merged 2026-10-08) | `dlls/user32/lstr.c`: `CharToOemBuffW` wrote more bytes than asked for with a multi-byte OEM code page (UTF-8); untranslatable characters become `_` |
| MR !7746 (open upstream when taken) | `dlls/shell32/shfldr_fs.c`, `shfldr_desktop.c`: sizes of files of 4 GiB and more (the pidl keeps only 32 bits); checked in Explorer's Details view |
| `3de138d3d3` (wine-mirror/wine, 2026-10-29) | `dlls/ntdll/unix/virtual.c`: `map_anon_area()` helper; the first thread's data block is allocated aligned to the 64 KB signal stack size (`08889af3` left it page-aligned, so the signal handlers of the main thread read a wrong `thread_data` and a suspended thread never delivered its context; Chromium's sampler then hung); see `docs/CHROMIUM_FIRST_THREAD_DATA_ALIGNMENT_20261101.md` |
| `9f4469dbfb` (wine-mirror/wine, 2026-10-29) | `dlls/wow64/wow64_private.h`: `token_user_32to64()`, `token_owner_32to64()`, `token_primary_group_32to64()` and `token_default_dacl_32to64()` return NULL for a NULL 32-bit pointer; these are optional `NtCreateToken` parameters, and a 32-bit caller that left one out crashed in wow64 (the i386 `test_NtCreateToken` cases failed with `STATUS_ACCESS_VIOLATION`) |

## Reverted (were changed earlier, now stock again)
- `dlls/comctl32/monthcal.c` (today-circle removal): timedate.cpl now creates its calendar with
  `MCS_NOTODAY | MCS_NOTODAYCIRCLE`, which is what the XP applet looks like.
- `include/commctrl.h`, `include/prsht.h` (`SNDMSG` redefinition).
- `dlls/ntdll/unix/sync.c` (`NtSetSystemTime` running `sudo date`): clock changes go through the applet's elevation
  prompt only.
- `dlls/shell32/dialogs.c` (Run dialog placement).

## Token Security, Elevation & Chromium Compatibility (gemini/token-security)
- `include/winnt.h`: Added definition for `SECURITY_CAPABILITIES`.
- `dlls/kernelbase/process.c`: Added attribute length check for `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES`; zeroed output buffer in `GetProcessMitigationPolicy` stub.
- `server/protocol.def`:
  - Extended `filter_token` with `disable_sids_size` and VARARG `restrict_sids`.
  - Added request `create_lowbox_token` with `package_sid` and `capabilities`.
  - Extended `get_token_groups` with `which_groups` selector (0=groups, 1=restricted SIDs).
  - Added request `set_token_integrity_level`.
  - Extended `get_token_info` reply with `integrity_rid`, `is_app_container`, `is_restricted`.
- `server/token.c`:
  - Added `integrity_rid`, `restricted_sids`, `app_container`, and `capabilities` to `struct token`.
  - Implemented token filtering flags `DISABLE_MAX_PRIVILEGE`, `LUA_TOKEN`, and restricting SIDs storage.
  - Implemented `create_lowbox_token` handler setting AppContainer SID, Low integrity (`0x1000`), and capabilities.
  - Implemented `set_token_integrity_level` handler for setting token integrity labels.
  - Implemented restricting SID intersection check in `token_access_check`.
  - Implemented Mandatory Integrity Check (`SYSTEM_MANDATORY_LABEL_NO_WRITE_UP`, `NO_READ_UP`, `NO_EXECUTE_UP`) in `token_access_check` and `check_object_access`.
- `dlls/ntdll/unix/security.c`:
  - Implemented `NtCreateLowBoxToken` wineserver call.
  - Extended `NtFilterToken` passing `restrict_sids` and `disable_sids_size`.
  - Implemented `NtSetInformationToken(TokenIntegrityLevel)`.
  - Implemented `NtQueryInformationToken` for `TokenIntegrityLevel`, `TokenAppContainerSid`, `TokenIsAppContainer`, `TokenIsRestricted`, `TokenHasRestrictions`, and `TokenRestrictedSids`.
- `dlls/wow64/security.c`: Added WOW64 thunks for `NtCreateLowBoxToken` and 32-bit queries for `TokenIsRestricted`, `TokenHasRestrictions`, `TokenRestrictedSids`, and `TokenAppContainerSid`.
- `dlls/kernelbase/process.c`:
  - Added `is_elevation_required(HANDLE token, const WCHAR *filename)`: checks PE manifest runlevel (`RunlevelInformationInActivationContext`). If `requireAdministrator` (or `highestAvailable` on limited token) and elevation helper is configured in `HKLM\Software\Wine\Elevation\Helper`, `CreateProcessInternalW` fails with `STATUS_ELEVATION_REQUIRED` (`ERROR_ELEVATION_REQUIRED` / 740).
- `dlls/ntdll/loader.c`:
  - Added `is_elevation_helper_configured()`: prevents silent in-process token elevation (`elevate_token()`) when helper is configured, maintaining stock behavior when unconfigured.
- `dlls/shell32/shlexec.c`:
  - Added `get_elevation_helper()` and `run_elevation_helper()`: invokes configured helper on verb `"runas"` or upon `ERROR_ELEVATION_REQUIRED`.
  - On helper approval (exit 0), spawns elevated process with `CreateProcessAsUserW(get_admin_token(), ...)` and returns genuine `hProcess`.
  - On helper refusal or cancellation (exit non-zero), sets `GetLastError() = ERROR_CANCELLED` (1223) and returns `SE_ERR_ACCESSDENIED`.
  - Handles working directory normalization, quoted helpers with arguments, and token closing safely.
- `dlls/shell32/tests/shlexec.c`:
  - Added `test_elevation()`: tests stock unprompted behavior, direct `CreateProcess` 740 failure with `requireAdministrator` manifest, `ShellExecuteEx` approval with genuine `hProcess`, `runas` approval, `ShellExecuteEx` cancellation with `ERROR_CANCELLED` (1223) and `hProcess == NULL`, and registry rollback.
- `programs/consent/`:
  - User Account Control Consent Dialog (`consent.exe`), resource script, 4-quadrant golden shield graphics, Authenticode verification, consent and credentials modes with automated test overrides (`WINE_CONSENT_AUTO_APPROVE` / `WINE_CONSENT_AUTO_DENY`).
- `include/winnt.h`:
  - Added `CLAIM_SECURITY_ATTRIBUTES_INFORMATION` structure definition for token security attribute queries.
- `dlls/ntdll/unix/security.c`:
  - Implemented `TokenUserClaimAttributes` (33) and `TokenSecurityAttributes` (39) returning valid empty claim blocks (`Version = 1, AttributeCount = 0`). Updated `info_len` table.
- `dlls/wow64/security.c`:
  - Implemented 32-bit thunking in `wow64_NtQueryInformationToken` for `TokenUserClaimAttributes` and `TokenSecurityAttributes` (12-byte layout).
- `dlls/advapi32/security.c`:
  - Handled `REVOKE_ACCESS` in `SetEntriesInAclW` first pass to cleanly remove matching ACEs without error.
- `dlls/advapi32/tests/security.c`:
  - Added unit tests `test_claim_security_attributes` and `test_revoke_access_acl` (3,665 tests passing, 0 failures).
- `dlls/kernelbase/process.c`:
  - Supported `PROC_THREAD_ATTRIBUTE_COMPONENT_FILTER` (26), `PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY` (15), `PROC_THREAD_ATTRIBUTE_DESKTOP_APP_POLICY` (18), `PROC_THREAD_ATTRIBUTE_MITIGATION_AUDIT_POLICY` (24), and `PROC_THREAD_ATTRIBUTE_WIN32K_FILTER` (16) in `validate_proc_thread_attribute` and `CreateProcessInternalW`.
  - Silenced `GetProcessMitigationPolicy` stub logging to TRACE.
- `server/protocol.def`:
  - Added `@REQ(get_process_handle_table)` with `count` reply field.
  - Added `@REQ(get_thread_page_priority)` and `@REQ(set_thread_page_priority)`.
- `server/handle.c`:
  - Implemented `DECL_HANDLER(get_process_handle_table)`: queries handle table, validates buffer capacity, sets `STATUS_INFO_LENGTH_MISMATCH` with required length on undersized buffers.
- `server/thread.h`, `server/thread.c`:
  - Added `page_priority` in `struct thread` (default 5).
  - Implemented `DECL_HANDLER(get_thread_page_priority)` and `DECL_HANDLER(set_thread_page_priority)` enforcing priority range 0..5 and `THREAD_SET_LIMITED_INFORMATION` access checks.
- `server/token.c`:
  - Implemented two-pass DACL evaluation in `token_access_check`: Pass 1 checks user and group SIDs; Pass 2 checks restricting SIDs in DACL order, honoring `ACCESS_DENIED_ACE_TYPE` before allow ACEs.
  - Implemented strict intersection semantics in `filter_token`: intersecting existing restrictions with new restrictions, retaining `null_sid` (`S-1-0-0`) on disjoint sets.
  - Wired LowBox AppContainer package and capability SID checks in `token_sid_present`.
- `dlls/kernelbase/process.c`:
  - Wired `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES` in `CreateProcessInternalW` to create lowbox tokens via `NtCreateLowBoxToken` and assign them to child processes.
- `dlls/ntdll/unix/process.c`:
  - Handled `ProcessHandleTable` (58) in `NtQueryInformationProcess`, querying `server_get_process_handle_table` and setting required buffer length on `STATUS_INFO_LENGTH_MISMATCH`.
- `dlls/ntdll/unix/thread.c`:
  - Implemented `ThreadPagePriority` (24) query and set in `NtQueryInformationThread` and `NtSetInformationThread` via wineserver requests.
- `dlls/advapi32/tests/security.c`:
  - Added unit tests for Case C deny on restricting SIDs in `test_restricted_token_access_check`, and `test_restricted_sid_intersection` (3,692 tests passing, 0 failures on x86_64 and WOW64).
- `dlls/ntdll/tests/info.c`:
  - Enhanced unit tests `test_process_handle_table` and `test_thread_page_priority` (2,798 64-bit and 2,729 WOW64 tests passing, 0 failures).
- `dlls/shell32/shlexec.c`:
  - Added 30-second bounded wait in `run_elevation_helper` with `TerminateProcess(pi.hProcess, 1223)` on timeout.
- `programs/consent/consent.c`:
  - Removed `WINE_CONSENT_AUTO_APPROVE` and `WINE_CONSENT_AUTO_DENY` environment bypasses.
  - Implemented genuine PAM shadow password verification via `/sbin/unix_chkpwd <username> nullok`.
    (Historical, removed: consent.exe no longer checks passwords; see docs/ELEVATION.md. The
    current path is the S7 PAM worker, which calls `pam_authenticate` with
    `PAM_DISALLOW_NULL_AUTHTOK`, so an account without a password never authenticates.)
  - Added root-only opt-in fixture `/etc/wineexplorer/test-elevation.conf`.
- `dlls/shell32/shell32_main.c`, `shellpath.c`, `shell32_main.h` (always; upstreamable): `DllInstall(TRUE, L"U")` (was a stub) does the per-user setup, as Windows' Active Setup does at first logon (`regsvr32 /s /n /i:U shell32.dll`): `_SHRegisterUserShellFolders(FALSE)` (HKCU, creates the user's folders) and `create_extra_folders(TRUE)` (TEMP/TMP, Temp, Themes, AccountPictures, Screenshots; not the COMMON_APPDATA step). It writes no HKLM, so it works when the elevation helper denies machine writes, where `DllRegisterServer` stops at its first HKLM write and never creates the folders. `DllRegisterServer` and other `DllInstall` arguments are unchanged.

## License headers

`wexhost` now also provides `WexAppsList` / `WexAppsLaunch` through runtime-loaded GIO. `apps.c` handles freedesktop discovery and launching with the shell's Wine variables removed; both Start menu styles expose the entries. The X11 WM checks its running state under the connection lock so requests arriving during shutdown cannot use the closed connection. These are project additions, not changes to Proton or a shared prefix.

Native focus adds `WexWmFocus`, current-focus validation for queued X events, and foreground-event synchronization. Explorer attaches the old foreground thread to its own input queue so Wine's detach operation does not recreate Explorer's queue and discard its active window/input time. Alt+Tab defers completion until after the keyboard hook. These fixes stay in Explorer and `wexhost`; the diagnostic X11-driver experiments were removed.

Rounded captions add `user_api_hook.pSetWindowRgn`, with the default user32 callback forwarding to `NtUserSetWindowRgn` and uxtheme tracking explicit application regions. The theme renderer combines the caption's transparent background region with its body, avoids recursive region repainting, and clears only its own region when styles are disabled. Start menu bitmap callbacks and its theme-derived panel shape stay in Explorer; ordinary application menu bitmaps retain upstream Wine behavior. See [THEMES.md](THEMES.md) for verification and references.
The project is GPL-2.0-or-later as a whole (see `LICENSE.md`). New project files carry the GPL-2.0-or-later header (`scripts/add-license.py` adds it to anything missing it); older project files keep their LGPL-2.1-or-later header, which is compatible.
Modified Wine files keep their original header. ReactOS-derived programs keep theirs:
- `programs/calc` - ReactOS Calc, LGPL-2.1+ (header in each file); `copying.txt` is the GPL text shipped with it.
- `programs/wmplayer` - ReactOS mplay32 (ReactOS license: GPL-2.0-or-later).
- `programs/explorer/alttab.c` - ReactOS-derived Alt+Tab implementation; retains its GPL-2.0-or-later header and attribution.
- `dlls/ntoskrnl.exe` - stock Wine.

## Themed non-client frame (visual phase)
- `include/ntuser.h`, `include/winuser.h`: new `DrawNonClientFrame` user32 callback, `struct draw_non_client_frame_params`,
  `user_api_hook.pNonClientFrameDraw` and `NCFRAME_*` flags.
- `dlls/win32u/defwnd.c`: `nc_paint` and the caption-button redraws ask the hook first; if it draws, only the rectangle is advanced.
- `dlls/user32/{user_main.c,hook.c,nonclient.c,user_private.h}`, `dlls/wow64win/user.c`: callback plumbing and default (declines).
- `dlls/uxtheme/{window.c,system.c,uxthemedll.h}`: `UXTHEME_NonClientFrameDraw` draws `Window` caption, frame and buttons from the active msstyles.
- `dlls/uxtheme/window.c`: the themed caption title takes its colour and shadow from the restored caption part (`WP_CAPTION`/`WP_SMALLCAPTION`) when the maximized part lacks those properties; the open style defines them only on the restored part, so a maximized title was drawn white on white.
- `dlls/uxtheme/{window.c,system.c,uxthemedll.h}`: the themed frame draws classic when the application cleared `STAP_ALLOW_NONCLIENT` (`SetThemeAppProperties`) and opens its theme through `UXTHEME_OpenNonClientTheme`; `OpenThemeData` returns no theme when `STAP_ALLOW_CONTROLS` is cleared, as on Windows (stock Wine ignored both; the `OpenThemeData` part is upstreamable). Used by GUI-mode setup for XP's classic look.

## Explorer window (file browser)
- `dlls/shell32/shell32.rc`: shell view column titles "File"/"Modified" are now "Name"/"Date Modified" (Windows wording).
- `programs/explorer/explorer.c`: window class is `CabinetWClass`; the window chrome moved to `browser_ui.c` (menu bar, Standard
  Buttons, Address bar with Go, activity logo, status bar) and `browser_taskpane.c` (File and Folder Tasks / Other Places /
  Details, drawn with the ExplorerBar class).  Toolbar and logo pictures come from ReactOS browseui (`res_browser/`).

## Menu accelerator underlines (Windows XP behavior)

* `dlls/user32/text.c`: `DrawText` honors `DT_HIDEPREFIX` (the `&` is removed, the letter is not underlined).
* `dlls/win32u/menu.c`: menus hide the accelerator underlines until a key is used while the menu is open (or
  `SPI_GETKEYBOARDCUES` is set), like Windows XP; menu items with a 32 pixel picture are exactly as high as the
  picture (the classic Start menu).

| `dlls/wexhost` (`unixlib.c`), `include/wexhost.h` | WEX_POWER_LOGOFF (login1 TerminateSession of our session) and WEX_POWER_SWITCHUSER (GDM CreateTransientDisplay) | always |

## Chromium token reconciliation candidate (2026-10-31, unaccepted)

Wine topic `f2e67abe` changes only `server/token.c`, above `ce87bac0`:

- AppContainer SID matching recognizes AllApplicationPackages.
- The generated token default DACL uses the actual user SID, not Domain Users.
- The NULL-descriptor `token_duplicate` branch copies the source object's
  whole owner/group/DACL/SACL. This part conflicts with upstream
  `test_token_security_descriptor` and the native default-descriptor contract;
  Wine `e1a90b94` removes this candidate behavior, restoring `token_duplicate`
  exactly to `ce87bac0`, including independent source integrity-label
  propagation whether or not an explicit descriptor is supplied.

These are topic candidates, not a production Wine pin or a confirmed Chromium
fix. After guest recovery, the corrected Mojo probe passes all three modes
at mask `0x88` on x64 and x86 with the candidate; the baseline still fails
both sandboxed modes. See
[the continuation and raw evidence](CHROMIUM_MOJO_0X88_CONTINUATION_20261031.md).
The native descriptor oracle further distinguishes the token's integrity
state from an explicitly supplied object label; the baseline rollback is not
full descriptor conformance.

The subsequent public-duplication correction changes
`server/{token.c,security.h,process.c,named_pipe.c}`: an explicit public/internal
flag limits creator-default descriptor initialization to public token
duplication. An explicit object mandatory label is preserved; an omitted
label derives from source token integrity state rather than the source object
label. Omitted object defaults no longer leak the source token default DACL.
Allocation and descriptor-setting failures release the duplicate and propagate
failure. Internal filter, lowbox, process and named-pipe callers retain the
prior label behavior; no access-check exemptions or restrictions are removed.

Matched guest x64/x86 each pass all 114 named native-backed descriptor
assertions and all three Mojo modes with server SHA-256
`196f24db0bfd402bc5656dc6be09a9d5baf82f863eeedd9596f9f3f78160a800`.
This establishes the focused public contract, not every effective-creator
context or internal assignment contract, nor actual Chromium/Edge acceptance.

## Temporary opt-in pipe diagnostics (2026-10-31)

Wine `2ab3949a` adds the `wex_mojo` channel in
`dlls/kernelbase/{file.c,sync.c}`. It records named-pipe creation and pipe-only
file opens, parameters, returned NT status and defined output handles. It
changes no API result, descriptor, access mask or policy; the prefix test is
disabled unless the channel is enabled. This avoids the full file/sync
trace's measured startup log-quota overrun. Both kernelbase architectures
build without new warnings and the instrumented guest passes the six
x64/x86 startup, Mojo and descriptor programs. This is diagnostic code, not
a browser connection fix; the r12 observation stopped at an earlier
pre-logging boundary and does not establish Chromium or Edge acceptance.

## Temporary opt-in process mapping diagnostics (2026-11-01)

Wine `65f8cc3a` adds the `wex_process` channel to
`dlls/ntdll/unix/server.c`. After process/thread server initialization it
records Windows PID/TID and Unix PID/TID; no process, token, signal or access
semantics change. Only the Unix ntdll module is overlaid on the preserved
corrected WoW64 runtime. The r14 observer binds the native-held browser ID
to these records and verifies live thread command lines and thread groups,
without relying on `/proc/<leader>/environ`. The same instrumented runtime
passes all six x64/x86 startup, Mojo and descriptor programs. This resolves
diagnostic identity, not the browser stall or Chromium/Edge acceptance.

## Temporary opt-in pending wait diagnostics (2026-11-01)

Wine `b737f525` adds the `wex_wait` channel in
`dlls/ntdll/unix/{server.c,thread.c}`. It records pending-select cookies,
operation/size/flags/timeout, context presence and the first handle, the
returned status, and pending/resumed context handles. It changes no wait,
signal, process, token or access semantics. Only Unix `ntdll.so` is overlaid
on the corrected WoW64 runtime; both PE architectures and server are unchanged.
The r15 trace exercises these markers and all six x64/x86 startup, Mojo and
descriptor programs pass afterward. Its root observer aborts at five seconds
with an unknown original stderr cause, so it is not a completed browser
observation or a connection fix. Subsequent visual r16/r17 checks use the
original corrected runtime without this overlay.

## Temporary opt-in exception-boundary diagnostics (2026-11-01)

Wine `29e2853b` adds `wex_exception` in `dlls/ntdll/unix/virtual.c`.
It logs only the guaranteed-stack overflow transition and terminal
exception-frame allocation below the hard floor. Records include effective
and TEB stack guarantee, bounds, exception code/flags/address, original stack
pointer and requested allocation size. No allocation, protection, exception,
stack sizing or termination semantics change. Only Unix ntdll is overlaid;
the original PE modules of both architectures and corrected server remain
unchanged. The r30 runtime passes all six x64/x86 startup, Mojo and descriptor
programs. Its browser trace reproduces the overflow with an 8,192-byte effective
guarantee, zero TEB-requested guarantee and a terminal access violation, but
does not identify the initiating fault or a justified semantic fix.

Wine `ccebeff5` extends this temporary `wex_exception` instrumentation in
`dlls/ntdll/unix/signal_x86_64.c` and `virtual.c`: for stack-overflow and
access-violation dispatch it records exception parameters, access type and
fault address, and it marks entry to the suspend-signal branch with no TEB.
These traces do not change exception, signal, stack or wait behavior. r35
overlays only Unix `ntdll.so`; all six x64/x86 regressions pass, and the
captured no-TEB signal is from a different Wine process than the later
stack-overflow/AV trace. No semantic fix or Chromium acceptance is claimed.
See `CHROMIUM_MOJO_0X88_CONTINUATION_20261031.md`.

## Per-image page-aligned PE section cache for FileAlignment 0x200 DLLs (2026-11-01)

`dlls/ntdll/unix/virtual.c`: when `map_image_into_view` maps a non-shared PE section whose sector-rounded
raw file offset is not host-page aligned (Chromium, Edge and Electron DLLs linked with `FileAlignment` 0x200),
copy the section once into a per-image cache file under `<config_dir>/pe_section_cache` and `mmap` that file
`MAP_PRIVATE` instead of falling back in `map_file_into_view` to `mprotect` plus `pread` into private anonymous
memory. Untouched code and read-only pages are then shared through the page cache across processes, while
relocations and writable-copy sections stay copy-on-write. Cache files are keyed by `st_dev`, `st_ino`,
`st_size`, `st_mtim` and `st_ctim` (nanoseconds) plus a 64-bit digest of the unaligned section layout, written to
a temporary file and published with `rename()`, created under an advisory `flock` so concurrent first loads do
not each write the file, and pruned oldest-first to a 4 GiB cap (`WINE_PE_CACHE_MAX_MB` overrides). Aligned
sections, removable media and any cache or `mmap` failure fall back unchanged to `pread`. `TRACE_(module)` logs
cache hits and misses. Measured with Edge 156 (8 renderers): 5.9 GB to 3.9 GB PSS without touching the DLL. See
`EDGE_DEVELOPMENT_RUN_20261101.md` and the planned system service in `PREFETCH_SERVICE.md`.

## EmptyWorkingSet pages out the process's private memory (2026-11-01)

`dlls/kernelbase/process.c`: `SetProcessWorkingSetSizeEx` was a stub returning `TRUE`; it now passes the sizes to
`NtSetInformationProcess( ProcessQuotaLimits )`. `dlls/ntdll/unix/process.c`: that class is implemented for set:
`(SIZE_T)-1, (SIZE_T)-1` on the current process (`EmptyWorkingSet`) calls the new `virtual_trim_working_set()`
(`dlls/ntdll/unix/virtual.c`), which walks the per-page commitment of each private anonymous view (so reserve-then-commit
memory counts) and applies `MADV_PAGEOUT` to the committed pages; any other limits
are accepted and ignored, as before. `dlls/wow64/process.c` thunks the 32-bit `QUOTA_LIMITS`. Without swap the
call changes nothing; with swap or zram the pages are compressed or written out and fault back intact. Measured
with zram swap: 270 MB resident to 8 MB (64-bit) and 141 MB to 10 MB (32-bit), content intact after re-touch.

## PE images fetched from the wexprefetchd service (2026-11-01)

`dlls/ntdll/unix/virtual.c`: before falling back to the in-process cache described above, `map_image_into_view` now
asks the `wexprefetchd` service (`docs/PREFETCH_SERVICE.md`, `src/wexprefetchd/protocol.h`) for a page-aligned "flat
image" of any image that has a section whose sector-rounded file offset is not page aligned
(`pe_has_unaligned_sections`, the same test the section loop uses). `open_prefetch_image` connects to
`/run/wineexplorer/prefetch.sock` (`WINE_PREFETCH_SOCKET` overrides it), sends the already-open PE file as an
`SCM_RIGHTS` descriptor and never a path, and receives a read-only descriptor to a sparse file holding each section's
bytes at its RVA; the section is then mapped `MAP_PRIVATE` at that offset with the existing `map_pe_cache_section`. The
call has 15 s socket timeouts, retries `connect`, `sendmsg` and `recvmsg` on `EINTR` (Wine signals its threads constantly
and a socket with a timeout is not restarted after a signal), takes ownership of every descriptor that arrives so none can
leak, validates the reply (magic, version, status, layout, exactly one descriptor, regular file at least `image_size`
long, at most 1 GiB), and remembers per process that no service exists after the first `ENOENT`, `ECONNREFUSED` or
`EACCES`. Any failure falls through to the in-process cache and then to `pread`; `WINE_PE_CACHE=0` disables both cache
tiers. `TRACE_(module)` logs each image served by the service. A cache descriptor is used only if a page of it can be
mapped executable (`fd_maps_exec`), so a `noexec` filesystem falls back to `pread` instead of leaving code without
execute permission; the socket flags are guarded for systems without `SOCK_CLOEXEC` and `MSG_CMSG_CLOEXEC`.

## Client-side readahead of the previous run's private files (2026-11-02)

`dlls/ntdll/unix/virtual.c`: when a `wexprefetchd` reply to `open_prefetch_image` has `WEXPF_HINT_TRACE` set (new `hints`
byte in the reply padding, ignored by older clients), the process sends one `WEXPF_OP_GET_TRACE` request, receives the
NUL-separated paths the service could not open itself, opens each with the process's own rights (at most 64 files and
512 MB) and calls `posix_fadvise(POSIX_FADV_WILLNEED)` (`fetch_prefetch_trace`). Failures are ignored; it happens at most
once per process, after the image reply. See `docs/PREFETCH_SERVICE.md`.

## explorer switches to the desktop it creates (2026-11-02)

`programs/explorer/desktop.c`: after `CreateDesktopW` and `SetThreadDesktop` for `/desktop=NAME` (virtual or root
desktop), explorer calls `SwitchDesktop`. The fork's secure-desktop rules (server `send_hardware_message`) accept hardware
input only on the input desktop and no longer let a host event move it, so without this call the installer's Setup and
OOBE (`explorer /desktop=Setup`, `/desktop=OOBE`) drew their pages but got no mouse or keyboard input. Found by the first
fresh install of an ISO built on Wine 11.21; the same OOBE responded on the 11.19 runtime and not on the bare 11.21 merge.

## dwmapi: state, thumbnails, colorization and the missing exports (2026-11-02)

`dlls/dwmapi/dwmapi_main.c`, `dwmapi.spec`, `Makefile.in` (imports gdi32, advapi32), `include/dwmapi.h`, `dlls/dwmapi/tests/dwmapi.c`.
Follows the behaviour measured on Windows 11 (`scripts/dwmapi-probe.c`, `docs/DWMAPI_BEHAVIOR.md`): a process-local table of
per-window attributes (write-only attributes read back `E_INVALIDARG`, computed ones such as `CLOAKED`, `NCRENDERING_ENABLED`,
`CAPTION_BUTTON_BOUNDS`), a thumbnail table with properties and source size (the visible frame, like
`DWMWA_EXTENDED_FRAME_BOUNDS`), `DwmGetColorizationColor` from the DWM registry values, blur-behind and iconic bitmap argument checks, the exports
`DwmQueryThumbnailSourceSize`, `DwmModifyPreviousDxFrameDuration` and `DwmTransitionOwnedWindow`, `DWM_E_COMPOSITIONDISABLED` for the graphics
stream hint, and composition timing rates as counter frequency over refresh period. Nothing is drawn yet; a DWM service
(`docs/DWM_PLAN.md` 1.0) is to take over the state. The conformance test passes on Windows 11 and on Wine, 32-bit and 64-bit.

## DWM control channel: wineserver endpoint and `ProcessWineDwmChannel` (2026-11-02)

New `server/dwm.c` (requests `dwm_connect`, `dwm_get_broker_capability`, `dwm_broker_register`, `dwm_send`, `dwm_recv`, `dwm_reply`),
`include/wine/dwm_protocol.h`, `dlls/dwmapi/dwm_client.c`; small hooks in `server/protocol.def`, `server/user.h` (per-desktop endpoint and
epoch), `server/winstation.c` (init and revoke on desktop destruction), `server/process.c` (clean-up on process exit), `server/window.c`
(two accessors; `window_desktop_denied` made non-static), `dlls/ntdll/unix/process.c` and `dlls/wow64/process.c` (private information class
`ProcessWineDwmChannel` = 1004 on `NtSet/QueryInformationProcess`, the same pattern as `ProcessWineGrantAdminToken`), `include/winternl.h`.
One bounded message channel per desktop between clients and one broker; the server stamps sender, desktop, request id and epoch, so the wire
carries no identity. The broker registers with a one-use capability minted only for the owner of the desktop's top window. Absent broker:
`STATUS_PORT_DISCONNECTED`; messages are capped (size, queue depth 64). Test: `dlls/dwmapi/tests/dwm_channel.c` (skips on Windows).

## explorer starts and supervises the DWM broker (2026-11-02)

`programs/explorer/dwmhost.c` (new), `desktop.c` (`start_dwm_broker()` after the desktop window exists, `stop_dwm_broker()` at exit), `explorer_private.h`,
`Makefile.in`. Off unless `HKCU\Software\WineExplorer\Shell\DwmService` is non-zero. The desktop manager mints a one-use broker capability for its desktop
and starts `src/programs/dwm` (`dwm.exe /parent=<pid>`, the capability goes through its stdin) on that desktop; a worker thread restarts it after a crash (250 ms, 1 s, 4 s) and gives up
after three failures in a minute. `dwm.exe` ends when the channel is revoked or explorer exits. No broker is started for Winlogon or other desktops without a manager.

## dwmapi scalar attributes held by the DWM broker (2026-11-02)

`server/dwm.c` (any opcode up to `DWM_OP_LAST` is passed to the broker; the window token is still checked against the sender's desktop),
`include/wine/dwm_protocol.h` (`DWM_OP_SET_ATTR`, `DWM_OP_GET_ATTR`), `dlls/dwmapi/dwm_client.c/.h`, `dlls/dwmapi/dwmapi_main.c`,
`src/programs/dwm/dwm.c`. With a broker, `DwmSetWindowAttribute` also stores the DWORD in the broker and `DwmGetWindowAttribute` reads the broker
first, so another process sees the value. The process-local table stays as a mirror: it answers without a broker and refills a broker that
restarted. Entries of destroyed windows are dropped by a sweep. Not done: checkpoint sections for a setter that exited before a broker restart.
Test: `dlls/dwmapi/tests/dwm_attr.c` via `scripts/dwm-p3-attr-test.sh`.

## dwmapi: `WexDwmpPoll` for the shell's corner bridge (2026-11-02)

`include/wine/dwm_protocol.h` (`DWM_OP_POLL`, `struct dwm_poll_req/dwm_change/dwm_poll_reply`, flags `DWM_POLL_SNAPSHOT`, `DWM_POLL_MORE`),
`dlls/dwmapi/dwm_client.c`, `dwmapi.spec` (private export `WexDwmpPoll`). The broker (`src/programs/dwm/dwm.c`) keeps a revision counter and a
1024-entry change log; a poll returns the changes since a revision, or pages through the whole table when the revision is too old or zero.
The shell (`src/dlls/wexshell/dwm_bridge.c`, started only with `Composition=1` and `DwmService=1`) polls every 250 ms and passes
`DWMWA_WINDOW_CORNER_PREFERENCE` to the X11 compositor (`WexCompositionSetCorner`: ROUND 8 px, ROUNDSMALL 4 px, DEFAULT and DONOTROUND square).
Visual check: `scripts/dwm-corner-visual-probe.c` under the shell (`explorer /desktop=Shell,1024x768 /shell`) on Xvfb, composition on versus off.

## dwmapi: `DwmFlush` and timing info on the compositor's frame clock (2026-11-02)

`include/wine/dwm_protocol.h` (`struct dwm_clock_page`, `DWM_CLOCK_COMPOSED`, object names), `dlls/dwmapi/dwmapi_main.c`, test
`dlls/dwmapi/tests/dwm_clock.c` (and `gdi32` in the test imports). With `Composition=1` the shell (`src/dlls/wexshell/dwm_bridge.c`) creates a
per-desktop page `Local\__wex_dwm_clock_<desktop>` and event `Local\__wex_dwm_frame_<desktop>`; the X11 compositor (`src/dlls/wexhost/compose.c`)
writes the frame count and compose time (QPC) under a sequence lock after each composed frame, and a shell thread pulses the event
(`WexCompositionWaitFrame`; the compositor's Unix thread cannot touch Win32 objects). `DwmFlush` waits for the next composed frame, at most two
refresh periods; `DwmGetCompositionTimingInfo` reports `cFrame`, `cRefresh` and `qpcCompose`. These are composition times, not display times:
`qpcVBlank` stays an estimate until X Present or Wayland presentation feedback (later P6 slices). Without the page, or with
`HKCU\Software\WineExplorer\Shell\DwmFrameClock` = 0, both keep the previous estimate. Tested 47/47 on x86_64 and i386 under the shell on Xvfb.

## dwmapi: live thumbnails through the DWM broker (2026-11-02)

`include/wine/dwm_protocol.h` (`DWM_OP_THUMB_SET/DEL/LIST`, `struct dwm_thumb*`, at most 80 per desktop), `dlls/dwmapi/dwm_client.c/.h`,
`dwmapi_main.c`, `dwmapi.spec` (private `WexDwmpThumbList`), test `dlls/dwmapi/tests/dwm_thumb.c`. The process-local thumbnail table and its
validation stay; `DwmUpdateThumbnailProperties` also sends the merged properties to the broker (`src/programs/dwm/dwm.c`, keyed by connection
and handle, dropped when either window is gone) and `DwmUnregisterThumbnail` removes them. The shell's bridge polls the list and the X11
compositor scales the source window's pixels into the destination (`WexCompositionSetThumbnails`, XRender transform, bilinear). Not done yet:
`fSourceClientAreaOnly` (the whole window is used), minimized sources (no pixels), Wayland. The broker now sweeps every 5 s by elapsed time;
before, the sweep only ran after 5 idle seconds, which the bridge's polling prevented. Visual check: `scripts/dwm-thumbnail-visual-probe.c`.

## dwmapi: live thumbnails held by the broker (2026-11-02)

`src/programs/dwm/dwm.c` (a fixed table of `DWM_THUMB_MAX` entries keyed by (connection epoch, HTHUMBNAIL value), a `thumb_revision`
counter, `DWM_OP_THUMB_SET/DEL/LIST`, and a sweep that drops thumbnails whose dest or src window is gone), `dlls/dwmapi/dwm_client.c/.h`
(`dwm_client_thumb_set/del` and the private export `WexDwmpThumbList`), `dwmapi.spec`, `dlls/dwmapi/dwmapi_main.c` (a successful
`DwmUpdateThumbnailProperties` publishes the merged properties to the broker and `DwmUnregisterThumbnail` deletes them; the broker is
never called while holding `thumbnail_cs`), test `dlls/dwmapi/tests/dwm_thumb.c` (skips without a broker). The protocol in
`include/wine/dwm_protocol.h` is unchanged.

## dwmapi/winex11: glass margins (DwmAlpha) (2026-11-02)

`dlls/dwmapi/dwmapi_main.c`, `dlls/winex11.drv/bitblt.c`, `window.c`, `x11drv.h`, `x11drv_main.c`. With
`HKCU\Software\WineExplorer\Shell\DwmAlpha` non-zero (read once per process), `DwmExtendFrameIntoClientArea`
stores the four margins as window properties (`__wine_dwm_margin_*`, a -1 "sheet of glass" stored as 0x10000)
and `__wine_dwm_alpha`, and removes them for all-zero margins, then sends `SWP_FRAMECHANGED` so
the driver recreates the surface and the client repaints (pixels drawn before the switch carry a forced opaque
alpha); it still returns S_OK for every input. The surface keeps the pixels' own alpha when flushing (`keep_alpha`). The X11 driver selects the ARGB visual for such
windows (and back to the default visual when the margins are removed), avoids reusing a stale surface when the
glass state changes, and publishes the client rectangle and decoded margins on the whole window as
`_WEX_DWM_MARGINS` (CARDINAL, 8 values), refreshed on position changes. The X11 compositor
(`src/dlls/wexhost/compose.c`) paints the margin bands with `PictOpOver` (the alpha shows the desktop) and the
rest with `PictOpSrc`. Off by default; the conformance test (`dlls/dwmapi/tests/dwmapi.c`) is unchanged.
Visual check: `scripts/dwm-glass-visual-probe.c`.

## uxtheme/win32u/user32: opaque modern frames (DwmFrameEffects) (2026-11-02)

`dlls/uxtheme/window.c` (modern caption and frame renderer, owned rectangular window region), `dialog.c`, `system.c`,
`uxthemedll.h` (settings cache reset on WM_SETTINGCHANGE/WM_THEMECHANGED), `dlls/win32u/defwnd.c` (gate cache,
`NCFRAME_DWM_FRAME_EFFECTS` to `draw_themed_nc_frame`, modern hit testing), `dlls/user32/class.c` (keeps uxtheme loaded
for the gate without an active theme), `include/winuser.h` (`NCFRAME_DWM_FRAME_EFFECTS`), test
`dlls/uxtheme/tests/ncframe.c`. With `HKCU\Software\WineExplorer\Shell\DwmFrameEffects` non-zero (default off), captioned
top-level windows get a flat Windows 10/11-style opaque caption: DPI-scaled line glyphs, accent color from the DWM
values, dark mode and caption/text/border colors from `DwmSetWindowAttribute`, tool and maximized variants, matching
hit testing. Gate off: unchanged. ncframe 11 (off) and 31 (on) pass on x86_64 and i386. Visual check:
`scripts/dwm-frame-visual-probe.c`. Known: a one-pixel light strip at the top right of a colored caption.

## dwmapi/winex11/uxtheme: glass frames (DwmAlpha + DwmFrameEffects) (2026-11-02)

`dlls/dwmapi/dwmapi_main.c`, `dlls/winex11.drv/window.c`, `dlls/uxtheme/window.c`. With both gates on,
`DwmExtendFrameIntoClientArea` keeps the window glass even for all-zero margins and sets `__wine_dwm_frame`;
`DwmSetWindowAttribute` with `DWMWA_SYSTEMBACKDROP_TYPE` 2-4 or `DWMWA_MICA_EFFECT` does the same for a window that
has no glass yet (Windows 11 draws the backdrop behind the caption without extended margins). `_WEX_DWM_MARGINS` gets a
9th value, flags, bit 0 = the frame is glass; the compositor then blends the whole window except the opaque client
part. uxtheme paints such a frame twice, over black and over white, and writes the difference as per-pixel alpha (the
black pass is the premultiplied color), so the caption background is clear and text, glyphs and icons keep their
antialiased edges; the caption stays opaque when the app or the accent colors it; a pressed button repaints the whole
frame. Either gate off: unchanged. ncframe 31 passes on x86_64 and i386. Visual check:
`scripts/dwm-glassframe-visual-probe.c`.
Windows 10/11 geometry for glass frames that are not maximized (also `dlls/win32u/defwnd.c`): the left, right and
bottom frame bands are an invisible resize area (they still hit-test as sizing borders), the 1 px border runs along the
visible frame and the caption buttons move inward by the band width, in painting and in
`handle_modern_nc_hit_test` alike; `DWMWA_EXTENDED_FRAME_BOUNDS` reports the visible frame. The compositor clips to it,
rounds it (8 px unless `DWMWCP_DONOTROUND`) and draws a soft shadow outside it.

## user32/dwmapi: blur behind and ACCENT_POLICY (P12b) (2026-11-02)

`dlls/user32/win.c`, `dlls/dwmapi/dwmapi_main.c`, `include/wine/dwm_protocol.h`. `SetWindowCompositionAttribute`
accepts `WCA_ACCENT_POLICY` (19): it validates the window and the size, maps `ACCENT_ENABLE_GRADIENT` (1),
`ACCENT_ENABLE_TRANSPARENTGRADIENT` (2), `ACCENT_ENABLE_BLURBEHIND` (3) and `ACCENT_ENABLE_ACRYLICBLURBEHIND` (4) to
the private attribute `DWM_ATTR_WINE_BLUR` (0x80000001; mode plus a tinted flag) and `GradientColor` to
`DWM_ATTR_WINE_TINT` (0x80000002; opaque for state 1, used by state 3 only with accent flag 2), and passes them to
`DwmSetWindowAttribute` in a dwmapi loaded on demand; it returns TRUE. Other attributes keep the stub's failure.
`DwmEnableBlurBehindWindow` with `DWM_BB_ENABLE` sets the same attribute; with `DWM_BB_BLURREGION` the bounding box
of `hRgnBlur` goes to `DWM_ATTR_WINE_RGN_LT`/`_RB` (0x80000003/4) and the blur is limited to it.
`DwmSetWindowAttribute` accepts the private attributes; with `DwmAlpha` a window that has no glass yet
becomes a glass sheet, and loses it again when the blur is turned off. The blur itself needs the shell's
`DwmMaterials` gate. Gates off: the calls succeed and nothing is drawn differently.

## dcomp/winex11/wined3d: transparent DComp targets (P7b) (2026-11-03)

`dlls/dcomp/device.c`, `dlls/winex11.drv/init.c`, `dlls/wined3d/swapchain.c`. `dcomp_target_create` calls
`DwmExtendFrameIntoClientArea` (dwmapi loaded on demand) with sheet-of-glass margins for a top-level target window
with `WS_EX_NOREDIRECTIONBITMAP`; dwmapi applies it only with the `DwmAlpha` gate. `needs_offscreen_rendering`
returns TRUE for a window with `__wine_dwm_alpha`, so an opaque GL client window never covers the ARGB window.
`swapchain_gl_present` sends a target scene of such a window (with no GDI underlay) through the CPU composition and
`SetDIBitsToDevice` to the window's ARGB surface, premultiplied, instead of the GL drawable. Gate off: unchanged.

## dwmapi: iconic thumbnails (P9) (2026-11-03)

`dlls/dwmapi/dwmapi_main.c`, `dlls/dwmapi/dwm_client.c`, `dlls/dwmapi/dwmapi.spec`, `include/wine/dwm_protocol.h`.
`DwmSetIconicThumbnail` and `DwmSetIconicLivePreviewBitmap`, after the existing checks, copy the bitmap (at most
4096x4096, else `E_INVALIDARG`) as top-down BGRA into a section named `Local\__wex_dwm_iconic_<hwnd>` or
`Local\__wex_dwm_iconic_live_<hwnd>`, sized to the picture, which the process keeps open while the window lives (32
windows per process); the live preview also records the client offset and the window's restored rectangle.
`DwmInvalidateIconicBitmaps` clears both. New private export `WexDwmpGetAttr` reads an attribute as the broker holds it, so the
shell sees `DWMWA_HAS_ICONIC_BITMAP` set by another process. The shell draws the picture in its taskbar and Alt+Tab
previews (off unless `TaskbarThumbnails` / `AltTabThumbnails`). The section is readable and writable by the session,
like the other `Local\` DWM objects; a reader only ever gets a wrong preview picture. dwmapi now has a `DllMain`
that closes the sections on `FreeLibrary`; a section that already existed is never trusted to be large enough.

Review fixes (2026-11-03): `uxtheme/window.c` holds its own reference to dwmapi (`LoadLibraryW`) for its cached
`DwmGetWindowAttribute`. dwmapi makes a window glass for blur only when `DwmMaterials` is on too, and an explicit
`DwmExtendFrameIntoClientArea` takes over glass the blur made, so turning the blur off keeps the app's margins.

## uxtheme/win32u/dwmapi/winex11: Aero glass frames (P13) (2026-11-03)

`dlls/uxtheme/window.c`, `dlls/win32u/defwnd.c`, `dlls/dwmapi/dwmapi_main.c`, `dlls/winex11.drv/window.c`,
`include/winuser.h`. With `DwmFrameStyle` = 1 (set by a theme's `[WineExplorer] FrameStyle=aero`) next to
`DwmAlpha` and `DwmFrameEffects`, every captioned top-level window gets a Vista/7 glass frame: uxtheme asks dwmapi
for zero margins once per window, dwmapi marks the frame `__wine_dwm_frame` = 2 and, with `DwmMaterials`, blurs it
with the colorization color. uxtheme draws the active style's `DWMWindow` parts (Windows 7 part numbers) over the
glass and stores the caption buttons' geometry in `__wine_nc_buttons` (`NCFRAME_BUTTONS` in winuser.h), which
win32u's hit test reads. winex11 publishes frame flag 2, so the compositor shows the frame to the window's edges with
6 px corners. Without glass the themed frame is drawn (Vista Basic) and hit-tested the classic way; a style without
`DWMWindow` parts falls back to the Windows 10/11 glass frame. Style 0 (default): unchanged.

`dlls/uxtheme/draw.c`, `msstyles.c`, `msstyles.h` (P13, 2026-11-03): an atlas image named by a disk stream property is
loaded from the STREAM resource only (`MSSTYLES_LoadBitmap` takes `is_stream`; the image cache keys on it), because
resource ids are per type and Windows 7 styles also carry an IMAGE with the same id. A part-level atlas rectangle is a
vertical strip of states; a state-level one is used as is. Atlas parts count as image backgrounds in the size,
content-rect and transparency queries too.

`dlls/uxtheme/stylemap.c` (P13, 2026-11-03): INI (XP format) styles may define the `DWMWindow` class with the Windows 7
part names (frames, caption buttons, glyphs, `TEXTGLOW`); its states (ACTIVE/INACTIVE, NORMAL/HOT/PRESSED/DISABLED)
are listed once for all parts, which `MSSTYLES_LookupPartState` now accepts as a fallback (no other class lists any).

`dlls/win32u/defwnd.c`, `dlls/uxtheme/window.c` (polish, 2026-11-03): with `DwmFrameEffects`, the caption button under
the pointer is kept in the window property `__wine_nc_hot` (set on WM_NCMOUSEMOVE with a non-client leave tracking,
cleared on WM_NCMOUSELEAVE) and the frame repainted; uxtheme draws it hot (Aero: the style's HOT state; the Windows
10/11 frame: gray, red for close).

`dlls/dwmapi` (`dwmapi_main.c`, `dwmapi.spec`), `dlls/uxtheme/window.c`, `dlls/win32u/defwnd.c` (polish, 2026-11-03):
a theme that switches `DwmFrameStyle` while windows live switches their frames. WM_THEMECHANGED repaints the frame
(with `DwmFrameEffects`); uxtheme calls the new private export `WexDwmpRefreshFrame` for a glass frame that does not
match the style: Aero glass that uxtheme asked for goes away under the Windows 10/11 style, other glass is marked
again with its margins. The Aero blur dwmapi adds is remembered (`__wine_dwm_blur_auto`) and removed with it; an
application's own blur replaces it.

Review fixes (Codex, 2026-11-03): an application's blur request is never replaced by the automatic Aero blur; an
application's own `DwmExtendFrameIntoClientArea` makes the glass its own (a frame style switch keeps it); every theme
change refreshes glass frames once (a new colorization recolors the Aero blur); `__wine_nc_buttons` is removed whenever
the Aero painter does not draw; button geometry is clamped to what the property holds before painting (300 % dpi).

`dlls/uxtheme/window.c` (2026-11-03, docs/CAPTION_BUTTONS.md): the Windows 10/11 frame colours follow measurements on a
Windows 11 host: flat #F3F3F3 (light) and #202020 (dark), active and inactive, close hover #C42B1C, inactive light glyph
#929292. The Aero frame takes its caption button sizes from the style's `DWMWindow` parts.

## winewayland.drv/dwmapi: DWM glass on Wayland (P5) (2026-11-03)

`dlls/winewayland.drv/{window.c,window_surface.c,wayland_surface.c,waylanddrv.h}`, `dlls/dwmapi/dwmapi_main.c`,
`include/wine/dwm_protocol.h`. A window with the `__wine_dwm_alpha` property (DwmAlpha) gets an ARGB8888 buffer; on flush
the alpha byte is forced to 0xff outside the glass area (the frame when it is not glass, the client interior inside the
margins) and kept inside it, and the surface's opaque region is set to the opaque area. Without the property nothing
changes. dwmapi publishes the glass frame kind as the private broker attribute `DWM_ATTR_WINE_FRAME` (0 none, 1
Windows 10/11, 2 Aero) so the shell can tell the compositor to round and shadow the frame.

## dcomp: align with Wine-Staging conformance tests (2026-11-03)

Wine `dlls/dcomp/{device.c,dcomp.spec,Makefile.in,tests/Makefile.in,tests/dcomp.c}`, `configure.ac`.
Brings Wine's DirectComposition device implementation in line with the behavior validated by
CodeWeavers' Wine-Staging DirectComposition test suite (Zhiyi Zhang):
- Export validation: DCompositionCreateDevice accepts only IID_IDCompositionDevice (returning
  E_NOINTERFACE otherwise); DCompositionCreateDevice2 and DCompositionCreateDevice3 accept
  IID_IDCompositionDevice and IID_IDCompositionDesktopDevice; NULL out pointer returns E_INVALIDARG
  for all three creation exports, CreateTargetForHwnd, and CreateVisual (patches 0003, 0005, 0006, 0022).
- Version-gated QueryInterface: device QI exposes IDCompositionDevice2 and IDCompositionDesktopDevice
  only on version >= 2 devices, and IDCompositionDevice3 on version >= 3 devices. Visuals created
  from a v1 device reject IDCompositionVisual2 with E_NOINTERFACE (patches 0009, 0011, 0067).
- Argument validation: CreateTargetForHwnd rejects GetDesktopWindow() with E_INVALIDARG (0008);
  SetRoot returns E_INVALIDARG if the visual is already the root of another target (0015);
  SetBackFaceVisibility validates the enum values (0039); AddVisual returns E_INVALIDARG without
  silent reparenting if the visual is already a child or a root (0039) and does not take an extra
  COM reference on pending child visuals; SetContent validates that content supports swapchain or
  surface interfaces or returns E_INVALIDARG (0013, 0051).
- SetOffsetX/SetOffsetY over-validation removed: any float is accepted and stored (0043).
- Target window property safety: replaces target object pointer in the window property table with
  a sentinel value (HANDLE)1, avoiding cross-process pointer leaks.
- Conformance test suite: ported 12 unit tests from staging covering device creation, targets,
  visuals, SetRoot, SetContent, SetOffsetX, AddVisual, and enum validation.

## uxtheme, dwmapi: composited text, buffered paint alpha, DWM attribute accuracy (2026-11-03)

Wine `dlls/uxtheme/{draw.c,buffer.c,uxthemedll.h,tests/glass.c,tests/Makefile.in}`, `dlls/dwmapi/dwmapi_main.c`.
- DrawThemeTextEx honours DTT_COMPOSITED, DTT_GLOWSIZE, DTT_CALCRECT and the colour properties (glow
  and shadow on glass title text).
- Buffered paint keeps per-pixel alpha: BPPF_ERASE clears the buffer, BufferedPaintSetAlpha works on
  the glass bitmap.
- DwmIsCompositionEnabled reports the real gate state; DwmGetWindowAttribute round-trips FLIP3D_POLICY
  and EXCLUDED_FROM_PEEK; DwmGetColorizationParameters returns the theme colours.
- New test `uxtheme/tests/glass.c`.

## dcomp: IDCompositionSurfaceFactory and IDCompositionSurface BeginDraw/EndDraw (2026-11-04)

Wine `dlls/dcomp/{device.c,Makefile.in,tests/Makefile.in,tests/dcomp.c}`. Ports the behaviour of Wine-Staging's
DirectComposition series (patches 0047-0054, LGPL-2.1+): CreateSurfaceFactory and CreateSurface on the
Device2/Device3/desktop devices (formats B8G8R8A8, R8G8B8A8, R16G16B16A16F; alpha PREMULTIPLIED/IGNORE), a physical
`ID3D11Texture2D` on the application's device, and the BeginDraw/EndDraw state machine (one active draw per
device, whole-surface first draw, busy error reported before rectangle validation). Surface content is not yet
composed onto targets; virtual surfaces, SuspendDraw, ResumeDraw and Scroll stay stubs. Tests ported from
staging; dcomp test 917/917 on x86_64 and i386.

## dcomp, dxgi, d3d11, wined3d: surface content in the retained composition (P7b slice 1, 2026-11-04)

Wine `dlls/dcomp/device.c`, `dlls/dxgi/swapchain.c`, `dlls/d3d11/{texture.c,d3d11_private.h}`,
`dlls/wined3d/{swapchain.c,wined3d.spec}`, `include/wine/{wined3d.h,winedxgi.idl}`. A DirectComposition surface is
accepted as visual content; builtin interfaces `IWineDCompSurface` (physical texture, content valid after EndDraw)
and `IWineD3D11Texture` (wined3d texture) let dxgi turn the last ended draw into a retained frame through the new
`wined3d_dcomp_frame_create_from_texture` / `wined3d_dcomp_frame_release` exports. Not presented yet without a
swapchain producer (slice 2, in progress); dcomp tests 917/917 on x86_64 and i386.

## dcomp, dxgi, wined3d: surface-only composition producer (P7b slice 2, groundwork, 2026-11-04)

Wine `dlls/dcomp/{device.c,tests/dcomp.c,tests/Makefile.in}`, `dlls/dxgi/swapchain.c`, `dlls/wined3d/{swapchain.c,wined3d.spec}`,
`include/wine/{wined3d.h,winedxgi.idl}`; reverts the slice 1 `dlls/d3d11` private interface. A target whose scene has
surface content but no swapchain gets a lazily created 1x1 composition swapchain as producer (revoked when a real
swapchain layer appears or the scene goes away). dxgi snapshots a surface through a D3D11 staging copy into
`wined3d_dcomp_frame_create_from_pixels` (replaces `_create_from_texture`, which acquired a GL context off the CS thread
and asserted); wined3d takes its own frame references. A never-drawn surface composes as nothing. Verified on screen with X grabs (`scripts/check-dcomp-surface-screen.sh`): green, then yellow after Commit; the
test's three GetPixel checks stay todo_wine because winex11 presents GL to a child drawable GDI does not read; dcomp 941 tests, 0 failures.

## dcomp, dxgi, wined3d: harden the P7b surface composition (2026-11-04)

Wine `dlls/dcomp/device.c`, `dlls/dxgi/swapchain.c`, `dlls/wined3d/swapchain.c`. Code-review fixes for the P7b
DirectComposition work: (1) dxgi's surface frame staging copy now serializes with the app thread through
`ID3D11Multithread` Enter/Leave on the surface device's immediate context (the same lock the app's D3D11 calls take),
so a concurrent draw/EndDraw cannot race the CopyResource/Map/Unmap; (2) the device commit is all-or-nothing across
bindings: every producer is activated before the visual tree is switched, and a failed activation revokes the
already-activated bindings and leaves the previous scene and tree intact; (3) auxiliary-producer revocation is
deferred until the commit is known to succeed, so a failed transaction keeps it; (4) a surface frame capture failure
(OOM, staging create, Map) now returns the D3D11 HRESULT (typically `E_OUTOFMEMORY`) instead of collapsing to
`E_NOTIMPL`, with the previous scene left intact; (5) wined3d's `set_target_scene` tracks the number of layers whose
references were acquired and releases exactly those on failure, instead of truncating `layer_count`; (6) duplicate
producer preparation is deduplicated by the content `IUnknown` (the underlying object) rather than the
`IWineDCompSwapChain` interface pointer. dcomp 941 tests, 3 todo, 0 failures on x86_64 and i386; on-screen check
(`scripts/check-dcomp-surface-screen.sh`) shows green then yellow. The dxgi conformance failures (display/adapter
rects, refresh rate, factory refcount) are unchanged by this work: a dxgi dll built from commit `29696397` (which did
not touch the dxgi module) reports the identical set, so they predate the dcomp work.

Note on the commit transaction above: a failed activation revokes the producers already activated in that commit (their
previous scene is not re-published, so a failure blanks them rather than mixing old and new); the visual tree and
the auxiliary producers are only changed after every activation succeeded. The dxgi conformance failures seen in
the harness (67 in a plain 1024x768 Xvfb prefix: display-mode, monitor-rect and factory-refcount checks) are identical
with the dxgi dll from before the dcomp work, so they predate it.

## dcomp: complete surface draw controls, scrolling and virtual surfaces (2026-11-04)

`wine/dlls/dcomp/device.c`, `wine/dlls/dcomp/tests/dcomp.c`: implement
`IDCompositionSurface::SuspendDraw` / `ResumeDraw` with the device-wide active-draw
state, physical `ID3D11Texture2D` backing and `content_valid` preservation;
implement validated immediate `Scroll` through a temporary D3D11 texture; and
expose virtual surfaces from the factory, Device2, Device3 and desktop-device
entry points. Virtual surfaces allow a partial first BeginDraw and implement
overlap-preserving Resize plus validated no-op Trim, bounded to the 11_0
16384-by-16384 texture limit. The conformance additions cover suspend/resume
contention, Scroll, partial virtual drawing, Resize and Trim. Static inspection
only; the user will build and run the 941+ test suite on i386 and x86_64.

## dcomp: IDCompositionSurfaceUnknown resize compatibility (2026-11-04)

`wine/dlls/dcomp/device.c`, `wine/dlls/dcomp/tests/dcomp.c`: ported the Wine-Staging 0063/0064
`IDCompositionSurfaceUnknown` ABI (IID `5680c979-14c8-48b5-8b92-3247379fd907`) onto the existing
surface object. The inherited public surface methods remain backed by our retained physical texture,
`IWineDCompSurface`, SuspendDraw/ResumeDraw/Scroll state, and virtual-surface interface. Unknown1–2
and Unknown4–99 return `E_NOTIMPL`; Unknown3 validates nonzero, bounded dimensions, rejects any
device-wide active draw with `DCOMPOSITION_ERROR_SURFACE_BEING_RENDERED`, preserves the overlapping
texture contents, replaces the physical texture, and invalidates the old draw/content state. The
ported `test_surface_interface` assertions cover `E_INVALIDARG` for zero dimensions, `S_OK` for valid
resize, busy-draw rejection, and post-resize bounds validation, based on staging patch 0064 by Zhiyi Zhang.
Static inspection only; the user will build and run dcomp tests under Wine on x86_64/i386 and natively
on Windows 11.
Native check of the surface-interface test on the Windows 11 host (build 26300) crashes at the first successful
`Unknown3` resize, so that interface is undocumented and unstable across Windows versions: the test follows staging's
layout and was only verified under Wine (1002 tests, 0 failures, x86_64 and i386); the staging stack-pointer check
(removed upstream by patch 0065) is not ported.

## services.exe: single instance in WoW64 prefixes (2026-11-05)

`wine/programs/services/services.c` takes the named mutex `__wine_SvcctlInstance` after creating the started event. A
second `services.exe` (the 32-bit and 64-bit `wineboot --init` can start two within ~200 ms in a WoW64 prefix, before
either sets the started event) waits for the started event and exits 0 instead of building a second service database.
Verified in the QEMU guest with the Chrome updater service (it now reaches the dispatcher and reports running) and with
the advapi32 `service` test (759/777 tests, 0 failures). See `CHROME_INSTALLER_ANALYSIS.md` section 9.

## user32: GetPointerDevice (2026-11-05)

`wine/dlls/user32/misc.c` and `user32.spec` export `GetPointerDevice` (previously a commented-out stub). It fails like the native export does for an invalid
handle (`ERROR_INVALID_HANDLE`, or `ERROR_INVALID_PARAMETER` for NULL, checked on Windows 11), because
`GetPointerDevices` reports no devices and so no handle is valid. Chromium delay-loads
the export during start-up and aborts with a delay-load failure (`DelayLoad-ModuleName` USER32.dll, last error 127)
when it is missing; the official Chromium installer itself passes. Test: `dlls/user32/tests/input.c`
`test_GetPointerDevice`.

## dcomp: no magnitude limit on visual transforms (2026-11-05)

`wine/dlls/dcomp/device.c` (`dcomp_visual_SetTransform_locked` and the commit-time accumulated transform) only rejects
non-finite matrix values. The earlier 64x scale and 1048576 translation limits were arbitrary and made Chromium's
GPU process abort (`dc_layer_tree.cc` check, `E_INVALIDARG`) when it scaled a 1x1 solid-colour surface to the layer
size. Test: `test_visual_SetTransform_scale` in `dlls/dcomp/tests/dcomp.c` (passes on Windows 11 and Wine). See
`CHROMIUM_DCOMP_ANALYSIS.md`.

## server: registry key security descriptor calls ignore the handle access (2026-11-05)

`wine/server/handle.c` (`get_security_handle_obj`, used by `set_security_object` and `get_security_object`): for a registry
key the descriptor right (`WRITE_DAC`, `WRITE_OWNER`, `READ_CONTROL`, `ACCESS_SYSTEM_SECURITY`) is checked against the
caller's token and the key's descriptor, not against the access bits of the handle. Windows does the same: on Windows 11 a
key created with `KEY_WRITE` or reopened with `KEY_SET_VALUE` can have its DACL read and changed through `GetSecurityInfo` and
`SetSecurityInfo` (probe `ChromiumLab/guest-sysrole/probes/keywrite_dacl.c`, native results all 0). Other object types keep
the handle check. Reason: Chromium's updater service creates `ClientStateMedium` with `KEY_WRITE` and then calls
`AddAllowedAce`, which returned 0x80070005 and made the Chrome installers fail. Test: `test_regkey_descriptor_handle_access`
in `dlls/advapi32/tests/security.c` (the same calls pass on Windows 11 and under Wine). See `CHROME_INSTALLER_ANALYSIS.md`
section 15.

## rpcrt4: LPWSTR and LPSTR parameters in the typelib marshaller (2026-11-05)

`wine/dlls/rpcrt4/ndr_types.idl` (the reference method that generates the shared format strings gets `[in, string] wchar_t *`
and `[in, string] char *` parameters) and `ndr_typelib.c` (`write_oleaut_tfs`, `type_memsize`, `get_stack_size`,
`get_param_info`) support `VT_LPWSTR` and `VT_LPSTR` parameters, which the Chrome updater's type library uses
(`get_param_info unhandled type 31` made `CreateStubFromTypeInfo` fail with `E_NOTIMPL`, the updater's COM calls returned
`E_NOINTERFACE` and the Chrome MSI failed with 1627). Verified in the QEMU guest: with the change the Chrome enterprise MSI
installs (exit 0, `Program Files\Google\Chrome\Application\chrome.exe`). Only the `[in]` pointer form is covered; `[out]
LPWSTR *` is not. A unit test in `dlls/oleaut32/tests/tmarshal.c` is still to be written (see `TYPELIB_PROXYSTUB_ANALYSIS.md`).

## combase: IClientSecurity::SetBlanket on proxies (2026-11-05)

`wine/dlls/combase/marshal.c`: `ProxyCliSec_SetBlanket` accepts the blanket and records it in the proxy manager (`blanket_*`
fields); `ProxyCliSec_QueryBlanket` returns the recorded values once set and, before that, the defaults measured on Windows 11 (WINNT, authz none, PKT_PRIVACY, IDENTIFY). Security is
not enforced for the local transport; the change only stops `CoSetProxyBlanket` from failing with `E_NOTIMPL`. The Edge updater
calls it (PKT_PRIVACY, IMPERSONATE) on its update-service proxy and treats a failure as fatal ("Failed to install apps
0x80004001", MSI 1627). Verified in the QEMU guest: with the change the Edge enterprise MSI installs (exit 0,
`Edge\Application\156.0.4314.58`) and the installed Edge passes the browser fixture (WebGL and WebGL2). `dlls/ole32/tests/marshal.c` has `test_proxy_blanket` and the stale `todo_wine` markers of `test_client_security` are gone; the whole
`marshal` suite passes on Windows 11 natively (1174 tests, 0 failures) and under Wine (1249/1252 tests, 0 failures).

## d3d12: include the D3D11 headers before initguid.h (2026-11-06)

`wine/dlls/d3d12/d3d12_main.c` includes `d3d11_4.h` before `initguid.h`. Our `winedxgi.idl` now imports `d3d11.idl` (for the
DirectComposition surface interfaces), so `winedxgi.h` pulled the D3D10/D3D11 headers in after `initguid.h`, `d3d12_main.o`
defined their IIDs itself and `d3d12.dll` failed to link against `libdxguid` (multiple definition of `IID_ID3D10*`). A fresh
full build (the ISO build) hit it; incremental dev builds had not rebuilt d3d12.
