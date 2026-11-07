# E1-S13 lab failure (first run of the S13 harness)

Status 2026-11-07. The S10/S11 harness passes (E1S10_TOKEN OK, 52 PASS lines, both ABIs), S6 passes (302 checks), S12
passes (scripts/test_profile_storage.sh), the main and normal Wine builds pass. The S13 harness
(scripts/test_hku_load.sh, run in the clean wexbuild as root with WINE_BUILD_DIR=/root/owned-e1s6/build/wow64 and
WINE_NORMAL_BUILD_DIR=/root/owned-e1s11/build/normal-wow64) now builds both client ABIs with the S13 define and gets
through fixture setup and the S12 provisioning of Bob's profile for case accept (both fixed in the harness: the seed
file must be mode 0644 and use the audited default-template format). The first real S13 case fails:

lab_hku_accept_64, peer (32-bit, standard user Bob), step "S13 BEFORE", before the broker loads anything:

    FAIL e1s13_key_path:34 call=query && !last_status last_NT_call=NtQueryKey(KeyNameInformation) NTSTATUS=c0000008
    FAIL e1s13_key_path:36 call=!_wcsicmp(value.name, expected)
    ... later: FAIL peer:481 marker == (death ? 0x7e : 0)

c0000008 is STATUS_INVALID_HANDLE: RegOpenCurrentUser(KEY_READ) (and the KEY_ALL_ACCESS variant in e1s13_peer_state)
returns success but the returned handle is not a valid key handle when queried with NtQueryKey in the 32-bit peer.
Bob is an ordinary token at that point (nothing loaded yet).

Open questions for the next fix: which handle does RegOpenCurrentUser return in the lab build (ntdll reg.c
RtlOpenCurrentUser versus the kernelbase e1s_special_root path), is the handle valid in the 32-bit process
(WoW64 handle truncation or a handle from the wrong process or a closed handle in a cache), and does the 64-bit
peer behave the same? Run one case by hand if useful.

Separate flake, not yet understood: about every second harness run `wex-accounts sync` in the harness fails with
status=6 (WEX_ACCOUNT_MAP_UNPROTECTED, a read_protected check in wine/server/accounts.c) right after the machine
root setup, and the next run passes. The harness now writes map-state-pre.log (stat and getfacl of /, /etc,
/etc/wineexplorer and the map, plus the group) before the sync and keeps it on failure. Candidates: a stale
/etc/wineexplorer or group id from the previous run, a default ACL, or a timing issue in save_map.

## Static repair (2026-11-07)

Changes below are uncommitted. No build, program, harness, fixture, WSL or VM was
run. The original lab observations above are retained; this is a source-level
explanation and repair, not a passing runtime result.

### Invalid current-user handle

The pre-repair `kernelbase/registry.c:881-916` implementation explains the
failure without any handle truncation, cross-process handle or cache reuse:

1. An ordinary Bob thread has no impersonation token, so its initial
   `OpenThreadToken(..., FALSE, ...)` fails with NO_TOKEN.
2. `RegOpenCurrentUser` then calls `ImpersonateSelf(SecurityIdentification)`.
   `ntdll/sec.c:RtlImpersonateSelf` duplicates the process token at the requested
   level and sets it on the thread. The next `OpenThreadToken(..., FALSE, ...)`
   uses that Identification token for access checking: `server/token.c:open_token`
   calls `alloc_handle` without the OpenAsSelf override, and
   `check_object_access` rejects any below-Impersonation object open under
   enforced security (`STATUS_BAD_IMPERSONATION_LEVEL` for a token). This is an
   intentional security check, not a WoW64 conversion failure.
3. The function ignores those failures, reverts and falls through to
   `RegOpenKeyExA(HKEY_CURRENT_USER, "", ...)`. That function's empty predefined
   key shortcut returns `HKEY_CURRENT_USER` (0x80000001) as success, without
   opening a server handle. Both requested access masks take the same shortcut.
4. `ntdll/unix/registry.c:NtQueryKey/enumerate_key` sends this value to
   `server/registry.c:enum_key/get_hkey_obj`; `server/handle.c:get_handle_obj`
   finds neither a supported magic token/process/thread handle nor a real key
   table entry and sets `STATUS_INVALID_HANDLE`.

Thus the original API uses neither `RtlOpenCurrentUser` nor `e1s_special_root`
on this failing path. It returns a predefined pseudo-handle. The same code
permits the failure in both ABIs; only the 32-bit lab failure was observed.

