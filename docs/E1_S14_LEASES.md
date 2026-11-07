<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# E1-S14: managed hive leases, persistence and recovery

2026-11-07, implementation and static review only; no build, generator, Wine,
WSL, VM, fixture or runtime test was run. This implements section 7 row 14 of
[E1-S](E1_S_STANDARD_USERS.md), following [S12](E1_S12_PROFILE_STORAGE.md),
[S13](E1_S13_HKU_LOAD.md), [S13 lab history](E1_S13_LAB_FAILURE.md),
[S1 kind 6](E1_S1_CONTRACTS.md) and [S10](E1_S10_TOKEN.md). Historical S13
acceptance in the decision log does not validate these new changes.

The implementation remains disabled: Linux plus WEX_E1S13_HKU_TEST,
WEX_E1S10_TOKEN_TEST and WEX_E1S6_TRANSPORT_TEST are required by the request
handler. Automatic PE hooks still require WEX_E1S13_HKU_TEST in both ABIs.
WEX_E1S14_CRASH_TEST is an additional, independent lab-only define for crash
injection and the synthetic ownership-transfer driver. No default Makefile,
installer, capability advertisement, environment switch or production broker
activates this work. S18 authenticated integration remains absent.

## Survey and implementation evidence

| Area | Source evidence and consequence |
| --- | --- |
| Prefix startup and save branches | `wine/server/registry.c:210` defines save-branch metadata. The first three slots retain their existing meaning; eight managed slots add an explicit manager pointer. `:3070` registers a managed branch only after storage/security/S10 validation and successful result-handle allocation. `:4028` routes those branches directly to the managed descriptor saver, before any stock path, merge or system-group handling. |
| S13 unload stub | Replaced by `e1s_unload_hive` at `wine/server/registry.c:3195`; the old e1s_profile_unload_s14 symbol is removed. `:4214` flushes managed keys through the same durable saver. Generic NtUnloadKey and client-file NtSaveKey cannot unload/save a managed branch. |
| Key lifetime | `wine/server/registry.c:110,950,1393` maintains an intrusive list of every managed key, including detached/deleted keys, through successful initialization and destruction. `:3209` examines actual object handle_count, notifications and wait references, rather than only the root or linked descendants. Duplicated/inherited handles count naturally. |
| Process/token lifetime | `wine/server/process.c:728` registers every successfully created process before its first thread can appear; `:553` marks final native death. `:1122` scans these retained primary-token and thread-impersonation-token references. `:1782` checks primary replacement before changing the token. Retained dead process/token handles do not count as running users. |
| S10 binding | `wine/server/token.c:2868` obtains request_id and server_gen from the retained, revalidated consumed context. The storage reference supplies neither. The original S10/S6 broker and one-shot load gates remain. |
| Lease allocation | `wine/server/registry.c:2846` allocates all native S1 kind-6 fields. `:2967` reserves the SID before filesystem work and rolls back only that reservation on failure. `:2572` supplies a server-internal ownership-transfer interface for S18. |
| Storage locks/recovery | `wine/server/wex_profile_storage.c:197,215,605,736,948` implements stable SID exclusion, transaction detection across published incarnations of that SID, and S12 provisioner cooperation. `:782` reopens/rechecks the same prefix/profile/hive/lock identities. |
| Durable publication | `wine/server/wex_profile_storage.c:830,854` compares the held load snapshot and performs the anchored save transaction. `wine/server/registry.c:3127` emits a bounded relative managed hive; `:3177` retains/poisons the lease on an unsafe or incomplete save. |
| Cache lifetime | `wine/dlls/kernelbase/registry.c:73,95,121,630` adds generation to the existing effective-identity query and cache key. `wine/server/registry.c:3101` creates detached proxies; `:2056` resolves them only against the current account/SID generation. No proxy owns a hive/key/view reference. |
| Replacement file security | `wine/server/e1s_profile.c:231` applies, fsyncs and queries the canonical file descriptor on the temporary hive. `:253` deduplicates registrations on reload. `:41` retires unlinked inode pins only when `wine/server/fd.c:1163` proves both the inode-object references and anonymous/imported fd references are gone. An indeterminate anonymous fstat retains the pin. |

## Lease and serialization contract

The table is local to the wineserver's pinned config_dir_fd, hence to one
prefix/server instance. Active lookup compares the canonical S4 incarnation
key and exact mapped SID; an active different key with the same SID also
conflicts. At most eight hives are active. A bounded 4096-entry SID generation
table survives successful unloads and failed setup until this server exits.

