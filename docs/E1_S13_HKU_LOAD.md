<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S13 managed HKU load

Work in progress, default disabled. Static survey recorded before implementation.
No builds, WSL, VM, fixtures or runtime probes are authorized in this sandbox.
Wine-only identity: Bob remains the host UID and owns prefix storage; Alice is
the Wine token user. Same-UID native access is outside this boundary.

## Survey of the incoming checkout

* `wine/server/registry.c:2509-2544,2685-2693`: startup opens prefix `user.reg`,
  creates `User\\<local_user_sid>` and registers it among fixed save branches.
  `:2546-2563` formats an arbitrary SID, but startup supplies only the local SID.
* `wine/server/registry.c:969-996,1075-1094`: system-role per-user detection
  recognizes only local SID and `_Classes`; fallback DACL grants local SID,
  SYSTEM and Administrators, with Administrators owner. Managed roots need
  explicit Alice owner and descriptors on every loaded child, not this fallback.
* `wine/dlls/ntdll/reg.c:182-237`: current-user path uses effective token SID;
  `RtlOpenCurrentUser` calls NtCreateKey, permitting an absent empty HKCU.
* `wine/dlls/kernelbase/registry.c:50-62,166-176,447-475`: HKCR is machine
  Software/Classes; predefined HKCU/HKCR handles are process cached. Existing
  `RegOpenUserClassesRoot` at `:2733-2756` is a stub returning HKCR. A managed
  view must bypass cached SYSTEM/Bob handles, include Alice classes and refuse
  missing registration before allowing the machine fallback.
* `wine/server/registry.c:2352-2443`: load_keys reads Wine text hives; ordinary
  load_registry duplicates a client file handle and autodetects prefixes.
  `:2044-2050` parser errors only print diagnostics. Managed loading needs a
  strict parse, bounded source, relative keys and rollback of unpublished state.
* `wine/server/registry.c:2936,3192-3244,3438-3496`: saving and generic
  load/unload exist; save registration is fixed startup state. S13 must keep
  managed branches out of Bob's save branch, reject generic unload, and report
  flush as unsupported until S14 implements checked persistence and leases.
* `wine/dlls/userenv/userenv_main.c:668-697`: A/W load stubs return the caller's
  predefined HKCU irrespective of hToken; unload always fails. A managed token
  must open only its registered hive through the broker gate; no stub success.
* `wine/dlls/ntdll/unix/registry.c` NtLoadKey/NtLoadKeyEx and
  `wine/dlls/kernelbase/registry.c` RegLoadKeyW use the generic file-handle
  route. They carry no S4 identity, S10 consumption or S12 storage reference.
  The chain is `wine/dlls/advapi32/advapi32.spec:655-656` imported RegLoadKey
  A/W, kernelbase RegLoadKeyW, then
  `wine/dlls/ntdll/unix/registry.c:697-750` NtLoadKeyEx opening the file and
  sending a generic request.
* `wine/tools/winebuild/import.c:580,1492,1617`: private exports are omitted
  from import libraries. Kernelbase/userenv resolve the new private function
  dynamically; only ntdll calls its own implementation directly.
* `wine/server/token.c:2528-2556,2731-2778`: S10 gate requires enforced active
  system role, privileged SYSTEM primary, enabled Tcb, no impersonation and
  S6 image/generation/token endorsement. Issuance relooks up S4 name/key/map,
  allocates a fresh logon and issues a query/duplicate/assign-only handle.
* `tools/wex-profile/profile.c` validate_profile/manifest/find_existing and
  `profile.h`: S12 layout is prefix-local
  `drive_c/users/.e1s/<SID>/<incarnation>-<label>/user.reg`. Completion metadata
  binds prefix device/inode and random storage generation. Existing rename
  labels are retained. Provision descriptors are not leases; all kind-6 S1
  reserved fields stay zero. S13 needs a no-create reopen using the same code.
* `wine/server/file.c:370-409,480-534`: file security is synthesized from Unix
  mode; file_set_sd does not implement Windows owner/group/SACL persistence.
  Applying an SDDL string alone cannot satisfy S12's pending ACL contract.
  Managed descriptor persistence and access checks must be explicit.

## Implemented boundary and protocol