`RegOpenCurrentUser` now validates/clears its output and delegates to
`RtlOpenCurrentUser`. That implementation already formats the effective token
SID without self-impersonation and returns a real, caller-process key handle
via `NtCreateKey` for ordinary tokens. In the S13 client it first requires the
existing QUERY classification and CURRENT registration/identity/access checks
for managed tokens. Indeterminate query failures still deny; disabled-server
fallback remains the existing RTL policy. No token access, Identification,
DACL, MIC, broker, registration or managed-cache check was relaxed. The
ordinary RTL path retains its existing ability to create an absent user root;
managed tokens still cannot create an unregistered empty HKCU.

The standalone S13 probe now checks both KEY_READ and KEY_ALL_ACCESS before
and after loading, rejects predefined-handle success, queries the expected SID
path, and verifies that no thread token appeared. It separately self-impersonates
at Identification level, requires denial with a cleared key, and verifies that
the failed API preserved that impersonation until the probe itself reverts.
Raw handle/API/query statuses are logged. A query failure no longer cascades
into an unrelated empty-name comparison. The ordinary advapi32 registry test
also queries the returned handle with NtQueryKey.

### Sync status 6

Status 6 alone does **not** prove `read_protected` failed. In
`tools/wex-accounts/wex-accounts.c:main`, status is also set to
WEX_ACCOUNT_MAP_UNPROTECTED immediately before `checked_lock`; its
`flock(LOCK_EX | LOCK_NB)` failure returns through the same status. The original
`scripts/setup-machine-root.sh` installed/enabled `wex-accounts.path` and
`.timer` on systemd hosts before the S10 fixture's useradd/usermod sequence.
The path watches `/etc/passwd` and starts the same account writer; the timer
repeats it. That is a concrete source of lock contention with the fixture's
explicit sync. Those global units were also outside its directory/group
cleanup, allowing activation to survive a prior harness run.

This source-level race is fixed for the fixture: setup accepts the validated
`WEX_ACCOUNTS_AUTOSYNC=0` opt-out, while normal setup still defaults to 1. The
S10/S13 fixture passes 0, uses its just-built account tool even for initial
machine provisioning, and refuses active/activating foreign account units
before/after setup. It never stops or adopts foreign services. No retry,
metadata repair, chmod/chown workaround or lock bypass was added.

Shared `protected_directory`/`read_protected` diagnostics compile only into the
root account tool and now distinguish directory type/owner/mode, access ACL or
ACL-query failure, file type, owner, write bits, link count and actual/expected
GID. The tool separately identifies lock-open/metadata/acquisition failures.
Server status replies stay unchanged; no map bytes or account records are
logged. `map-sync.log` now captures stderr too; pre-sync metadata evidence
includes sysrun.conf and the lock. Added account-map fixture assertions cover
wrong-GID rejection and contention diagnosis without changing map bytes.

`save_map` already holds the checked writer lock, validates new file ownership,
mode and ACL before publishing, fsyncs it, and atomically renames it. A writer
failure there is reported as status 5, not status 6. Its protection/publication
algorithm was left intact. Existing-map GID drift and unsafe directory/file
ACLs still deny. Without the failing run's metadata and new stage diagnostics,
the reported intermittent failure cannot be conclusively attributed to the
automatic writer rather than one of these other predicates.

### Verification and wexbuild follow-up

Performed only static call/handle/authority/cleanup review and `git diff --check`
in both repositories. No shell syntax execution, compilation, linking, probe,
generator, benchmark or runtime validation. No commit or host/guest/service
state change. Existing unrelated untracked files were left alone.

Unrun validation for wexbuild:

* Rebuild/relink kernelbase and the existing S13 ntdll objects for both PE
  architectures, wineserver (shared account-provider edits), the native account
  tool and both probes. Run the S13 harness against clean lab and normal builds;
  require final E1S13_HKU_LOAD OK and no FAIL lines.
* Require real Bob handles and exact Bob SID paths for both masks in 32/64-bit
  peers before/after load; Identification-level denial with preserved thread
  token; managed missing-registration denial; SYSTEM/Alice cache separation,
  child/grandchild identities and existing broker/DACL/MIC/replay negatives.
* Run the advapi32 registry tests for both ABIs, and test_accounts_map.sh for
  wrong-GID/owner/mode/ACL/link/ancestor refusals and writer-lock contention.
* Repeat the S10/S13 harness on a systemd wexbuild: no account path/timer/service
  may be started by fixture provisioning. An already-active foreign unit must
  cause preflight refusal. Retain map-sync.log and map-state-pre.log on any
  failure; the new reason distinguishes contention from an unsafe map/policy.

The 64-bit peer outcome, exact handle from the old failing binary, and actual
status-6 predicate remain runtime-unverified. S13 stays default-disabled; S14
lifetime/persistence and S18 authentication work remain outside this fix.