The native `wex_profile_lease_reserved` record now has nonzero lease_id,
request_id, server_gen, prefix_id, account_key, user_sid, map_gen,
profile_generation and state. S12 provision results still leave it entirely
zero. It is neither an IPC capability nor a new client-submittable record.

* lease_id is LE server generation plus a checked, server-global allocation
  sequence, unique across SIDs and failed attempts; overflow denies.
* request_id/server_gen come from S10's retained authoritative context. The
  lab context still uses S10's synthetic fixture producer; this does not prove
  the missing S18 evidence/authority chain. Changed server generations deny.
* prefix_id is a nonzero random instance-scoped identifier associated with the
  pinned prefix. S12's manifest/device/inode/mount checks bind the storage.
* account_key/SID/map_gen are the validated S4/S10 snapshot. profile_generation
  names S12's completion generation. The separate SID cache generation changes
  on unload/setup rollback; it is not a replacement storage manifest.

State 1 owns setup. Creation or primary replacement checks that the managed
hive is still loaded, but does not manufacture a commit. State 2 transfers
ownership to server process/descendant tracking through
`e1s_profile_commit_process(process, lease_id, request_id)`: the trusted S18
producer must already have retained/verified the suspended child and performed
its other commit/guard checks. This function does not resume a process or
authenticate serialized bytes. The lab-only TEST_TRANSFER operation exercises
this interface on the endorsed broker's actual direct child. State 3 is set
only at release/owned setup rollback, after closing the storage resources on
successful unload. Failed durable saves retain the lease and its locks.

Lock order is S12 prefix `.profile.lock`, stable SID-directory `.hive.lock`,
then current hive-inode flock. Acquisition is nonblocking. Loads and S12
provisioners acquire the prefix lock before the SID lock. The server retains
the SID lock across every save/rename and until unload; S12 also checks it
before reuse or provisioning another incarnation. The replaceable user.reg
inode is an additional lock, not the authoritative lifetime lock. Save-time
prefix-lock checks are nonblocking, so reverse reacquisition cannot deadlock a
provisioner. A conflict before transaction creation is retryable; interrupted
or uncertain publication is retained and denied.

Server dispatch serializes reservation, load, save and unload. No request or
token/process publication interleaves one of these synchronous operations.
Requests queued behind a completed operation linearize afterwards; there is
no asynchronous worker that may publish into a concurrently unloading SID.
Explicit loading/unloading phases deny if later asynchronous work ever exposes
them. This is serialization at server dispatch, not a claimed measurement of
arrival order on concurrent client sockets.

## Last-user and open-handle rule

Unload denies while any retained, not-finally-dead process has a primary of
the exact SID, or any of its threads has an impersonation token of that SID.
Pending first-thread startup, suspended processes, descendants, terminating
processes and native SIGKILL/reaping delay count. Unknown primary identity also
denies. Token object refcount alone is unsuitable: broker handles, contexts,
duplicates and dead process query handles retain token objects without running
as that user. No parent PID heuristic or logon-LUID-only comparison is used.

After that check, every managed key object is checked for open handles and
notification/wait references, including the user root, actual classes, alias,
union view, descendants and deleted keys detached from the tree. Ordinary key
handles must be closed. No handle is silently revoked or closed remotely.

An explicit broker unload releases immediately when safe. The registry timer
also retries state-2 last-user draining after two consecutive no-user sweeps
(normally 30–60 seconds). New managed process creation resets that observation.
Busy key handles retain the hive for later retries. Setup-only state-1 leases
require S18's explicit rollback release; they cannot expire underneath pending
child setup. Shutdown uses the same durable saver; server death invalidates all
old lease IDs and the OS releases the locks.

Managed LoadUserProfile returns a broker-opened proxy root. UnloadUserProfile
passes that handle as the UNLOAD context; the server checks exact account,
generation and user-root kind before unloading. It closes the caller's proxy
locally only after successful unload. The private broker API also allows a
NULL context when no userenv handle is involved.

## Durable save and interrupted state

The saver takes no native path, client file handle, SID or prefix selector.
It rechecks S12's pinned prefix, mapping, canonical stored label, complete
manifest, skeleton, private modes, owner, mount and hive/lock inode identities.
It compares the read-time hive device/inode, size, mode, owner/group, link count,
mtime and ctime, both before the transaction and immediately before rename.
Replaced files or observed in-place edits refuse saving; they are not adopted
as a new authority. The normal same-UID native-tampering limitation remains.