Normal builds return STATUS_NOT_SUPPORTED for every new operation. Loading
requires Linux and all of `WEX_E1S13_HKU_TEST`, `WEX_E1S10_TOKEN_TEST`,
`WEX_E1S6_TRANSPORT_TEST`. No build-system default, production caller,
environment enable switch or new capability advertisement exists. S18 remains
disabled; S13 registration is not an S1 kind-6 lease, and all S12 reserved lease
fields remain zero. There is no credential or host-identity transition here.
Automatic ntdll/kernelbase current-user/classes hooks additionally require
`WEX_E1S13_HKU_TEST` in both PE architectures; normal clients do not query S13
on these registry paths. The private PE export remains available for explicit
denial probes against a normal server.

Append request 325 `load_e1s_profile`; protocol **978**, previously 977.
S10 request 324 and its byte records remain unchanged. Fixed request layout:
header 0..11, operation:u32 at 12, context:handle32 at 16, token:handle32 at 20,
access:u32 at 24, padding 28..31. Reply: header 0..7, hkey:handle32 at 8,
managed:u32 at 12; total **32/16 bytes**. Protocol/generator maximum remains
88. Tables/unions/assertions/tracing were edited by hand, matching generator
padding conventions; actual regeneration is a Qwen prerequisite. S13 bytes use
the existing count-only `e1s_token` trace dumper; no record/path/SID bytes leak.

`include/wine/wex_profile.h` defines the private reference, not a new S1 record:
48 LE bytes: version:u32=1 at 0, size:u32=48 at 4, canonical S4 account_key[16]
at 8, map_generation:u64 at 24, S12 storage_generation:u64 at 32, reserved:u64=0
at 40. The storage generation names S12 completion metadata in this prefix;
it has no independent authority. No SID, name, native path, file handle or
prefix selector is accepted. Canonical name comes from the retained S10
consumed record; the server re-resolves S4 and derives SID/layout itself.

Private PE export, declared under `__WINESRC__` in winternl.h:

```c
NTSTATUS WINAPI NtLoadWexProfile(ULONG operation, HANDLE context, HANDLE token,
    ACCESS_MASK access, const void *record, ULONG length,
    HANDLE *key, ULONG *managed);
```

No syscall table or WoW64 thunk slot changes: the PE wrapper carries protocol
u32 handles and byte records for both architectures. Output key/managed clear
on every failure, unexpected handles close, and the local input is bounded to
48 bytes. NULL token means open/query the effective token with an owned local
handle. Required output pointers may not be NULL.

| Op | Input and authority | Successful output |
| --- | --- | --- |
| LOAD=1 | Original S10 context and issued token object, 48 reference bytes, requested key access. Exact S10/S6 broker gate. | Non-inheritable handle to only that account's HKU root, managed=1. |
| QUERY=2 | Token query handle, context/access/variable bytes zero. No loading authority. | managed=0/1 only; no key. |
| OPEN=3 | Token and desired access; registered hive required; exact broker gate. No reference bytes/context. | That token's registered root only. |
| UNLOAD_S14=4 | Registered managed token, broker gate. | Always unsupported; no handle, unlock or deletion. |
| CURRENT=5 | Effective managed identity or endorsed broker; registered hive required. | Exact-SID HKCU root with normal access checks. |
| CLASSES=6 | Same identity gate as CURRENT; registered hive required. | Private live merged classes view, user precedence. |

LOAD burns a separate attempt in the issuing S10 context before reference
validation, S4 lookup or I/O. Wrong/truncated/reference/ownership failures are
not retryable on that context. The original issued token is retained there;
duplicates preserve managed provenance for current-user views but cannot
authorize a load. Copied contexts retain S10's process/thread/generation gate.
S4 snapshot, retained caller, original expiry and trusted validator are checked
again before publication. Closing the context releases the retained token.
S18 still has to provide the real authenticated producer/revocation wiring;
S10's synthetic lab installer is not production authentication.

## Storage, security and publication

`wine/server/wex_profile_storage.c/.h` now contain S12's shared implementation;
the original tool source/header are thin includes, preserving its lab API and
CLI. `wex_profile_open_existing()` never provisions, seeds, creates a lock or
cleans transactions. It selects a stored rename label by authoritative key,
checks the complete manifest/prefix/storage generation, Bob ownership/modes,
no-follow edges, required skeleton, device/mount, POSIX ACL/reparse/hardlink and
legacy/alias rules. Missing provisioned state or hive maps to distinct
STATUS_OBJECT_PATH_NOT_FOUND. Unsafe ownership/incarnation/reference denies
ACCESS_DENIED; storage exclusion denies SHARING_VIOLATION. Unknown accounts
already fail S10/S4. No Bob hive or shared machine hive is an input.

