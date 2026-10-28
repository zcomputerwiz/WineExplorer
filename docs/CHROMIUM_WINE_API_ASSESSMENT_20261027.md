<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium active-API requirements vs combined Wine — static assessment (WO-20261027-CHROMIUM-WINE-API-ASSESSMENT)

Date: 2026-10-27
Branch (root): `codex/chromium-wine-api-assessment` (cut from parent review commit `993faaa7`)
Assessed Wine base: **`fffa7de0df386ae16c4d47b1d0e450d8b7fbcbd3`**

This is a **purely static, read-only code assessment**. No VM, browser, Wine/probe
execution, build, or test run was performed. It maps the 27 prioritized rows of the pinned
Chromium source audit ([requirements](CHROMIUM_ACTIVE_API_REQUIREMENTS_20261027.md)) against
the *actual* combined Wine base `fffa7de`, classifies each row with one honest status, and ends
with at most 1–3 concrete fix candidates / next checks. It does **not** claim that the source
audit explains the actual killed page, and it does not diagnose the separately-owned
control-channel `violent_death` issue (Chromium's Win32 Mojo named pipe is a different layer
from Wine's internal client↔wineserver control socket).

## Source identity / build association (honest)

Verified read-only against local git objects of the wine fork:

- `fffa7de0df386ae16c4d47b1d0e450d8b7fbcbd3` — 2026-10-26 22:40:15 -0500, subject
  *"Merge token security history (ae5fec1) into accepted graphics/SID/registration history (cb1d117)"*.
  It is a **merge commit** with parents `cb1d117d` (graphics/SID/registration) and `ae5fec1e`
  (token security). Both are confirmed ancestors.
- The root's currently-pinned wine submodule commit `4315be83` **is an ancestor of `fffa7de`** —
  i.e. the assessment base is a strict superset / ahead of what the root pins today.
- The rejected ECN stub `d629841` is **not** in `fffa7de`'s ancestry (correctly excluded).
- The diagnostic child base `a91ceea` is **not** in `fffa7de`'s ancestry — so this base carries
  **no inert termination traces**; it is a clean API base, not the diagnostic build.

All citations below are to source at commit `fffa7de`.

## Scope and reading rules

- Target consumer: Windows x64 Chromium snapshot 156.0.8078.0 (source `7d98240`), headful
  `--app`, local loopback HTTP, ordinary sandbox/features/logging, actual VirGL. Rows are
  prioritized for functional rendering; a checked source branch only establishes what happens
  **if reached**, not that this snapshot executed it.
- Retained runtime evidence **R** (from the audit): parent reports combined Wine `fffa7de`
  launched browser/GPU/network/renderer processes; metadata probe 21/21 passed; displayed
  `RESULT_CODE_KILLED` / Aw Snap and the fixture HTTP remained empty. R proves processes
  appeared — **not** that each row's call executed, nor successful IPC, TCP navigation,
  rendering, or sandbox isolation. R applies to all rows; individual tests cited below are
  existing Wine unit tests (source-observed), not executions for this snapshot.
- Confidence vocabulary: **likely** = on an ordinary startup/consumer path but runtime
  reachability unproven; **conditional** = a feature/policy/backend must be selected;
  **unknown** = exact selection/default unresolved.

## Method

1. Read the parent policy, control-channel and ECN reviews, the requirements document, and the
   parent review of the audit.
2. Extracted the `fffa7de` source read-only (object read, no checkout) into a scratch tree and
   traced each row's Win32 wrapper through its `Nt*` syscall / information class into the Wine
   Unix/server object, handle, async and synchronization behavior, noting x64 specifics. Every
   claim cites `path/file.c:function():line` at `fffa7de`.
3. Classified each row with exactly one of the five statuses below; distinguished a real
   implementation from an exported-symbol-only or success-only stub; noted existing test
   coverage vs missing semantic contract, and abort-vs-log-and-continue consequences.
4. Spot-verified the headline claims directly against source (row 10 count stub, row 24 EGL
   absence) and confirmed the load-bearing test-function citations exist at `fffa7de`.

**Snapshot caveat:** the extraction covered the core process/token/section/pipe/IOCP/socket and
server directories plus the graphics export modules. **`dlls/wined3d` source was not extracted**,
so any behavior that terminates inside wined3d (final GL composite/present for rows 23–26) is
marked *runtime-inferred*, not source-observed.

## Status legend

| Status | Meaning |
|---|---|
| **sufficient with evidence** | Real implementation **and** an existing test covers this scoped contract. |
| **implemented unverified** | Genuine implementation exists but no test proves this specific semantic, or reachability/semantics unproven (high source confidence). |
| **partial stub** | Exported symbol / partial path exists but does not fully implement the required semantics (success-only, missing error code, wrong layout, no real effect). |
| **absent** | The API/object is not implemented at all in this commit. |
| **inactive unknown** | Present but conditional/deferred; condition and why it can't be confirmed active for this x64 target are stated. |

## Summary table (27 rows)

| Row | P | Requirement (short) | Key Wine path @ `fffa7de` | Status |
|---|---|---|---|---|
| 1 | P0 | Child launch + token (CreateProcessAsUserW, suspended) | `kernelbase/process.c:CreateProcessAsUserW():454` → `NtCreateUserProcess()` → `server/process.c:new_process():1094` | implemented unverified |
| 2 | P0 | Initial impersonation (SetThreadToken/RevertToSelf) | `kernelbase/security.c:SetThreadToken():864` → `server/token.c:security_set_thread_token():175` | sufficient with evidence |
| 3 | P0 | Restricted token alters effective access | `kernelbase/security.c:CreateRestrictedToken():624` → `NtFilterToken()` → `server/token.c:filter_token():1560`, `token_access_check():986` | sufficient with evidence |
| 4 | P0 | Token query / default DACL (two-phase) | `kernelbase/security.c:GetTokenInformation():705` → `NtQueryInformationToken():323` → `server/token.c:get_token_*()` | sufficient with evidence |
| 5 | P0 | PROCESS_CREATION_HANDLE_LIST inheritance | `kernelbase/process.c:CreateProcessInternalW():568` → `server/handle.c:copy_handle_table():361`, `inherit_handle():343` | implemented unverified |
| 6 | P1 | Security capabilities / jobs / mitigation | `server/token.c:create_lowbox_token():1689` (real); job machinery real; mitigations success-only | partial stub |
| 7 | P0 | Sandbox policy section create/map | `kernelbase/sync.c:CreateFileMappingW():1032` → `NtCreateSection()` → `server/mapping.c:create_mapping():1504` | sufficient with evidence |
| 8 | P0 | Cross-process section duplication | `server/handle.c:duplicate_handle():556` (type-agnostic, refcounted) | implemented unverified |
| 9 | P0 | Drop token + registry cache (RevertToSelf) | `kernelbase/security.c:RevertToSelf():856` → `server/token.c:security_set_thread_token():175`; `RegFlushKey()` no-op | sufficient with evidence |
| 10 | P0 | Process handle table / count | `ntdll/unix/process.c:1368` (ProcessHandleCount = **FIXME stub, returns 0**); `:1388` real table → `server/handle.c:req_get_process_handle_table():953` | partial stub |
| 11 | P1 | Handle type/name + closure (NtQueryObject) | `ntdll/unix/file.c:NtQueryObject():7938` → `server/handle.c:req_get_object_info():687`, `server/directory.c:req_get_object_type():604` | sufficient with evidence |
| 12 | P0 | Named pipe create/connect + ERROR_PIPE_CONNECTED | `kernelbase/sync.c:CreateNamedPipeW():1379`, `ConnectNamedPipe():1350` → `server/named_pipe.c:pipe_server_ioctl():1257` (`:1273`) | sufficient with evidence |
| 13 | P0 | Mojo overlapped read completion | `kernelbase/file.c:ReadFile():3624` → `NtReadFile()` → `server/named_pipe.c:pipe_end_read():1026` → `server/async.c:add_async_completion():529` | sufficient with evidence |
| 14 | P0 | Mojo write + full completion / ownership | `kernelbase/file.c:WriteFile():4027` → `NtWriteFile()` → `server/named_pipe.c:pipe_end_write():1056`, `wake_message()` | sufficient with evidence |
| 15 | P0 | IOCP registration + dispatch | `kernelbase/sync.c:CreateIoCompletionPort():1214`, `GetQueuedCompletionStatus():1251` → `server/fd.c:set_completion_info():3176`; `server/completion.c:remove_completion():327` | sufficient with evidence |
| 16 | P1 | Posted wakeups / broker job port | `kernelbase/sync.c:PostQueuedCompletionStatus():1303` → `server/completion.c:add_completion():263`; job: `server/process.c:set_job_completion_port():1918` | implemented unverified |
| 17 | P0 | Received-section validation (NtQuerySection) | `ntdll/unix/virtual.c:NtQuerySection():6638` → `server/mapping.c:get_mapping_info():1542` | implemented unverified |
| 18 | P1 | Section access downgrade (DuplicateHandle FILE_MAP_WRITE) | `server/handle.c:duplicate_handle():556` → `server/token.c:check_object_access():1265` (SD-driven) | implemented unverified |
| 19 | P0 | TCP nonblocking connect (WSAEventSelect/FD_CONNECT) | `ws2_32/socket.c:WSAEventSelect():3888`, `connect():1370` → `server/sock.c:2913`, `:2663` (EINPROGRESS→`WSAEWOULDBLOCK`) | sufficient with evidence |
| 20 | P0 | TCP receive/read readiness + EOF | `ws2_32/socket.c:recv():2886` → `ntdll/unix/socket.c:sock_recv():918`; `server/sock.c:recv_socket():3926` | sufficient with evidence |
| 21 | P0 | TCP async write (WSASend) | `ws2_32/socket.c:WSASend():3251` → `ntdll/unix/socket.c:sock_send():1186` (short-write emulation) | sufficient with evidence |
| 22 | P2 | Optional TCP tuning + UDP ECN | `ws2_32/socket.c:setsockopt():3318` — `SO_RANDOMIZE_PORT` FIXME no-op (`:3466`); `IPV6_TCLASS` absent→`WSAENOPROTOOPT`; RECV* ECN real+tested | partial stub |
| 23 | P1 | DXGI/D3D11 feature probing | `d3d11/d3d11_main.c:D3D11CreateDevice():287` → `dxgi/device.c:543` (wined3d GL backend) | sufficient with evidence |
| 24 | P1 | ANGLE EGL display init | `opengl32.spec`: **no EGL exports** at this commit | absent |
| 25 | P1 | DirectComposition availability | `dcomp/device.c:DCompositionCreateDevice3():2870` (real device; fork "DComp underlay" compositor) | implemented unverified |
| 26 | P1 | Output surface / presentation | `dxgi/swapchain.c:d3d11_swapchain_Present1():1186` → `wined3d_swapchain_present()` (final pixels runtime-inferred) | implemented unverified |
| 27 | P0 | Renderer bootstrap / inherited pipe usable in child | named-pipe client/server ends + `set_completion_info` on any overlapped fd; proxy test `ntdll/tests/pipe.c:3060-3134` | implemented unverified |

### Status counts

| Status | Count | Rows |
|---|---|---|
| sufficient with evidence | **14** | 2, 3, 4, 7, 9, 11, 12, 13, 14, 15, 19, 20, 21, 23 |
| implemented unverified | **9** | 1, 5, 8, 16, 17, 18, 25, 26, 27 |
| partial stub | **3** | 6, 10, 22 |
| absent | **1** | 24 |
| inactive unknown | **0** | — |
| **Total** | **27** | |

Editorial notes on two statuses (both moved *toward* the more conservative label, with the
reason stated): row 16's core posted-completion path is tested, but the **broker job-exit
notification** (a stated part of the contract) has no dedicated test → implemented unverified.
Row 27 is supported only by a strong cross-process-dup proxy test, not a direct
"inherited invitation pipe + first overlapped IO in child" test → implemented unverified.

## Per-row assessment

### Process launch & token (rows 1–6)

**Row 1 — Child launch + token (P0) — implemented unverified.**
`CreateProcessAsUserW():454` → `CreateProcessInternalW():568` (STARTUPINFOEX switch :646–719) →
`create_nt_process():259` (PS_ATTRIBUTE_TOKEN :312–319; thread always created with
`THREAD_CREATE_FLAGS_CREATE_SUSPENDED` :349–352) → `NtCreateUserProcess():683` (new_process req,
token + env vararg) → `server/process.c:new_process():1094` (token check TOKEN_QUERY|TOKEN_ASSIGN_PRIMARY
:1257; `create_process():593`). Suspension is real: child self-suspends at
`ntdll/unix/signal_x86_64.c:2954`; parent resumes unless CREATE_SUSPENDED (`kernelbase/process.c:782`).
*Evidence:* source-observed full path (token rights, env block `env.c:create_startup_info():2133`, std
handles, real process/thread handles). `advapi32/tests/security.c:test_create_process_token():7919` proves
token-rights enforcement + restricted-token child; `ntdll/tests/thread.c:test_NtCreateUserProcess():241`
proves basic suspended launch. **No test covers the combined CREATE_SUSPENDED + STARTUPINFOEX +
handle-list contract.** *Risk:* `PROCESS_CREATE_FLAGS_SUSPENDED` is set client-side (`:724`) but never read
server-side — correct today via the always-suspend path, but unpinned; a regression would let the child run
before broker setup (no test catches it). Failure → SBOX_ERROR_CREATE_PROCESS → **abort**. x64 n/a.

**Row 2 — Initial impersonation (P0) — sufficient with evidence.**
`SetThreadToken():864` → `NtSetInformationThread():2497` (case ThreadImpersonationToken :2515) →
`server/token.c:security_set_thread_token():175` (TOKEN_IMPERSONATE check :185–188). Every access check
runs against the thread token via `check_object_access():1265` (`current->token ?: current->process->token`).
*Evidence:* `advapi32/tests/security.c:test_duplicate_handle_access():7741` — SetThreadToken with a
restricted-derived impersonation token, access denied under it (:7822–7834), **restored after RevertToSelf**
(:7836). ImpersonateSelf/ImpersonateLoggedOnUser also covered. *Risk:* `NtImpersonateAnonymousToken()` is a
FIXME stub (`ntdll/unix/security.c:1073` → STATUS_NOT_IMPLEMENTED); named-pipe `FSCTL_PIPE_IMPERSONATE`
(`server/named_pipe.c:1312–1321`) impersonates a process-token duplicate, not the client token. The tested
broker path (SetThreadToken/ImpersonateSelf) succeeds; failure would be **abort**. x64 n/a.

**Row 3 — Restricted token alters effective access (P0) — sufficient with evidence.**
`CreateRestrictedToken():624` → `NtFilterToken():943` → `server/token.c:filter_token():1560` (privilege/group
removal, DISABLE_MAX_PRIVILEGE :1586–1595, LUA_TOKEN :1597–1614, restricted-SID merge/intersect :1616–1676).
Enforcement is real: `token_access_check():986` intersects granted access with ACEs matching restricting SIDs
(:1164–1230); deny-only groups (SE_GROUP_USE_FOR_DENY_ONLY) match only DENY ACEs via `token_sid_present():951`.
*Evidence:* `test_CreateRestrictedToken():5228` asserts deny-only group attrs, CheckTokenMembership false,
privilege removal; effective-access change proven by `test_duplicate_handle_access` and
`test_create_process_token_child():8005–8021`. No material gap. Failure would be **abort**. x64 n/a.

**Row 4 — Token query / default DACL (P0) — sufficient with evidence.**
`GetTokenInformation():705` → `NtQueryInformationToken():323` — fixed-size classes via info_len[] table
(:326–389, STATUS_BUFFER_TOO_SMALL on undersize); TokenUser :393–412; TokenGroups/LogonSid/RestrictedSids
:414–455; TokenDefaultDacl :560–580 — all filled from real server data (`server/token.c:get_token_sid():1824`,
`get_token_groups():1865`, `get_token_default_dacl():1944`). *Evidence:* `test_GetTokenInformation():1985` plus
two-phase TokenUser/TokenGroups/TokenDefaultDacl sequences (:1703–1977) prove size→data coherence. *Risk:*
unlisted classes hit default STATUS_NOT_IMPLEMENTED (`ntdll/unix/security.c:732–734`) — e.g. TokenCapabilities;
known-used classes (incl. TokenIntegrityLevel :623, TokenIsAppContainer :685) succeed. Querying an unimplemented
class would be **abort**. x64 n/a.

**Row 5 — PROCESS_CREATION_HANDLE_LIST inheritance (P0) — implemented unverified.**
`CreateProcessInternalW():568` (HANDLE_LIST attr :670–673) → `create_nt_process():259` adds
PS_ATTRIBUTE_HANDLE_LIST only with PROCESS_CREATE_FLAGS_INHERIT_HANDLES (:304–311) → `NtCreateUserProcess():683`
(rejects list without INHERIT_HANDLES :738–741; `alloc_handle_list():657`) → `server/handle.c:copy_handle_table():361`:
with a list, copies listed + 3 std handles at the **same index** (:375–388); `inherit_handle():343` skips
non-inheritable (:351) and does `dst[index] = *src` (same server object). So child entries reference the same
server objects; parent attribute-buffer lifetime is irrelevant. *Evidence:* source-observed same-index/same-object
copy; **no targeted test** for PROC_THREAD_ATTRIBUTE_HANDLE_LIST semantics. *Risk:* a listed handle lacking
RESERVED_INHERIT is **silently skipped instead of failing the create** (`inherit_handle():351`) — Windows would
fail with ERROR_ACCESS_DENIED; a misconfigured broker handle yields a child missing its control-channel/Mojo
handle → renderer crash on connect rather than a clean SBOX error (abort in child, parent logs). Unlisted handles
are correctly excluded. x64 n/a.

**Row 6 — Security capabilities / jobs / mitigation (P1) — partial stub.**
AppContainer: `create_lowbox_token():1689` (app_container :1714, low integrity label, capability SIDs :1718–1734) —
real. Jobs: `server/process.c:new_process():1094` validates JOB_OBJECT_ASSIGN_PROCESS + add_job_process loop;
machinery at `job_init():383`, KILL_ON_JOB_CLOSE in `job_close_handle():343`, `terminate_job():319`; wrappers in
`ntdll/unix/sync.c` (NtCreateJobObject :1407, NtSetInformationJobObject :1573 — limits + completion port real;
BasicAccounting/BasicProcessIdList real, BasicLimit/ExtendedLimit return **zeroed** structs :1546–1563). Mitigations:
`SetProcessMitigationPolicy():1383` is a FIXME returning TRUE; `PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY` is
validated but never applied (`:713–715`); COMPONENT_FILTER/WIN32K_FILTER/etc. are TRACE-only (`:702–708`) — silently
ignored, no error returned. *Evidence:* source-observed; job tests in `ntdll/tests/om.c` cover only name/type/
create-open — no lifetime/assignment/kill-on-close/accounting; no mitigation-policy tests. *Risk:* mitigations are
**success-only stubs** (Chromium believes DEP/ASLR/SEHOP were applied when they weren't) — silent, more lenient than
Windows' ERROR_NOT_SUPPORTED, **log-and-continue, never abort**. Component blocklist unenforced. x64 n/a.

### Sections & cross-process transfer (rows 7, 8, 17, 18)

**Row 7 — Sandbox policy section create/map (P0) — sufficient with evidence.**
`CreateFileMappingW():1032` → `NtCreateSection()` → `ntdll/unix/sync.c:NtCreateSection():2978` →
`server/mapping.c:create_mapping():1504` (anonymous branch :1131–1144: real backing fd). Map side:
`MapViewOfFileEx():289` → `NtMapViewOfSection():6354` → `server/mapping.c:map_view():1609` (access enforced via
`get_mapping_obj()` :1620; `view->fd = grab_object(mapping->fd)` :1638). The section is a real `struct mapping`
holding ONE fd; every mapper grabs a reference to that same fd and mmaps MAP_SHARED → genuinely shared, writable
(PAGE_READWRITE grants SECTION_MAP_WRITE), correctly sized (bounds-checked :1622–1629), refcounted lifetime.
*Evidence:* source-observed single-fd design; `ntdll/tests/virtual.c:1784,2421` and `kernelbase/tests/process.c:131`
exercise CreateFileMapping(PAGE_READWRITE)+NtMapViewOfSection (create+map+writable+size). The cross-process
byte-sharing property is source-observed (single shared fd), not asserted by a dedicated two-process test.
Failure → hard error → **abort**. x64 n/a.

**Row 8 — Cross-process section duplication (P0) — implemented unverified.**
`DuplicateHandle` → `server/handle.c:duplicate_handle():556`: for a section, the shared `struct mapping` is handed
to the target as a new process-relative handle with the requested mapped access (`map_obj_access()` :575;
`alloc_handle_entry()` :604); refcounting means closing the sender's handle does not destroy the object while the
target holds its handle. *Evidence:* source-observed type-agnostic path (high confidence); `kernelbase/tests/process.c:72,85`
cover DuplicateHandle but only DUPLICATE_SAME_ACCESS on process/NUL-file handles — **no test duplicates a SECTION
cross-process with read/write/query**. Failure → error/invalid handle → **abort**. x64 n/a.

**Row 17 — Received-section validation via NtQuerySection (P0) — implemented unverified.**
`ntdll/unix/virtual.c:NtQuerySection():6638` → `SERVER_START_REQ(get_mapping_info)` (access=SECTION_QUERY) →
`server/mapping.c:get_mapping_info():1542` (`reply->size = mapping->size`, `reply->flags = mapping->flags`). Client
fills `_SECTION_BASIC_INFORMATION` (`include/winternl.h:3666`): Attributes=flags (so the SEC_IMAGE bit 0x01000000 is
present iff image), Size=size, *ret_size=sizeof; length guard → STATUS_INFO_LENGTH_MISMATCH (:6647). This is a **real
implementation, not an export-only stub**; a pagefile-backed shared section has flags=SEC_COMMIT (no SEC_IMAGE) so
Chromium's "reject on SEC_IMAGE" correctly accepts it with a real size. *Evidence:* source-observed; **no test anywhere
calls NtQuerySection or references SectionBasicInformation** (grep across all `tests/`). A query failure or wrong size
→ Chromium REJECTS the region → sandbox policy load fails → **abort**. x64 n/a (`#ifndef _WIN64` value-mangling compiled out).

**Row 18 — Section access downgrade (P1) — implemented unverified.**
`duplicate_handle():556`: if requested access exceeds the source handle's access (:578), it consults
`check_object_access()` for both tokens (:580–581) → `server/token.c:check_object_access():1265`. Downgrade enforcement
is real but **SD-driven, not capped by the source handle's access** (matches Windows): `check_object_access` returns TRUE
(grants everything) when the object has no security descriptor (`if (!obj->sd) return TRUE`, :1276–1280); only with a
DACL/SDL present does it evaluate SECTION_MAP_WRITE. *Evidence:* source-observed; no test covers FILE_MAP_WRITE downgrade
on a section. *Risk:* you CAN get a writable dup from a read-only handle if the section carries no restrictive SD; you
CANNOT if it does. If Chromium creates the section with a default/no SD and expects the read-only source handle alone to
force failure, Wine would NOT deny it — a semantic mismatch (silent divergence). x64 n/a.

### LowerToken / handle table / closure (rows 9–11)

**Row 9 — Drop token + registry cache (P0) — sufficient with evidence.**
`RevertToSelf():856` → `SetThreadToken(NULL,0)` → `NtSetInformationThread(ThreadImpersonationToken)` →
`server/token.c:security_set_thread_token():175` (handle 0 ⇒ releases and NULLs `thread->token`). Effective-token
resolution (`thread_get_impersonation_token():1625`, gate `check_object_access():1265`) uses `thread->token ?:
process->token` — so reverting genuinely changes the effective token for all subsequent checks. Registry flush:
`RegFlushKey():2682` → `NtFlushKey()` → `server/registry.c:req_flush_key():2969` (validates, returns success; Wine has no
separate per-token registry cache, so it is a harmless no-op). *Evidence:* source-observed;
`advapi32/tests/security.c:test_impersonation_level():3128` and `test_duplicate_handle_access_child():7862` — under a
restricted thread token DuplicateHandle fails ACCESS_DENIED then succeeds after RevertToSelf (:7904–7905). This is the
semantic contract (revert really restores access), not "returns TRUE". No reachable failure path found. x64 n/a.

**Row 10 — Process handle table / count (P0) — partial stub.** ⚠️ *headline finding*
`GetProcessHandleCount()` → `NtQueryInformationProcess(ProcessHandleCount)` at `ntdll/unix/process.c:1368` is a
**client-side FIXME stub**: `memset(info, 0, 4); len = 4` — returns **count = 0 with SUCCESS** (FIXME at :1375). The
`ProcessHandleTable` case (`:1388`) is a **real** request → `server/handle.c:req_get_process_handle_table():953`, which
walks the target's live table and fills reply data with `obj_handle_t` values via `index_to_handle(i)` — i.e. a
**uint32 array of real, client-visible NT handle values**, ReturnLength = count*4; undersized/oversized →
STATUS_INFO_LENGTH_MISMATCH. This is the correct uint32-array layout (NOT the NtQuerySystemInformation process-snapshot
structure). *Evidence:* source-observed for both halves; targeted test `ntdll/tests/info.c:test_process_handle_table():2377`
(undersized ⇒ MISMATCH + required length; sufficient ⇒ SUCCESS, handles[0] > 0; NULL process ⇒ STATUS_INVALID_HANDLE) — but
`test_query_process_handlecount():2332` carries `todo_wine { ok(handlecount > 0) }` (:2371–2374), acknowledging the stub's 0.
*Risk:* if Chromium only uses the count as a hint and re-sizes from ReturnLength on MISMATCH, behavior is fine (log-and-continue);
if it CHECKs count > 0 or allocates the table buffer strictly from that count, the child **ABORTS** — runtime-inferred, depends on
Chromium's exact usage. See fix candidate #1. x64 n/a.

**Row 11 — Handle type/name + closure (P1) — sufficient with evidence.**
`ntdll/unix/file.c:NtQueryObject():7938` — ObjectBasicInformation → `server/handle.c:req_get_object_info():687`;
ObjectNameInformation (:7972) → `req_get_object_name():699` (real name via `get_full_name`; unnamed objects ⇒ success with
zero-length name); ObjectTypeInformation (:7010) → `server/directory.c:req_get_object_type():604` (real TypeName + counts).
Closure: `CloseHandle` → `NtClose()` → real server close. *Evidence:* source-observed — every object type has a
Windows-compatible name ("File" `server/file.c:51`, "Key", "Event", "Mutant", "Semaphore", "Section" `mapping.c:111`,
"Process", "Thread", "Token", "Job", "IoCompletion"); targeted test `ntdll/tests/om.c:test_query_object():1677` verifies
MISMATCH+length and SUCCESS for Basic/Name/Type with exact name content. *Risk:* object names carry Wine full-path prefixes
(e.g. `\Sessions\1\BaseNamedObjects\<name>`); if Chromium matches configured names by exact string rather than suffix, some
objects are skipped → leaked handle in the child (**log-and-continue**, not abort). x64 n/a.

### Mojo named pipe + IOCP + renderer bootstrap (rows 12–16, 27)

**Row 12 — Named-pipe create/connect + already-connected (P0) — sufficient with evidence.**
`CreateNamedPipeW():1379` → `NtCreateNamedPipeFile` → `server/named_pipe.c:create_named_pipe():1522`. DUPLEX→GENERIC_READ|WRITE
+ share R/W; byte mode ⇒ pipe_type/read_mode FALSE; overlapped ⇒ no FILE_SYNCHRONOUS_IO_NONALERT. Connect:
`ConnectNamedPipe():1350` → `NtFsControlFile(FSCTL_PIPE_LISTEN)` → `pipe_server_ioctl():1257`; already-connected ⇒
`set_error(STATUS_PIPE_CONNECTED)` at :1273. *Evidence:* source-observed; `ntdll/tests/pipe.c:411` asserts
`listen_pipe` on an already-connected server returns STATUS_PIPE_CONNECTED; `ntdll/tests/error.c:499` proves the
STATUS_PIPE_CONNECTED↔ERROR_PIPE_CONNECTED mapping; byte-mode create/read exercised by `read_pipe_test(PIPE_TYPE_BYTE)`
(:3200/:3206). Busy path real (no free listener ⇒ STATUS_PIPE_NOT_AVAILABLE :1430; instance cap ⇒ STATUS_INSTANCE_NOT_AVAILABLE
:1582; both map to ERROR_PIPE_BUSY). If the already-connected status were not exactly ERROR_PIPE_CONNECTED, Chromium's PCHECK
would fire → **abort**; Wine returns the exact status. x64 n/a.

**Row 13 — Mojo overlapped read completion (P0) — sufficient with evidence.**
`ReadFile():3624` → `NtReadFile()` with cvalue=overlapped when hEvent==NULL; pending ⇒ STATUS_PENDING → ERROR_IO_PENDING.
Server: `pipe_end_read():1026` queues into read_q, sets STATUS_PENDING; on data, `message_queue_read():895` (byte mode sums
avail) → `async_request_complete()` → `server/async.c:async_set_result():550` → `add_async_completion(...,apc_context=OVERLAPPED,
status,total)` :594. EOF/disconnect: `pipe_end_disconnect():399`; new read on disconnected end ⇒ STATUS_PIPE_DISCONNECTED (:1040).
*Evidence:* source-observed; `ntdll/tests/pipe.c:test_completion():1496` does overlapped NtReadFile on an IOCP-associated pipe and
asserts the completion's Information (bytes) and Status, incl. short-read STATUS_BUFFER_OVERFLOW (:1557) and full read (:1566).
*Nuance:* the dedicated IOCP tests use PIPE_TYPE_MESSAGE; a byte-mode overlapped read through an IOCP port has no standalone case
but flows through the identical `async_request_complete→add_async_completion` path (mode only changes framing) — runtime-inferred.
A missing/malformed completion would hang the Mojo pump → effectively **abort**; Wine posts exactly one completion per op. x64 n/a.

**Row 14 — Mojo write + full completion / ownership (P0) — sufficient with evidence.**
`WriteFile():4027` → `NtWriteFile()` with cvalue=overlapped; server `pipe_end_write():1056` queues a message with the write async
attached, sets STATUS_PENDING; completes when the reader consumes it (`wake_message(message,in_size)` :926/950) or buffer has room
(`reselect_write_queue():989`). *Evidence:* source-observed; `test_completion():1496` asserts overlapped WriteFile `num_bytes==sizeof(buf)`
and `test_queued_completion(port,&ov,STATUS_SUCCESS,num_bytes)` (:1531–1534) — a **full-count** completion. Wine reports the full in_size
on success and has no partial/short byte-pipe-write completion (a named-pipe write is buffered whole), matching Windows pipe semantics.
"Ownership released when write attempted" is a Mojo rule, not Win32 behavior (n/a to Wine). A non-full write completion would make Chromium
reject+disconnect → **abort**; Wine's full-count reporting avoids that. x64 n/a.

**Row 15 — IOCP registration + dispatch (P0) — sufficient with evidence.**
`CreateIoCompletionPort():1214` → `NtSetInformationFile(FileCompletionInformation)` → `server/fd.c:set_completion_info():3176`
(gated by `is_fd_overlapped()` :2113, sets fd->completion/comp_key). Dispatch: `server/async.c:add_async_completion():529` →
`add_completion(completion,comp_key,cvalue=OVERLAPPED,status,information=bytes)` :533; `create_async():330` binds the port at op creation.
Dequeue: `GetQueuedCompletionStatus():1251` → `NtRemoveIoCompletion` → `server/completion.c:remove_completion():327` (returns one
ckey/cvalue/status/information; blocks if empty). *Evidence:* source-observed; `ntdll/tests/pipe.c:test_completion():1496` asserts
NtRemoveIoCompletion returns value==OVERLAPPED ptr, correct Status and Information; `_test_no_queued_completion()` (:1465) asserts empty
port + timeout ⇒ FALSE+WAIT_TIMEOUT (empty dequeue ≠ a completion). KEY delivery proven by `ntdll/tests/sync.c:test_completion_port_scheduling():1016`
(PostQueuedCompletionStatus keys 1..5 + GetQueuedCompletionStatus(Ex) verifying key and LIFO wake order). One-completion-per-op confirmed
(cancelled/closed op still yields exactly one; cross-process cancel ⇒ STATUS_CANCELLED :3109–3114). A wrong/missing completion or key would
corrupt the Mojo IOCP pump → startup **abort**; no double-post observed. x64 n/a (cvalue round-trips as 64-bit apc_param_t).

**Row 16 — Posted wakeups / broker job port (P1) — implemented unverified.**
`PostQueuedCompletionStatus():1303` → `NtSetIoCompletion` → `server/completion.c:add_completion():263`. Job object: `struct
job.completion_port/completion_key` (`server/process.c:173–174`); association via `NtSetInformationJobObject(JobObjectAssociateCompletionPortInformation)`
→ `set_job_completion_port():1918`; child-exit notification `add_job_completion():196` fired as JOB_OBJECT_MSG_EXIT_PROCESS (:310).
*Evidence:* posted keyed completions + wakeup ordering are directly tested by `ntdll/tests/sync.c:test_completion_port_scheduling():1016`.
The job-exit hook is a short call into the same tested `add_completion` primitive and the full client→server chain is present (source-observed),
but there is **no dedicated job-completion-port test**. Hence implemented unverified for the broker-specific sub-contract. A regression there
would leave the broker's exit-wait stuck — blocks broker shutdown logic rather than aborting startup (**log-and-continue** for the pump). x64 n/a.

**Row 27 — Renderer bootstrap / inherited pipe usable in child (P0) — implemented unverified.**
The invitation pipes are real server objects with pseudo-fds supporting async (`create_pipe_client():1383`/`create_pipe_server():1356`).
`CreateNamedPipeW` sets OBJ_INHERIT on the created end when `bInheritHandle` (`sync.c:1410`); a child receives a normal handle to the same
pipe_end and overlapped IO on it is identical, and it can associate that inherited handle with its own port via the same `set_completion_info`
path (works on any overlapped fd in any process). *Evidence:* source-observed OBJ_INHERIT; nearest proxy test `ntdll/tests/pipe.c:3060–3134`
duplicates a duplex OVERLAPPED pipe handle across processes and performs overlapped NtReadFile on the duplicated handle — proving a pipe fd is
usable for overlapped IO from another process. **No dedicated "launch with inherited invitation pipe + first overlapped IO completes in the child"
test** → implemented unverified (strong proxy). If the first read/write could not complete in the child, renderer bootstrap stalls → effectively
**fatal**; the mechanism is present and `set_completion_info` operates on any overlapped fd regardless of owning process. The call-site ordering
itself (InitializeRenderer before launch callback) is Chromium-side and does not prove a specific Win32 API failure. x64 n/a.

### TCP sockets (rows 19–22)

**Row 19 — TCP nonblocking connect via WSAEventSelect (P0) — sufficient with evidence.**
`WSAEventSelect():3888` → `IOCTL_AFD_EVENT_SELECT` → `server/sock.c:sock_ioctl():2913`, which stores event+mask and sets
`sock->nonblocking = 1` (:2961) — the real Windows "event-select implies nonblocking" semantics. Then `connect():1370`
(synchronous=TRUE) → server case IOCTL_AFD_WINE_CONNECT :2663: unix connect() on an always-O_NONBLOCK fd (:1967, :2735);
EINPROGRESS → SOCK_CONNECTING (:2794); `synchronous && nonblocking` → STATUS_DEVICE_NOT_READY (:2796–2801) → WSAEWOULDBLOCK
(`NtStatusToWSAError():593`, mapping at :624). Completion: server polls POLLOUT while CONNECTING → SOCK_CONNECTED → posts
AFD_POLL_BIT_CONNECT (+WRITE) on POLLOUT / AFD_POLL_BIT_CONNECT_ERR on POLLERR|POLLHUP → signals the WSAEVENT. *Evidence:*
source-observed; `ws2_32/tests/sock.c:event_client():870` asserts exactly this contract (WSAEventSelect(FD_CONNECT)→connect→
err==WSAEWOULDBLOCK :891→event signaled→WSAEnumNetworkEvents FD_CONNECT ok); `test_connect_events():6843`; failure path in
`tests/afd.c:test_get_events():1897` (gated behind winetest_interactive). *Risk:* an immediate-success branch exists (if unix connect()
returns 0, success is returned before the nonblocking check, :2782–2790) → Chromium would hit NOTREACHED; runtime-inferred rare on Linux
loopback (nonblocking connect normally yields EINPROGRESS), but no test pins "never succeeds immediately". Matters only once navigation
reaches the network service. x64 n/a (native 64-bit uses afd_event_select_params_64).

**Row 20 — TCP receive/read readiness + EOF (P0) — sufficient with evidence.**
`recv():2886` → `WS2_recv_base():1078` → in-process fast path `ntdll/unix/socket.c:sock_ioctl():1766` → `sock_recv():918`;
server `recv_socket():3926`: nonblocking socket with no queued read async returns STATUS_ALERTED first (:3950–3964), ntdll does the real
unix recvmsg in `try_recv():790`; EWOULDBLOCK → STATUS_DEVICE_NOT_READY → WSAEWOULDBLOCK. EOF = recvmsg()==0 → STATUS_SUCCESS with 0 bytes
(`recv()` returns 0 at :2897). FD_READ re-arms per arrival (each recv attempt clears READ event state, :3970–3971). *Evidence:* source-observed;
`test_read_events():7176` (FD_READ fires per arrival, partial read re-arms, coexists with pending overlapped WSARecv); `event_client():942–965`
verifies a full 2048-byte pattern received intact across chunked (split) writes; `test_close_events():7382` asserts peer close → recv()==0.
*Documented divergence:* when the peer closes with data still buffered, Windows signals FD_CLOSE immediately while Wine signals it only after the
buffer drains (Linux POLLHUP post-drain; MSG_PEOF peek logic :1431–1462) — pinned by `check_events_todo(ctx, FD_CLOSE, ...)` at :7464. Chromium reads
until recv()==0 so an HTTP body EOF still terminates correctly; risk only if code waits for FD_CLOSE before draining (**log-and-continue**). x64 n/a.

**Row 21 — TCP async write via WSASend (P0) — sufficient with evidence.**
`WSASend():3251` → `WS2_sendto():1134` → fast path `ntdll/unix/socket.c:sock_ioctl():1795` → `sock_send():1186`; server `send_socket():4013`.
Key Windows-semantics emulation: on a nonblocking socket a short write is NOT reported partial — the remainder is copied into a new forced-async
system send (dup'd fd) and the call reports success with the FULL length (`ntdll/unix/socket.c:1217–1279`, "Windows never reports partial write in
this case"). Overlapped WSASend on a full buffer → STATUS_PENDING → ERROR_IO_PENDING; completion via WSAGetOverlappedResult. *Evidence:* source-observed;
`test_write_events():7078` (nonblocking 1 MB send returns either the full size or WSAEWOULDBLOCK, never a partial count :7113–7122; FD_WRITE re-signals
after the peer drains); `event_client():916–941`. *Risk:* the "queue remainder in background" path (:1246–1277) silently drops data with only an ERR() if
the re-queue fails (OOM) — a rare divergence from Windows AFD's bounded buffering; no WSASend-specific multi-WSABUF byte-accounting test under buffer
pressure (**log-and-continue**; failure would corrupt/lose request bytes → connection error, not crash). x64 n/a.

**Row 22 — Optional TCP tuning + UDP ECN (P2) — partial stub.**
`setsockopt():3318`: **SO_RANDOMIZE_PORT is a success-only stub** — `FIXME("Ignoring SO_RANDOMIZE_PORT\n"); return 0;` (:3466–3468); no effect, never fails.
IP_TOS set is real → unix setsockopt(IPPROTO_IP, IP_TOS). Receive-ECN enable options are real: IP_RECVTOS (:3624–3630) and IPV6_RECVTCLASS (:3741–3747)
(DGRAM/RAW type-checked). **IPV6_TCLASS (send-side TOS on IPv6) has NO case** in ws2_32 setsockopt/getsockopt → falls to default → WSAENOPROTOOPT
(:3770–3773). `WSAIoctl():2472`: SIO_KEEPALIVE_VALS is real (SO_KEEPALIVE + TCP_KEEPIDLE/INTVL with ms→s rounding); other unimplemented codes → WSAEOPNOTSUPP.
*Evidence:* the ECN receive/query/ancillary-data contract IS real and end-to-end tested for the RECV* variants: `test_ipv4_cmsg():2823` (IP_RECVTOS → cmsg
IPPROTO_IP/IP_TOS :2905–2924) and `test_ipv6_cmsg():2930` (IPV6_RECVTCLASS → cmsg IPV6_TCLASS :3016–3034); keepalive in `test_keepalive_vals():5899`.
SO_RANDOMIZE_PORT and the missing IPV6_TCLASS have no targeted test. *Risk:* if SetRecvTos literally sets IPV6_TCLASS on an IPv6 socket, that call fails with
WSAENOPROTOOPT and Chromium logs/reports it — UDP continues without ECN marking; accept-and-ignore is fine for tuning, but this specific option is **absent, not
emulated**. SO_RANDOMIZE_PORT is a no-op that always succeeds so "ignore failure" never even triggers. All **log-and-continue**; P2 non-blocking for page load. x64 n/a.

### Graphics (rows 23–26) — conditional; backend selection unknown

Backend at this commit (source-observed): D3D11/DXGI run on the modern **wined3d GL backend** (`dlls/d3d11/Makefile.in`
IMPORTS wined3d, EXTRAINCL VKD3D). The fork adds a custom **"DirectComposition underlay" compositor**
(`include/wine/dcomp_underlay.h`, `IWineDCompSwapChain`, `wined3d_swapchain_set_target_scene()`) — dcomp scenes are
composited with real GL inside wined3d (cf. commit `571e907a` "Bridge opaque cross-process Chromium surfaces to native wexcomp").

**Row 23 — DXGI/D3D11 feature probing (P1) — sufficient with evidence.**
`D3D11CreateDevice():287` → `d3d11_create_device():145` → `DXGID3D10CreateDevice()` → `dxgi/device.c:dxgi_device_init():487` →
`wined3d_device_create(..., HAL, feature_levels)` :543–546. Factory requires a live GL context via `wined3d_create(0)`
(`factory.c:794`, else DXGI_ERROR_UNSUPPORTED); adapters from `wined3d_get_adapter_count()`; descriptors from
`wined3d_adapter_get_identifier()` (real GL-driver vendor/device IDs); capability gate `dxgi_adapter_CheckInterfaceSupport():213`
returns DXGI_ERROR_UNSUPPORTED when max_feature_level < 10_0. Obtained level reported honestly (`d3d11_device_GetFeatureLevel():5940`).
*Evidence:* in-tree targeted: `d3d11/tests/d3d11.c:test_create_device():2109` (obtained feature level equals the device's actual supported
level — the "honestly-fake-but-consistent" contract), `dxgi/tests/dxgi.c:test_adapter_desc():975`, `test_check_interface_support():1252`.
Known semi-stubs: CreateSoftwareAdapter E_NOTIMPL, WARP→hardware fallback with FIXME, EnumWarpAdapter returns a hardware adapter,
CheckFeatureSupport unconditionally claims PRESENT_ALLOW_TEARING=TRUE. *Risk:* if host GL is absent/broken, CreateDXGIFactory1 fails → whole
D3D11 route unavailable (Chromium falls back to its next route — **fallback, not abort**); actual feature level (max 11_1 per wined3d) depends on
the host GL driver (runtime-inferred). Gating condition: a working host GL context (VirGL/llvmpipe).

**Row 24 — ANGLE EGL display init (P1) — absent.** ⚠️ *headline finding*
At this commit `opengl32.dll` exports **no EGL functions at all**: `dlls/opengl32/opengl32.spec` contains only legacy GL 1.x thunks + WGL, and
`Makefile.in` compiles only `thunks.c unix_thunks.c unix_wgl.c wgl.c` (no main.c). Whole-tree grep for EGLAPI/eglGetDisplay/eglGetProcAddress finds
only header typedefs. No implementation of eglInitialize/eglGetDisplay/eglCreateWindowSurface exists in the tree. *Evidence:* source-observed (two
independent files agree; grep-verified). *Caveat:* the snapshot is a curated subset, so an EGL implementation in a module outside the extracted set
would be invisible — but upstream convention places EGL in opengl32 and the audit's file map points there. *Risk:* `GetProcAddress(opengl32,
"eglInitialize")` returns NULL → Chromium treats it as "no display" and moves to the next type — a **clean, honest failure (fallback), not a harmful
success-only stub**. Key unknown before elevating priority: whether Chromium's ANGLE build on this target actually resolves `egl*` from opengl32 at all
(ANGLE typically bundles its own libEGL that uses D3D11/WGL underneath — if so, Wine's opengl32 EGL is not on the critical path; runtime/build-dependent).
If a Wine-EGL route *is* required it must be implemented from scratch — the one genuinely missing graphics primitive here.

**Row 25 — DirectComposition availability (P1) — implemented unverified.**
`DCompositionCreateDevice3():2870` → `dcomp_device_create():2830` allocates a real device with vtbls for IDCompositionDevice/2/3 +
IDCompositionDesktopDevice; module is built, export is real stdcall in dcomp.spec (all other dcomp exports are @ stub). The device is not a
placeholder: `Commit()` validates the pending visual tree and pushes a layer scene to the swapchain via `IWineDCompSwapChain_set_target_scene` into
wined3d; CreateTargetForHwnd + visual ops are real state machines. Transform *objects*, surfaces and effects are E_NOTIMPL stubs. `DCompositionCreateDevice3`
ignores its rendering_device argument (accepts anything) — harmless for availability, diverges from Windows validation. *Evidence:* source-observed; **no
dcomp test exists** in the tree. Whether Commit yields visible compositing on a host is runtime-inferred (final GL composite lives in wined3d, not extracted).
*Risk:* the availability contract (module + export + non-NULL device) is satisfied at source level → Chromium's setup return proceeds past dcomp detection.
Gating condition: working host GL. If Chromium uses transform objects or DComp surfaces/effects (stubs here), per-feature E_NOTIMPL failures mid-frame-setup —
a semantic gap, not an availability one.

**Row 26 — Output surface / presentation (P1) — implemented unverified.**
EGL half: **absent** (row 24). DComp/wined3d half (the route that can work here): `IDXGISwapChain::Present1` →
`dxgi/swapchain.c:d3d11_swapchain_Present1():1186` → `d3d11_swapchain_present_locked():696`: real present — retains the presented frame into history
textures via GL blits, then `wined3d_swapchain_present()` to the target hwnd with dcomp-aware flags; occluded windows return DXGI_STATUS_OCCLUDED. Resource
lifetime: history textures refcounted, cleaned on release. Synchronization: SRWLOCK + per-swapchain present_lock + wined3d_mutex. DComp scene push via
`wined3d_swapchain_set_target_scene()` with same-device enforcement; repaint re-draws retained frames on WM_WINE_DCOMP_REPAINT. *Evidence:* source-observed at
the dxgi layer; **final visible pixels are runtime-inferred** (actual GL composite/present happens inside wined3d, not extracted). No targeted test for the
composition/underlay present path (dxgi tests cover plain swapchain present, e.g. `test_swapchain_present():5390`). *Risk:* only the DComp/wined3d route can
produce pixels here; configuration needed to confirm: host GL working **and** Chromium selecting the DirectComposition output device (backend selection unknown).
Present is synchronous w.r.t. the call; if Skia's completion callback depends on async frame events, the only hook present is GetFrameLatencyWaitableObject —
callback semantics runtime-inferred. dcomp scene limits return E_NOTIMPL/E_INVALIDARG for complex visual trees.

## Cross-cutting observations

- **The P0 startup/IPC/TCP Win32 surface is largely solid and well-tested.** 14 of 27 rows are *sufficient with
  evidence*: process launch with token/suspend, impersonation and restricted tokens (with real access enforcement),
  GetTokenInformation, sections + cross-process duplication, the entire Mojo named-pipe + IOCP transport (rows 12–15),
  and nonblocking TCP connect/read/write (rows 19–21). This is consistent with the control-channel result: the renderer's
  abnormal death was **not** caused by a broken Win32 IPC primitive in Wine — the Mojo pipe/IOCP layer is genuinely complete.
- **The concrete gaps are narrow.** One clear P0 defect (row 10, `ProcessHandleCount` returns 0), a few success-only
  security/tuning stubs that do not block the page (row 6 mitigations; row 22 SO_RANDOMIZE_PORT / IPV6_TCLASS), and one absent
  graphics primitive (row 24 opengl32 EGL, likely not on Chromium's ANGLE path).
- **The "implemented unverified" rows are real-but-untested**, not missing: child-launch combined contract (1), handle-list
  inheritance (5), section duplication (8), NtQuerySection (17), section downgrade (18), DComp (25), present (26), renderer
  bootstrap (27). The main risk there is an untested semantic contract, not a missing implementation.
- **Layout note for future work:** at `fffa7de` the section object is `struct mapping` in `server/mapping.c` (no
  `server/section.c`); named pipes are `server/named_pipe.c`; IOCP/async dispatch is `server/async.c` + `server/completion.c`;
  process creation is `dlls/kernelbase/process.c`; token wrappers are `dlls/kernelbase/security.c` + `dlls/advapi32/security.c`.

## Ranked fix candidates / next checks (at most 1–3)

**#1 (recommended) — Implement the `ProcessHandleCount` count (row 10).**
- *Concrete mismatch:* `NtQueryInformationProcess(ProcessHandleCount)` at `dlls/ntdll/unix/process.c:1368` is a FIXME stub
  returning count = 0 with SUCCESS, while the sibling `ProcessHandleTable` handler (`server/handle.c:req_get_process_handle_table():953`)
  already enumerates the real table.
- *Real behavior change:* return the actual current-process handle count (the same value the table handler derives from
  `process->handles`), instead of a hard-coded 0. Small, well-scoped; keeps the existing uint32-array table contract intact.
- *Reachable consumer:* Chromium's `HandleCloserAgent::CloseHandles` (handle_closer_agent.cc:155–193) calls GetProcessHandleCount then
  NtQueryInformationProcess(ProcessHandleTable); parent review confirmed this call site and its failure branch.
- *Existing focused regression + acceptance:* promote the existing `ntdll/tests/info.c:test_query_process_handlecount():2332`
  from `todo_wine { ok(handlecount > 0) }` to a hard assertion; assert count>0 for a process with handles, count consistent with the
  ProcessHandleTable length/4, and correct handling of the no-handles edge (returns 0). Negative/error behavior: NULL process → STATUS_INVALID_HANDLE.
- *Discriminating next check (reachability of the abort):* confirm from Chromium source whether the count is used as a CHECK(count>0) / strict
  allocation basis (→ today's 0 would abort the child) or only as a hint with re-sizing from ReturnLength on STATUS_INFO_LENGTH_MISMATCH
  (→ behavior already fine). This determines severity, not whether the stub should be fixed.

**#2 (next check) — `PROCESS_CREATION_HANDLE_LIST` fail-fast semantics (row 5).**
- *Mismatch:* a listed handle lacking RESERVED_INHERIT is silently skipped (`server/handle.c:inherit_handle():351`) rather than failing the
  create with ERROR_ACCESS_DENIED as Windows does; a misconfigured broker handle would yield a child missing its Mojo/control handle.
- *Next check:* verify whether Chromium ever lists a non-inheritable handle (if not, this is a latent divergence, not an active failure). If it
  can, matching the fail-fast error is a small, targeted change with a focused two-handle create test. Lower priority than #1 because the normal
  (correctly-inheritable) path already works.

**#3 (next check) — Confirm whether opengl32 EGL is on Chromium's critical path (row 24).**
- *Not recommended as an implementation here:* implementing full EGL from scratch is a large effort, not a small supported fix, and it is P1/conditional.
- *Next check:* determine whether the ANGLE build in this target resolves `egl*` from `opengl32.dll` or bundles its own libEGL over D3D11/WGL. If the
  latter (the common case), row 24 is not a blocker and no Wine change is warranted; if the former, it becomes a real gap to plan separately.

Rows 6 and 22 (mitigation-policy and socket-option success-only stubs) are **not** ranked as fixes here: they are silent, log-and-continue,
non-page-blocking security/tuning-surface gaps — flagged for parent awareness, but implementing them is out of scope for a "small supported fix"
and does not advance real Chromium functionality.

## Limitations and honest unknowns

- **No execution.** Every status is source-based (or an existing unit test's source). Nothing here proves this snapshot reached a call site,
  completed IPC/TCP, or rendered pixels. A 21/21 metadata result or a checked source branch does not establish effective sandbox access, channel
  completion, HTTP response, or pixels.
- **Reachability is triage, not diagnosis.** Priority reflects an inspection order; the runtime has not identified which boundary (if any) actually
  failed. This assessment does **not** claim the source audit explains the actual killed page, and it does not re-diagnose the control-channel issue.
- **Snapshot coverage.** `dlls/wined3d` was not extracted; final GL composite/present for rows 23–26 is runtime-inferred. ANGLE's vendored source is
  separately versioned and was not inspected.
- **Per-child policy/backend unknown.** Exact selected sandbox policy, whether LowerToken/closer or each channel/TCP call was reached, the chosen
  renderer/GPU/presentation path, and feature/Finch defaults are unresolved; conditional rows state the configuration evidence needed to elevate them.
- Citations were produced by focused per-row source tracing at `fffa7de`; headline claims (rows 10, 24) and the load-bearing test citations were
  independently re-verified against the extracted tree.

## Deliverables & branch state

- New files on `codex/chromium-wine-api-assessment` (no other edits): this report and
  [CHROMIUM_WINE_API_ASSESSMENT_20261027.json](CHROMIUM_WINE_API_ASSESSMENT_20261027.json) (machine-readable table).
- No VM, browser, Wine/probe execution, build, or implementation patch was performed. No edits to control-channel source/builds/runners/private
  adapters. No master merge; no other worker/VM changes.