1. Exclusively create `.e1s14-state` in the held profile directory; fsync the
   marker and directory before making a temporary hive.
2. Exclusively create `.e1s14-save` there, acquire its inode flock, serialize
   only this user tree, apply/query its explicit file SD, fsync file and
   directory, then recheck source/temp/marker and storage-chain identities.
3. Rename the temp to `user.reg` in that same directory and fsync the directory.
4. Remove only this transaction's verified marker and fsync the directory again.
   Transfer the held hive descriptor/snapshot; explicitly unlock the old open
   description because S13 security pins may duplicate it.
5. Only after durable success bump the SID generation, detach/delete registry
   state, unregister the managed save branch, release the lease and its locks.

Unloading and RegFlushKey, periodic saving and shutdown all use this path.
Managed branches bypass stock client paths, merging and in-place machine-hive
saving. The synthetic KEY_VOLATILE isolation remains for Bob's save branches;
KEY_E1S_PERSIST selects actual persistent managed keys. Explicit volatile keys
are omitted. Class strings and values round-trip; no security/link/global
directive can nominate authority. The current S13 format cannot restore custom
per-key SDs or registry symbolic links: saving those states refuses and retains
the hive, rather than reporting lossy durability. Bounds remain 1 MiB, 16384
registry keys and depth 64. This deliberately limited subset needs runtime
confirmation; it is not general registry-format parity.

On the next load of a SID, any state marker or save temp in any published
incarnation of that SID returns WEX_PROFILE_RECOVERY / STATUS_REGISTRY_CORRUPT,
before reading/publishing registry data. S12 reuse also denies. Nothing chooses
the temp, merges files, repairs permissions or deletes recovery data. The
operator must retain and inspect all surviving files with the server stopped.
There is intentionally no automatic repair tool or claim of power-loss proof.

## Client generation and protocol

Protocol **978 -> 979** changes existing request **325** `load_e1s_profile`;
no request is appended. Request remains 32 bytes. Reply is now 24 bytes:
header 0..7, hkey:u32 at 8, managed:u32 at 12, generation:u64 at 16. QUERY returns
managed identity independently of registration: generation is zero when absent
or poisoned. Successful root opens return the current nonzero generation.
UNLOAD and test controls return no key, managed=1, generation=0.

Hand-edited protocol.def, request_handlers.h, request_trace.h and
server_protocol.h agree on that layout. Existing request order, tables/unions,
326 request count, 88-byte union/generator maximum and S10 raw carrier remain.
Actual tools/make_requests reproduction is still required in wexbuild.

New private PE `NtLoadWexProfileEx` adds an optional ULONGLONG generation output;
`NtLoadWexProfile` keeps its existing ABI and delegates to it. Ex has the matching
ntdll.spec and __WINESRC__ winternl.h declarations. All outputs clear on failure;
malformed successes deny. No syscall or WoW64 thunk-table slot is added.

Operations 1..6 retain their roles; OPEN now returns the userenv-compatible
proxy. CACHE_CURRENT=7 and CACHE_CLASSES=8 obtain cache proxies under the same
identity/DACL/MIC checks. TEST_CRASH=9 and TEST_TRANSFER=10 require the separate
lab hook define and exact broker gate; normal servers deny every operation.

Kernelbase includes generation in its existing SID/TokenId/ModifiedId lookup.
This adds eight reply bytes to S13's existing QUERY and **no additional server
round trip per registry call**. The server validates a proxy's copied account
and generation again at actual registry use, closing the query/use race.
Proxies hold no real hive/root references. New real key handles obtained through
them still block unload. Invalidated cache entries are retired until process
exit, bounded at 256 identity/generation entries; they are not remotely closed
or reused while another thread may still have copied a handle number.

## Status and test handoff

| Condition | NTSTATUS |
| --- | --- |
| Already loaded SID, including another incarnation | OBJECT_NAME_COLLISION |
| Load/unload observes loading | DEVICE_NOT_READY |
| Load/unload observes unloading | DELETE_PENDING |
| External S12/SID flock conflict | SHARING_VIOLATION |
| Live/pending/terminating exact-SID process or impersonation | RESOURCE_IN_USE |
| Open actual key, detached descendant or notification/wait | CANNOT_DELETE |
| Double unload / unregistered managed profile | OBJECT_PATH_NOT_FOUND |
| Interrupted/poisoned state | REGISTRY_CORRUPT |
| Observed source identity replacement/edit during save | FILE_INVALID |
| Other unconfirmed durable-save failure | REGISTRY_IO_FAILED |