The exact backing hive inode receives a nonblocking exclusive flock retained
until server exit, plus pinned profile/hive descriptors. This is provisional
load exclusion, not S14's durable path/save/recovery lock. A same SID already
registered, a different active incarnation, unknown existing SID/classes tree
or cache-name collision fails with OBJECT_NAME_COLLISION and is never merged.
S14 will later implement safe reuse/reference counts and saving.

The loader snapshots at most 1 MiB, rejects NULs and observed size/mtime/ctime
changes, and parses relative to the newly created exact-SID root. Managed
parser errors deny; global/key option directives (including text descriptors
and symlink options) deny. Existing ordinary startup/generic parsing is
unchanged. Allocation/parse/security/identity failure deletes only new registry
state, closes descriptors and transfers no handle. Security writes already
fsynced to private storage on a later failed attempt remain inert metadata;
they are not a completed load or a lease. The result handle is allocated
before final publication, so handle exhaustion also rolls registry state back.

Every loaded registry key has explicit Alice owner/group, protected inherited
Alice/SYSTEM/Administrators full-control DACL and High NW label; no Everyone
ACE. Future managed keys use SID-parametric managed defaults, ahead of the
unchanged local-SID fallback. The `_Classes` key aliases Alice's actual
Software/Classes. Managed branches are volatile and outside the fixed startup
save branches: no accidental Bob user.reg saving. Root delete/rename and
generic unload are refused; flush returns unsupported, never durable success.

`apply_e1s_profile_security()` checks/applies/fsyncs/queries the same explicit
Alice/SYSTEM file SD and High label on every existing skeleton object, profile,
manifest and hive. It preserves Unix Bob owner/group and 0700/0600. The private
`user.WEX_E1S13_SD` xattr is consistency storage, not identity evidence: only
server-registered, pinned inodes use it; standalone copied/forged xattrs grant
nothing. Metadata/xattr changes return a real empty denying DACL. The native
file-open path checks before truncation or delete disposition. Registered
parents also check Wine access before creation, require private modes, apply
and query inherited descriptors, reject unsafe objects and refuse unregistered
native-inserted ancestor directories below registered roots. Native Bob can
still directly read/change Bob-owned files, as the owner decision accepts.

Prefix edges and opened identities are rechecked after security application,
and all pinned security descriptors are queried back before publication.
Initial trees are bounded to 16384 visits/depth 32; registry loads to 16384
keys/depth 64; at most eight registered accounts and 131072 pinned security
inodes per server. Resource exhaustion denies. These pins/locks intentionally
remain until server exit. Preexisting Wine file handles and complete descendant
lifetime/recovery handling still require S14; no production launch is enabled.

## HKCU, classes, caches and userenv

Managed RtlOpenCurrentUser asks the server for CURRENT instead of creating an
empty SID key. Missing registration denies even if an unknown tree is present.
Kernelbase selects a separate cache by effective SID, TokenId and ModifiedId,
so SYSTEM/Bob handles and earlier token-security versions do not shape Alice's
HKCU/HKCR. Bob's startup mapping and legacy cache remain unchanged.
Managed maximum-access opens below High integrity deny explicitly; they do not
gain implicit write rights from the ordinary MIC path's explicit-write check.
Kernelbase resolves its cached roots with explicit `KEY_ALL_ACCESS` and retries
only ACCESS_DENIED with `KEY_READ`; every attempt uses the existing server
DACL/MIC checks. Medium/Low callers can obtain read-only roots where those
checks allow them, while writes to High-labelled keys remain denied. Other
errors and a failed read attempt never fall back to Bob's roots.
Raw cache entries/handles remain until process exit, without detection of an
unexpected close/reuse; S14 must implement handle lifetime and generation
invalidation before unloading. SID/TokenId/ModifiedId selection is not that
lifetime contract.

HKCR's private unnamed cache combines live machine classes and Alice's classes
on open/query/enumeration, with user names/values taking precedence. Its
descriptors belong to Alice; machine source reads still require access. Writes
select Alice's source, including copy-on-write creation for machine-only keys;
no machine values are imported into Alice's hive. Deleted user entries can
reveal machine fallback, consistent with a union. Managed classes use shared
WoW64 classes nodes; explicit 32-bit lookup of an unnamed union retains that
exact cache handle instead of reopening its diagnostic name. Security and
cache/WoW64 behavior need both-ABI runtime validation; no native parity claim
is inferred from these static paths.

