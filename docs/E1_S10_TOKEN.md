<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S10 account-specific server token

Current-tree protocol note: [S13](E1_S13_HKU_LOAD.md) advances the protocol to
978 and appends request 325. Request 324 and S10's byte records are unchanged.
S13 adds internal token provenance and a separate one-shot load attempt; the
977 layouts/commands below document S10's original package revision. Use the
current harness pin and regenerate the current protocol for new validation.

Implemented for disabled lab evaluation, 2026-11-06. **Static review only: no
build, generator execution, Wine, WSL, VM or probe execution.** Read with
[S1](E1_S1_CONTRACTS.md), [S4](E1_S4_ACCOUNTS_MAP.md),
[S5](E1_S5_BROKER_RECORDS.md), [S6](E1_S6_TRANSPORT.md),
[S8's S18 interface](E1_S8_EVIDENCE.md), [decisions](E1_DECISIONS.md) and
[tooling](TOOLING.md). S9 evidence was not produced or inferred here.

The owner-selected boundary is Wine-only identity: the issued token represents
the selected administrator, while the eventual process retains the caller's
host UID, prefix, wineserver, Wine session and validated desktop. This package
creates no target, changes no UID/groups, loads no hive and implements no broker
credential path. Normal server builds return STATUS_NOT_SUPPORTED for every
new operation, including negotiation. No production caller, capability
advertisement, environment switch, provisioning or installer enablement exists.

## Private request and byte records

One request is appended: `create_e1s_account_token`, request number 324 (zero
based). `SERVER_PROTOCOL_VERSION` is **977**, previously 976. Client and server
must be rebuilt together. Fixed request is 32 bytes: header 0..11, operation u32
at 12, context handle u32 at 16, caller handle u32 at 20, caps u64 at 24. Fixed
reply is 24 bytes: header 0..7, context u32 at 8, token u32 at 12, caps u64 at 16.
Variable data is bounded separately. The existing 88-byte fixed maximum in
`protocol.def`, `server_protocol.h` and `tools/make_requests` is sufficient and
unchanged. Generated headers were updated by hand using that generator's
alignment, table and tracing conventions; Qwen must check regeneration.

`include/wine/wex_token.h` defines the shared byte sizes and LE helpers.
`VARARG(record,e1s_token)` uses a dedicated redacting trace dumper; regeneration
preserves the redaction. Neither nonces, references nor account record bytes are
dumped by server tracing.

| Operation | Input | Output / ownership |
| --- | --- | --- |
| NEGOTIATE = 1 | No variable bytes, zero context/caller, caps exactly 0xc0. | Non-inheritable context handle, negotiated 0xc0. ACCOUNT_MAP_V1 and TOKEN_V1 are a private **lab** intersection, not launch readiness. |
| ISSUE = 2 | Context, caller=0, caps matching negotiation; exactly 204 bytes. | Non-inheritable primary token handle with exactly TOKEN_ASSIGN_PRIMARY, TOKEN_DUPLICATE and TOKEN_QUERY; canonical S1 kind 5. |
| TEST_INSTALL = 0x80000001 | Context, retained standard peer process handle, caps; exactly 268 bytes. | No result handle. Synthetic consumed-record fixture only; no authentication claim. |

The 204-byte ISSUE envelope is the **unchanged S1 kind-4 record (188 bytes)**
followed by the original 16-byte S8 evidence reference. That reference is
connection-local metadata; a reference or digest submitted by itself grants
nothing. The S1 record offsets are:

| Offset | Field |
| --- | --- |
| 0 / 4 / 8 | kind:u16=4, version:u16=1; length:u32=188; required_caps:u64=0xc0 |
| 16 / 24 / 56 / 88 | request_id:u64; request_digest[32]; binding_digest[32]; nonce[32] |
| 120 / 124 | attempt:u32; account_key[16] |
| 140 / 148 / 156 / 164 / 172 | root_gen, server_gen, broker_gen, map_gen, policy_gen, each u64 |
| 180 / 184 / 188 | session_id:u32; reserved:u32=0; envelope evidence_ref[16] |

Zero IDs/generations/digests/nonces/keys/references, unknown caps, wrong
kind/version/length, reserved bytes, extra/truncated data and mismatches deny.
Exact mask 0xc0 is this narrow link's required mask. S18 must still require all
ten S1 capabilities across the responsible launch components.

Success kind 5 is 136 bytes, including the S4 28-byte binary user SID. Offsets:
header 0, request_id 16, server_gen 24, status/reserved 32/36, token_ref[16] 40,
account_key[16] 56, SID blob length 72 and bytes 76, auth_id 104, token_id 112,
session/type/elevation/integrity 120/124/128/132. The opaque token_ref is the
new token-id/auth-id pair in LE, scoped by this context and server generation;
it is never an independently resolvable handle or grant. The real token handle
is transferred separately on the authenticated server connection.

Failure kind 5 is 108 bytes with empty SID, nonzero NTSTATUS at 32 and every
other result field zero. The export also zeroes unused response capacity through
byte 135. No token is transferred on failure. Pre-dispatch server failures
may have no variable reply; the export supplies the same canonical failure.

The private PE export, also declared under `__WINESRC__` in `winternl.h`, is:

```c
NTSTATUS WINAPI NtCreateWexAccountToken(ULONG operation, HANDLE *context,
    HANDLE caller, ULONGLONG *caps, const void *record, ULONG length,
    HANDLE *token, void *response, ULONG *response_size);
```

All pointer arguments except record/response are required. NEGOTIATE and
TEST_INSTALL use no response buffer and input response_size=0. ISSUE requires
response capacity exactly 136; response_size receives 136 or 108. Context is
input/output only for negotiation and must be closed by its owning broker.
The export has bounded local input/output arrays, rejects oversized API input,
closes unexpected handles and denies malformed successful replies. Invalid
local API parameters need not dispatch a server transaction. As with H2b, this
is a private PE `wine_server_call` wrapper with no new Unix syscall or WoW64
thunk slot; handles are protocol u32 values and records contain no pointers.

## Authority, consumption and token shape

The implementation requires Linux and **both** WEX_E1S10_TOKEN_TEST and
WEX_E1S6_TRANSPORT_TEST. These are explicit isolated-build flags. The handler
checks elevation enforcement, active protected system role, privileged sysrun
connection, SYSTEM service-account primary, enabled SeTcbPrivilege and absence
of a thread impersonation token. It additionally requires S6's held-image
broker role, nonzero registration generation, retained primary object and
unchanged TokenId/ModifiedId. An unrelated SYSTEM service is insufficient.

Negotiation creates an unnamed server context owned by the actual broker
process and requesting server thread/connection. It retains the broker primary
and registration snapshot; copied/inherited context handles cannot acquire
another process's authority. S6 re-registration or broker token mutation denies
old contexts. The context owns one transaction: negotiated -> installed ->
permanently consumed. Installation can be attempted once. ISSUE burns installed
state **before** comparing record bytes, looking up accounts or allocating a
token. Failure, mismatch or allocation failure cannot retry the record.
Malformed raw ISSUE sizes/caps also burn it. Closing the context releases its
retained process/token objects and authority callback state.

`server/e1s_token.h` supplies the server-internal S18 handoff:
`token_install_e1s_consumed(context, record)`. It is not an export or a
production IPC installer. Its trusted producer must redeem S8 on the owning
authenticated connection, compare the whole evidence record against S5,
serialize consume/cancel, and supply the exact kind 4/reference, canonical
account name, original retained S3 caller/primary objects and original absolute
CLOCK_MONOTONIC expiry. A required trusted validator rechecks current root,
server, broker, policy/map generations, endpoint pins, original writer/caller
versions, request/image lease, route, deadline and cancellation. It runs at
installation and before/after token construction. The context takes ownership
of its validator cookie only on successful installation. All production calls
currently deny. **S18 must implement this authoritative producer/validator and
revocation wiring; it cannot use the fixture callbacks or infer authentication
from serialized bytes.** S6's persistent role fields alone do not prove a live
S8 connection, current policy generation or evidence redemption.

TEST_INSTALL calls that same internal installation machinery with deliberately
synthetic metadata, an actual retained standard peer primary, and a five-second
server-generated fixture expiry. Its always-true native-authority callback is
compiled only with the two test flags. It can install deliberately unknown,
tombstoned or stale identity records so that the real ISSUE lookup is tested.
It does not read S8 or change S5–S8 code. No production producer uses it.

For issuance the server checks an unchanged live Primary/Default/Medium ordinary
standard caller: no service identity, Administrators, restricted context or
AppContainer, and nonzero AuthenticationId; process and token session must
agree with the record. It independently calls `lookup_account` using the
**retained canonical name**, compares the returned incarnation and map generation
to kind 4, and requires fresh administrator membership. No name, SID, groups,
privileges or token template comes from ISSUE. Unknown/tombstoned, nonadmin,
missing configured group, unsafe map, NSS errors and stale generation all deny.
A second lookup after construction compares the S4 snapshot by its fields
(including UID, primary GID and SID), and deadline/caller/authority are checked
again before handle allocation.
S4's documented account lifecycle/NSS concurrency limits remain applicable.

The factory calls `create_token` with explicit data from that snapshot:

* User and owner: the S4 mapped account SID. Primary group: S-1-22-2-GID for
  S4's trusted primary GID, present and enabled in the token's group list.
* Groups: enabled World, Local, Interactive, Authenticated Users, BUILTIN Users,
  Administrators, primary group and one fresh logon SID. **S4 v1's trusted
  snapshot contains primary GID and configured-group administrator membership;
  it supplies no general supplementary-group SID list.** No additional Unix
  memberships are invented, and no fixed legacy-domain Users SID is copied.
* Privileges: the 21 supported privileges of Wine's existing administrator
  factory, with its enabled/disabled defaults, including disabled SeTcbPrivilege.
* Primary, Full, High, caller's session. Default DACL grants only SYSTEM and
  that mapped account GENERIC_ALL. The token object's DACL is protected through
  `token_protect_elevated`, separate from its default DACL.
* Fresh nonzero `alloc_logon_luid` AuthenticationId, different from the caller
  and each issuance, with exhaustion rejection. Logon SID is
  S-1-5-5-AuthenticationId.HighPart-AuthenticationId.LowPart. It is created
  before groups are copied and never patched from the standard caller.

There is no linked-token call, getuid-based admin factory or SYSTEM-token SID
swap. Existing `token_duplicate` copies auth_id, groups, elevation, integrity
and session; `create_process` duplicates the explicitly selected primary and
derives process session from it. These existing paths supply duplicate/child
propagation without editing `process.c`. Runtime child verification remains S11.
Existing standard-session `create_linked_token` and `grant_process_admin_token`
denials are unchanged. No child or host authority is created by issuance.

## Files and preserved work

Stock Wine changes, also registered in [STOCK_CHANGES.md](STOCK_CHANGES.md):

| Files | Change |
| --- | --- |
| server/token.c, server/e1s_token.h, server/Makefile.in | Constrained factory, broker/context/consume gates, trusted internal handoff and test-only installer. |
| server/protocol.def, request_handlers.h, request_trace.h, include/wine/server_protocol.h | Appended request, version 977, layouts/assertions/tables. |
| server/trace.c | Dedicated byte-count-only redaction. |
| dlls/ntdll/sec.c, ntdll.spec, include/winternl.h | Private export and declaration. |
| include/wine/wex_token.h, include/Makefile.in | Shared byte constants/LE helpers and header inventory. |
| dlls/advapi32/tests/wex_token.c, Makefile.in | Ordinary/unnegotiated caller denial and cleared failure-record test. |

Project changes: `scripts/e1s10_token_probe.c`, this note and the appended S10
stock register entry. **No S5–S8 file was edited.** In particular, broker main.c
and e1s.h, wexhost transport, tools/e1s6/e1s7/e1s8, server sysrun/process/named-pipe
work and all account-provider fixes were preserved. No accounts.c lookup hook
was needed: the trusted consumed record retains the canonical S4 name and
ISSUE calls the existing lookup and compares the key/generation. No commit.

## Qwen build and lab procedure

The runnable entry point is now `scripts/test_e1s10_token.sh`; see
[E1-S11 prerequisites and coverage](E1_S11_TOKEN_TESTS.md) for its two-build
invocation, owned-fixture cleanup, implemented extra modes and remaining gaps.
The extended harness/probe are statically reviewed only, not new acceptance
evidence. The procedure and wire requirements below still apply.

Run S6/S7/S10 system-role tests **as root in wexbuild or an exclusively owned
throwaway VM**, with no installed system role before the sysrun/S6 provisioning
harness starts. NEVER run that harness in shared lab guest VM B or remove its
`/etc/wineexplorer`, `/usr/local/libexec/wineexplorer` or `wexadm` to bypass a
fixture refusal. Coordinate fixture ownership. Read TOOLING.md and current harness
source. The normal and lab runtimes must be separate, root-protected WoW64
builds (`--enable-archs=i386,x86_64`), with both MinGW compilers. Never run these
installers/probes on the Windows host. No commands below were executed here.

1. Sync the exact files above, assemble the overlays, regenerate with
   `tools/make_requests` from the assembled Wine root and compare all three
   checked-in protocol artifacts. Expected version is 977; request number 324;
   32/24-byte fixed layouts and unchanged max=88. Review any generator diff,
   including its server-status name table, before proceeding. Build/relink
   wineserver, both ntdll PE DLLs and both advapi32 test binaries. Run `wex_token`
   and `security` in both architectures against the normal server. The new test
   must deny; missing export on this Wine runtime is a failed build/deployment
   check, even though the test skips it on unrelated Wine/native Windows.
2. In the isolated lab tree, force recompilation of server/token.o with
   `CFLAGS='-g -O2 -DWEX_E1S10_TOKEN_TEST -DWEX_E1S6_TRANSPORT_TEST'` (not EXTRA_CFLAGS: Wine's makefiles ignore it), and of
   server/sysrun.o and the S6 wexhost Unix component with the existing S6 flag.
   Relink, inspecting the actual compiler command lines. A flag change alone
   does not invalidate existing objects. Keep the S10 flag out of all production
   Makefiles/installers. Reuse the owned S6 native-service fixture and kernel/bus
   positive control from E1_S6_TRANSPORT.md, without modifying those sources.
3. Build the standalone lab probe for both ABIs, for example:

   ```sh
   x86_64-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -Isrc/include \
       scripts/e1s10_token_probe.c -ladvapi32 -o "$OWNED_OUT/e1s10-64.exe"
   i686-w64-mingw32-gcc -O2 -Wall -Wextra -Werror -Isrc/include \
       scripts/e1s10_token_probe.c -ladvapi32 -o "$OWNED_OUT/e1s10-32.exe"
   ```

   Use MinGW's Windows SDK headers only: never add `-Iwine/include`, which mixes
   Wine and MinGW Windows types (`WINBOOL` failure in the first combined run).
   The probe includes the dependency-free `wex_token.h` byte helpers by relative
   path, and resolves both private exports via `(function_type)(void *)` casts
   through `GetProcAddress`. No Wine SDK include search path or ntdll import
   library is required. Compilation of this correction is unverified.
   The probe resolves the private export
   dynamically and uses S6's existing `WexE1s6Transport` for a real registration.
4. In the owned machine root, stage **one selected ABI** of this probe as the
   exact root-controlled `windows/system32/wexelevate_test.exe` recognized by
   S6. Refuse a foreign existing test-broker file; save/restore only owned state.
   Run it as a LocalSystem service child in Bob's **standard-user prefix**, using
   the existing service adapter (`wexdev svc` or the owned sysrun fixture), not a
   normal command that merely has an administrator token. Keep the native S6
   service running. Do not overwrite the shipping broker. Do not run both ABI
   variants against the same staged file concurrently.
5. Give the broker these arguments (quoted as individual Windows arguments):

   ```text
   broker PIPE CANONICAL_NAME INCARNATION_HEX MAP_GENERATION EXPECTED_SID MODE
   ```

   PIPE is a fresh unique local `\\.\pipe\...` name. Key/generation/SID come
   from the current root-owned S4 map, never from the caller's SID. The probe
   prints `E1S10 READY` after creating its one-instance pipe. Concurrently, in
   Bob's same server/prefix/session, run the other selected ABI as an ordinary
   standard client with `peer PIPE`. Use an outer 20-second timeout for **both**
   processes; the pipe API has blocking waits. Run `denied` separately as a
   filtered Limited administrator and as Bob. Do not use concurrent `wexdev`
   commands that contend on its shared lock; its service adapter may be paired
   with a direct owned-fixture `runuser`/Wine peer invocation as in its harness.
6. Require broker exit 0 **and** `E1S10 OK`, peer exit 0, and no FAIL lines.
   A setup/transport failure is FAIL, never a successful negative test. Each
   negative issuance runs after successful negotiation/fixture installation.
   Stop only the owned service/processes and restore that exact staged file and
   service/hive state; no global wineserver kill or foreign-fixture reset.

| MODE / invocation | Required check |
| --- | --- |
| accept, with Alice and then Carol | Actual Primary/Full/High, expected user/owner SID, enabled Administrators, primary/logon groups, 21 privileges with expected Tcb/notify defaults, restrictive default DACL, non-inheritable handle, caller session, nonzero distinct logon, DuplicateTokenEx logon preservation; response agrees with queries. Two separate contexts/transactions must get different logons. Caller TokenId/ModifiedId/AuthenticationId stay unchanged. |
| unknown | Use a syntactically valid, absent canonical account and arbitrary nonzero key; ISSUE denies. |
| tombstone | Root provisions then explicitly retires a fixture identity; pass its old canonical name/key. ISSUE denies. Use a fresh process/pipe; don't recreate it under the same key. |
| stale | Valid current account/key, with probe incrementing map generation in its installed request; ISSUE denies. |
| no-cap | Real S6 broker, but missing TOKEN_V1 negotiation bit; no context/token. |
| digest / evidence | Mutate digest or evidence_ref after install. ISSUE denies and consumes; restored bytes cannot retry. |
| expired | Sleep past the synthetic five-second installation deadline; ISSUE denies. |
| replay (automatic in every issuance case) | Repeat original bytes after either success or failure; no token. |
| denied (separate invocation) | Limited/ordinary client cannot negotiate even with an asserted capability mask. |
| normal build (separate runtime) | Every new operation denies, including the correctly named SYSTEM lab probe. No S10 fixture code in normal server. |

The S11 harness implements removal from the configured administrator group and
a missing group at ISSUE; malformed/trailing/oversized raw requests; absent
install; copied contexts across threads/processes; caller death; and a benign
child/descendant SID, logon SID, auth_id and session driver. These remain unrun.
Broker primary replacement/security-version mutation, S6 re-registration,
caller replacement, injected allocation failures, exhausted LUID allocation and
child host UID measurement remain explicitly open in the S11 note. They need
controlled drivers/injection without changing production checks. Preserve old
filtered-admin success/cancel, standard linked-token/grant denial, S3 provenance
and S6 impostor controls. Check installed artifact exclusion separately.

## Static checks and outstanding evidence

Reviewed the explicit token factory, current ordinary/duplicate/child paths,
S4 lookup statuses, S6 role fields, canonical record offsets, response clearing,
handle allocation ordering and cleanup. Matched protocol request order, enum,
dispatch, request/reply dump tables and names (325 entries), and manually checked
new offsets/32- and 24-byte sizes against the generator's 4/8-byte alignment.
Checked the protocol maximum against the unchanged generator limit. Root and
Wine `git diff --check` passed. SHA-256 comparisons confirm that the protected
broker, accounts, process and sysrun files match the initial in-flight snapshot;
the preexisting STOCK_CHANGES.md entries were preserved verbatim.

**Unverified:** every compile/link, actual generator reproduction, private PE
export/WoW64 ABI, every test/probe, NSS/Unix-group SID behavior, both independent
administrator identities, real handle rights/default DACLs, native S6 role and
channel lifetime, fresh logons/duplicate/child propagation, session/desktop and
host UID behavior, allocation/FD/clock/exhaustion failures, stress/leaks/races,
production binary exclusion and deployment. No S9/S11 acceptance evidence is
claimed. S18's actual S8 producer/validator and complete policy/server/root
generation authority remain unwired; fixture success would not validate them.

Tooling observation: `exec_command` returned exit_code 0 with empty output for
Get-Content/Get-Location/git status and explicit PowerShell/cmd retry calls;
their cause was not exposed, so those results were not treated as checks.
Direct Node filesystem reads and `execFileSync` of Git/rg provided visible
output. An rg no-match produced its normal exit-1 exception during inspection;
the subsequent direct reads completed the check. No build, VM, service or
fixture state was changed by these tool attempts.