Lab denials have distinct E1S14 site/status audit lines. No password, secret,
path chosen by a client or record payload is logged. Shutdown logs managed
status directly instead of attributing an ambient errno to it.

`scripts/test_hku_load.sh` still enters the S10/S6 owned root fixture through
`scripts/test_e1s10_token.sh` and `scripts/lib/e1s13-fixture.sh`. The existing
S13 probe wrapper includes the extended `e1s13_hku_checks.h`. Both broker ABIs
pair with the opposite-ABI ordinary Bob peer. The broker runs as the system
role in the **machine prefix**; peers run in **Bob's prefix**. Registry checks
use their shared wineserver. Native hive/hash assertions use Bob's S12 path,
never a broker C: path or the machine profile's user.reg. No S15 environment or
file-ownership claim is inferred from the broker's inherited environment.

Added, all unrun here:

* Live suspended Alice process denial, synthetic transfer to server ownership,
  final process death followed by successful explicit unload.
* Linked and deleted/detached descendant-handle denial with a distinct status.
* Persistent session value, native saved-file assertion, subsequent new-context
  load observing that value, second successful unload and double-unload denial.
  The first unload uses the private operation; the second uses the actual
  LoadUserProfileA/UnloadUserProfile proxy-handle path.
* Old proxy denial both after unload and after reload. Reuse the **same prior
  impersonation token** after reload to exercise kernelbase's generation check,
  without relying on a different TokenId to miss the cache.
* Actual server _exit(86) after marker fsync, temp fsync and rename/directory
  fsync. Require both broker arm and server checkpoint evidence, not a timeout.
  The next server must deny loading, preserve hashes of every surviving hive,
  marker/temp, and publish no HKU root. Only fixture-owned processes are stopped.
* Normal-server `advapi32/tests/wex_hku.c` covers all ten operations, both PE
  wrappers, generation clearing and existing malformed-length cases. Ordinary
  and filtered-admin lab peers also deny all management/control operations.

In clean, exclusively owned wexbuild as root: regenerate/compare all protocol
artifacts, rebuild normal and lab WoW64 runtimes, then use the existing
WINE_BUILD_DIR/WINE_NORMAL_BUILD_DIR invocation of test_hku_load.sh. The harness
pins 979, forces process/token/registry/storage/security/file/fd objects, both
ntdll/kernelbase/userenv DLLs and the additional lab hook define. Preserve the
S6 positive control, native crash checkpoint and final OK/FAIL evidence. Do not
run provisioning on shared VM B or bypass foreign-fixture refusal.

Still unverified: every compile/link and generated-header reproduction; normal
denial and both-ABI lab execution; timer-driven automatic draining and mixed
descendant/impersonation races; source replacement and live S12-lock contention;
periodic/shutdown flush persistence; fsync/rename/RNG/allocation/FD exhaustion;
custom descriptor/volatile/class behavior and resource/leak stress; native
same-UID race bounds and real power loss. S18 must supply authenticated lease
transfer, rollback and guard/resume integration. No new owner policy decision
is required for this disabled package; production acceptance remains gated.

Performed: source-level lifetime/ownership/rollback, allocation-failure,
descriptor and default-gate review; request enum/handler/dumper/name counts
(326 including NULL reply-dumper slots); manual 32/24-byte layouts and export
signatures; unchanged 88-byte carrier/generator maximum; `git diff --check` in
both repositories; Git Bash `bash -n` on the three touched shell scripts.
No build, generator or fixture execution, commit, or host/guest/service change.

Tooling observations: the initial PowerShell read/search returned exit 0 and
empty output; it was discarded. Explicit cmd.exe/login:false and bounded Node
filesystem reads restored visible evidence. Broad status/instruction search
reported the preexisting inaccessible unsloth-tmp directory; scoped reads were
used and preexisting untracked files were left alone. Two rg searches for `^`
under cmd returned no text; bounded Node line reads replaced them. Large output
was truncated and the relevant portions were reread separately. One static
protocol checker initially counted only function pointers in reply_dumpers and
failed because 90 existing NULL entries were omitted; counting those slots
confirmed 326 entries in every table. A no-match rg search raised its ordinary
exit-1 exception; the required source was read directly. All edits used
apply_patch; no C escapes were passed through a shell heredoc. No WSL attempt
or fixture/session mutation was made by any of these checks.