Union-view rename and notifications explicitly return NOT_SUPPORTED in S13;
there is no successful machine-only delete or silently armed proxy notification.
Ordinary direct user/machine-key operations retain their existing behavior.

LoadUserProfile A/W first identifies a managed token. For those tokens it
validates structure size and calls broker-gated OPEN against an already
registered hive; it never interprets lpUserName/path as load authority. The
broker must explicitly LOAD with its S10 context and S12 reference first.
This two-step path fits the Windows API's lack of those private inputs.
Ordinary default-build stubs keep returning predefined HKCU. Managed unload
calls the clearly named `e1s_profile_unload_s14()` TODO and fails closed while
the hive stays loaded. No S15 environment/path fallback work was pulled in.

## S12/S13 review repair (2026-11-07)

Read the independent report named in `E1_S12_S13_FINDINGS.md` and checked its
claims against the current source before editing. All changes below are
uncommitted and reviewed statically; no build, fixture or runtime execution.

| Finding | Source verdict and repair |
| --- | --- |
| 1: query errors treated as managed | The expression did treat failures as true, causing a real default-client compatibility problem. Fail-closed behavior is intentional when identity cannot be established in the enabled lab. Replaced the Boolean classification with a separate NTSTATUS and managed flag. Only a successful reply with managed=1 selects managed roots; successful managed=0 and disabled-server NOT_SUPPORTED can select stock paths. Token-open denial, NO_TOKEN after both effective-token opens, NO_MEMORY and query denial do not prove unmanaged identity, so still deny in the lab. The compile gate removes these new failures from normal-client paths. The review's unconditional success-only Boolean/fallback suggestion would open an identity-error bypass and was not adopted. |
| 2: extra IPC in ordinary registry use | Real. All kernelbase S13 automatic root, WoW64 union and explicit-token classes hooks, and ntdll RtlOpenCurrentUser's hook, are under WEX_E1S13_HKU_TEST. The test build still queries each effective identity; no process-global unmanaged answer can survive impersonation, primary-token replacement or token security changes. The harness now forces/relinks both client ABIs with the define. |
| 3: non-High cached roots denied | Real. Kernelbase's implicit MAXIMUM_ALLOWED request conflicted with the server guard. Explicit KEY_ALL_ACCESS with ACCESS_DENIED-only KEY_READ retry preserves read access allowed by DACL/MIC. Server guards, labels and all other authorization checks are unchanged. |
| 4: ancestor opens on every file open | Real recurring cost, although the old walk already had a 128-iteration bound. One cached unmanaged parent ancestry now retains at most 129 directory pins. Hits verify starting identity and every parent edge/root by statx device/inode/mount ID, avoiding ancestor openat/close calls and repeated secured-list scans. Cache misses/reparenting rebuild the proof; new security publication clears it, including inherited registrations. Cold-walk I/O/mount-ID/depth uncertainty denies and closes all provisional pins. No client path string is accepted as a containment proof. |
| 5: raw root handles cached indefinitely | Real S13 lifetime limitation; documented in code and above, as the task requests. No S14 unload/invalidation work was added. The report's contrast with stock cached dummy handles is inaccurate: stock special_root_keys also holds opened real handles. |

The ancestry cache retains only one chain: alternating directories can still
require cold walks to `/`, and hits retain O(depth) statx work plus the existing
immediate-parent descriptor check. Cache construction and replacement are
bounded; steady-state latency and syscall improvements have not been measured.
Cached negative proofs hold descriptors and recheck edges rather than assuming
an unchanged ctime, path spelling or native directory placement. These checks
do not make native same-UID races atomic with the later file open; the existing
same-UID boundary and S14 lifetime limits remain.
Constructing a negative proof explicitly requires successful fstat and an
inode lookup that finds no registration; the ordinary SD helper's NULL result
can also mean fstat failed and is not used as proof of unmanaged ancestry.

The disabled-server fallback must not conflate a missing private export or a
malformed successful reply with server NOT_SUPPORTED. Kernelbase now returns
PROCEDURE_NOT_FOUND for the former; the ntdll wrapper rejects the latter with
DATA_ERROR, retaining output clearing and unexpected-handle closure. These
indeterminate failures therefore deny rather than selecting stock roots.

Additional Qwen follow-up cases, **not implemented by the existing case table
below and not run here**, are required in both ABIs:

* Normal clients: trace repeated HKCU/HKCR/WoW64 and RegOpenUserClassesRoot
  calls and establish that no `load_e1s_profile` request is emitted. With the
  lab client, alternate Bob/Alice impersonation and token ModifiedId changes;
  verify success/managed=0 uses stock roots, managed=1 never uses Bob's cache,
  and injected token-open/query/resource failures deny without a returned key.
  Also inject a missing private export and malformed success replies (invalid
  flag, unexpected key or reply payload); require failure and handle cleanup.
* Lower an otherwise managed token to Medium and Low. Read-only HKCU/HKCR
  opens and direct queries must succeed where the DACL permits; set/create/
  delete/security-write and explicit maximum-access requests must remain
  denied by High NW. Repeat after cache population and token modification.
* Warm an unmanaged directory cache, then move that directory or an ancestor
  under a registered profile and try a native-inserted unregistered descendant.
  Repeat through relative paths, symlink aliases and bind-mount views; denial
  must survive a moved registered subtree, too. Warm a negative proof before
  loading another profile or publishing inherited security and check invalidation.
* Measure cold/repeated/alternating outside-profile opens and inspect directory
  FD counts. Inject fstat/statx/open failures, missing mount IDs and depth exhaustion;
  require denial, no leaked provisional descriptors, at most 129 cached pins,
  and unchanged registered-parent create/inherit/access enforcement.

Repair verification performed: source-text preprocessor guard checks for all
kernelbase managed helpers/call sites and ntdll's RtlOpenCurrentUser calls;
manual access-mask, cache-proof, publication invalidation and descriptor
ownership/cleanup review; `git diff --check` in Wine and for the touched project
files. The server registry/token/file/fd authorization and protocol/generated
files match HEAD. No compilation, harness execution, shell syntax parsing,
generator, runtime probe, benchmark, WSL or VM use in this repair.

## Qwen build/test handoff

All tests below are implemented but **unrun here**. Use root in clean wexbuild
or an exclusively owned throwaway VM; never VM B's installed role. Read
TOOLING.md and the current S10/S6 fixtures. Prepare two separate root-protected
WoW64 builds and sync **all new/untracked files**, including shared storage
sources and headers. Regenerate with `tools/make_requests`, compare all three
generated artifacts, then rebuild server, both ntdll/kernelbase/userenv DLLs
and both advapi32 test binaries. Verify version 978, request 325 and 32/16-byte
layouts. Run `wex_hku`, `wex_token`, `security` and registry tests in both ABIs
on the normal server; private exports must exist in this Wine runtime.

```sh
WINE_BUILD_DIR=/root/OWNED-lab-wow64 \
WINE_NORMAL_BUILD_DIR=/root/OWNED-normal-wow64 \
bash scripts/test_hku_load.sh
```

The entry point conditionally reuses the existing S10 harness and actual S6
fixture, not copies of their ownership/cleanup code. It forces/rechecks the
three lab defines on token/registry/storage/security/file/fd objects, separately
forces ntdll `reg.o` and kernelbase `registry.o` in both `x86_64-windows` and
`i386-windows` with `WEX_E1S13_HKU_TEST` via architecture-specific CFLAGS,
rebuilds S6 objects, relinks both client DLLs and server, checks normal artifact
exclusion (including the UTF-16 union marker in both kernelbase DLLs), compiles both
probe ABIs and a native S12 provisioner. Root opens a protected synthetic offline
seed; the native provisioner permanently drops to Bob before prefix mutation.
Only tracked fixture users/prefixes/configuration/services are used; foreign
roles/users/trees refuse. Logs survive failure. Require final
`E1S13_HKU_LOAD OK`, both-ABI broker and peer exits zero and no FAIL lines.

| Case | Implemented assertion |
| --- | --- |
| accept | Exact SID HKU and classes owner/three-trustee inherited DACL, managed API handle, Alice current HKCU in impersonation and actual child/grandchild primary processes; live user+machine HKCR values; private newly created file owner and Unix modes; Bob Wine hive open denied, native Bob access retained; Bob HKCU marker/path/token unchanged. |
| absent hive | Provision bundle, move only its hive aside; distinct PATH_NOT_FOUND; no HKU root/classes. |
| unprovisioned account | Valid S4 administrator/S10 token, no S12 tree, nonzero synthetic reference; same distinct absence denial, nothing loaded. |
| unmapped account | Existing S10 unknown-account case; no token/load authority. |
| wrong incarnation/reference | Tamper manifest incarnation or submitted canonical key; ACCESS_DENIED, nothing loaded. |
| hive owner | Root changes only the owned fixture hive's Unix owner; ACCESS_DENIED, nothing loaded; server never repairs ownership. |
| already loaded | A second fresh S10 context/logon for the same SID receives NAME_COLLISION. |
| limited caller | Ordinary Bob and Limited administrator cannot load/open/unload; QUERY on lab only reveals unmanaged=0. |
| replay | Original LOAD bytes after every successful/failed attempt deny with cleared outputs; fresh-context results remain independently checked. |
| normal server | Every private operation, including SYSTEM service invocation and QUERY, denies; lengths/output clearing covered by wex_hku and both standalone probes. |

## Static verification and remaining work

Performed: source/authority/descriptor/rollback review; explicit string-escape
inspection; protocol enum/handler/request-dumper ordering and field/size/max
text checks; Git whitespace checks; Git Bash `bash -n` only. No compilation,
generator execution, Wine/probe/fixture execution, WSL, VM or guest changes.
No commit. Existing S10/S11/S6 changes were preserved, including sysrun.c and
the project broker/transport changes that predated this package.

Concurrent-worktree observation at final review: another writer created project
commit `dca694e0` and Wine commit `19eb31d9` containing the in-progress S13 tree.
This implementation pass issued no commit/reset and preserved that history;
its final corrections remain uncommitted. Those commits' S6/S10 test messages
are not S13 runtime evidence.

Unverified: compilation/linking/headers on Linux; actual regeneration; every
lab case; filesystem xattr/ACL/mount enforcement and persistence; inherited
file security and open/create paths; explicit 32/64-key views, restricted-token
cache changes, full classes enumeration/delete/notification behavior and
native compatibility; descriptor/handle leaks under faults, map/NSS races,
preexisting handles, substitutions during load, second-server conflict, quotas,
allocation/I/O/fsync failures and same-UID race limits. S14 still owns leases,
path-level load/save exclusion, durable saving, descendants/open keys, unload
and recovery. S15/S18 still own profile consumers/environment and real broker
integration. No production readiness or S16/S20 acceptance is claimed.

No new owner decision was needed for this disabled package. The supported
release matrix and other section-9 release choices remain open. The added
storage reference is a private consistency input, and must never be presented
as a completed S1 profile lease or permission to launch.

Tooling observations: initial default PowerShell calls returned empty output;
those results were discarded and explicit cmd.exe plus encoded PowerShell
restored reads. A quoted multiword rg argument split (OS error 123); targeted
single-token searches avoided it. `python` was absent from PATH. Several
apply_patch attempts failed atomically on an unmatched anchor or duplicate
delete/add target; split patches with verified anchors succeeded. Direct quoted
Git Bash invocation was misquoted by the runner; encoded PowerShell invocation
ran syntax parsing. An initial protocol text-check regex also matched two reply
dumpers; restricting it to the request-dumper table confirmed matching order.
None of those failures ran or changed a fixture, VM or service.

Review-repair tooling: initial default PowerShell read/search calls again
returned exit 0 with empty output; discarded, cause not established, and reads
were repeated successfully with explicit cmd.exe/login:false. `rg --files
--hidden -g AGENTS.md -g CLAUDE.md -g !.git` reported access denied for
`unsloth-tmp`; scoped searches of docs/wine found no additional instruction
files. `rg -n "^" ... -m ...` returned no matches under this runner; `rg -n .`
provided the intended source reads. Several cmd.exe searches accidentally
included a semicolon as part of a path (`registry.c;`, `object.c;`) or a `-m`
argument and failed with path/not-a-number errors; independent commands
without separators succeeded. Searches for `wine/server/security.c` and
`wine/Makefile.in` failed because those files do not exist; checks used
token.c/handle.c and tools/makedep.c instead. A quoted alternation search also
returned no matches; separate rg `-e` patterns found the fixture references.
A quoted multiword rg search split `review` and `repair` into path arguments
and failed with OS errors 2/123; a single-token `-F S12/S13` search succeeded.
Large combined output was truncated and relevant portions were reread with
bounded individual searches. All edits used apply_patch, with no C escapes
passed through a shell heredoc. No VM/session/service state was changed.
