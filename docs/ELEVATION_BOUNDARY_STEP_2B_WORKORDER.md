<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: make the limited token real (plan step 2b)

Date: 2026-10-22. Status: scoped; D1 and D2 decided (below).
Parent plan: `docs/ELEVATION_BOUNDARY_PLAN.md` ("Status"). Previous step:
`ELEVATION_BOUNDARY_STEP_2_3_WORKORDER.md` (merged: Wine `0e402796`, parent `63e94608`).

## Goal

While an elevation helper is configured, a non-elevated process runs with a token that matches a native
filtered administrator token. Concretely:
- Administrators is deny-only;
- only the filtered-token privileges remain;
- the integrity level is Medium;
- the elevation type is Limited.

The linked token stays the full administrator token. With the helper unset, nothing changes.

## Why

Today's "limited" token (`token_create_admin( TRUE, -1, TokenElevationTypeLimited, ... )`, `server/process.c`
~line 672) differs from the full token only in its elevation type:
- it holds all 21 admin privileges, four of them enabled;
- Administrators is enabled;
- its integrity level is High.

A limited process can therefore enable `SeTcbPrivilege` and pass every check from step 3.

## Facts found while scoping

1. **The root token is created in one place.** Every process inherits it from the first process
   (`process.c:672`). Services are not special: `wineboot` starts `services.exe` with `create_native_process`
   (`programs/wineboot/wineboot.c:1465`), so `services.exe`, `winedevice.exe`, `rpcss` and `plugplay` inherit
   the same token. `ProcessWineMakeProcessSystem` marks a process for shutdown only; it does not change the
   token.
2. **Filtering code already exists.** `DECL_HANDLER(filter_token)` with `LUA_TOKEN | DISABLE_MAX_PRIVILEGE`
   (`server/token.c` ~line 1647):
   - makes Administrators deny-only;
   - drops every privilege except `SeChangeNotifyPrivilege`;
   - sets the label to Medium.

   Reuse it as one helper instead of writing a second copy.
3. **Registry permissions break first.** The default key security descriptor (`key_get_sd`, `server/registry.c`
   ~line 869) has a `Users: GENERIC_READ` ACE that is inherit-only, so it does not apply to the key itself, and
   an `Administrators: KEY_ALL_ACCESS` ACE. With Administrators deny-only, a limited process gets no access to
   any key that has the default descriptor, including `HKCU` and `HKLM` reads. Windows gives the user full
   control of `HKCU`, and Users read access to `HKLM\Software`.
4. **Files are unaffected.** File security descriptors are derived from Unix modes with the user's SID as owner
   (`mode_to_sd`, `server/file.c`), so the user keeps access to their own files.
5. **Privilege checks in the server.** `SeRestorePrivilege` and `SeBackupPrivilege` guard registry load and
   save (`server/registry.c` ~lines 3084–3139); these need elevation on Windows too. Client-side privilege users:
   `programs/taskmgr` (`SeDebugPrivilege`), `dlls/setupapi/misc.c`, `dlls/shell32/dialogs.c` (shutdown).
6. **Shipping images are not affected.** Nothing in `installer/` configures
   `HKLM\Software\Wine\Elevation\Helper`, so enforcement stays opt-in. Inside an enforced prefix nothing can
   elevate until the broker (plan step 4) lands. That is acceptable for test prefixes. Do not enable the helper
   in images before step 4.

## Decisions needed

- **D1: what native filtered token to copy.** Record a native filtered administrator token: `whoami /all` from
  a non-elevated prompt on the Windows 10.0.26300 worker (groups with attributes, privileges, integrity).
  Recommendation: match it exactly, limited to the privileges Wine defines.
- **D2: system role for services.** Services and drivers need the full token; they inherit the limited one
  today. Options:
  - **(a) Recommended.** The server gives the system role to `services.exe`, and so the full token to it and its
    children, only when its image file sits in a machine root owned by Unix root that the user cannot write
    (the S2/S3 machine root). In a user-writable prefix there is no trustworthy system role: services run
    limited and this is documented. The same mechanism gives the broker its system role later (plan Q2).
  - (b) Services always run limited under enforcement; fix what breaks.
  - (c) A separate Unix UID for services. Larger; needs the cross-UID handle design.

  **Decision (2026-10-22): (a) plus the Windows service account model.**
  - The server knows three service tokens: SYSTEM (S-1-5-18: all privileges, Administrators enabled, System
    integrity), LocalService (S-1-5-19) and NetworkService (S-1-5-20) (minimal privileges, `SeImpersonate` and
    `SeCreateGlobal` enabled, no Administrators). `services.exe` starts each service on the token named by its
    `ObjectName`; an empty or `LocalSystem` value means SYSTEM, as on Windows.
  - Only a trusted `services.exe` may receive or hand out the SYSTEM token: its image must sit in the machine
    root owned by Unix root that the user cannot write. The broker gets its system role the same way (plan Q2).
  - Why the gate also covers services: with SYSTEM a process holds `SeTcbPrivilege` and can get the full linked
    token as a primary token (native N6). In a user-writable prefix a limited process can replace
    `services.exe`, or create or reconfigure a LocalSystem service (Wine checks neither file nor registry ACLs
    yet), so an ungated SYSTEM token would be a way around the elevation boundary.
  - Making any service token needs an enabled `SeTcbPrivilege`, as `LogonUser` for a service account does on
    Windows: LocalService and NetworkService hold `SeImpersonatePrivilege`, which the limited token lacks, so
    handing them to an untrusted caller would also widen its rights. Today `services.exe` starts every service
    with plain `CreateProcessW` (`programs/services/services.c`), so services inherit its token; it will build
    the token named by `ObjectName` and use `CreateProcessAsUserW` when it holds the system role.
  - Not trusted (user-writable prefix): `services.exe` has no system role, so every service runs on the user's
    limited token, whatever its `ObjectName`. Documented as a limit of user-writable prefixes.
  - Even in the machine root, creating or changing a service must need `SC_MANAGER_CREATE_SERVICE` /
    `SERVICE_CHANGE_CONFIG`, which a limited token does not get on native, and the `Services` registry key must
    be write-protected (registry enforcement work order). Until both hold, the SYSTEM token stays off even in
    the machine root.
  - Later, not in this step: per-service SIDs (`NT SERVICE\name`), required-privileges lists, restricted service
    SIDs, Session 0 isolation.

## Scope

1. **Native reference (worker). Done (D1, 2026-10-22).** Run `whoami /all` non-elevated and elevated. Record both in
   `TOKEN_SECURITY_REFERENCES.md`.
2. **Probe first.** Extend `scripts/test_token_security_regressions.cpp` with `limited_token_shape()`, run with
   the helper configured. It asserts the D1 shape of the process token and that the linked token is still full.
   It also asserts:
   - `AdjustTokenPrivileges(SeTcbPrivilege)` fails with `ERROR_NOT_ALL_ASSIGNED`;
   - `ProcessWineGrantAdminToken` stays refused after that attempt;
   - `CreateProcessWithTokenW(linked)` now returns 1314.

   Show it failing on `0e402796`.
3. **Root token (server).** In `process.c` ~line 672, when enforced, build the limited token as filtered from
   the admin token. Factor the `filter_token` LUA and DISABLE_MAX_PRIVILEGE body into one helper in `token.c`,
   used by both paths; keep the D1 privilege list in that helper. `create_linked_token` keeps returning the
   full token. Also drop the 1346/1314 dual expectation in the step 2/3 probe; it becomes 1314.
4. **Registry permissions (server).** Change the default key descriptor so that:
   - the `HKCU` (`\Registry\User\<sid>`) subtree grants the user SID and SYSTEM full control, and
     Administrators full control;
   - `HKLM` keys grant Users read on the key itself, not inherit-only, and Administrators full control.

   This can be unconditional, because with today's tokens Administrators is enabled and the effective access
   does not change. Prove that with the unset-helper suite.
5. **System role (D2 decided, see above), after `ELEVATION_REGISTRY_ENFORCEMENT_WORKORDER.md`.** Probe:
   - in the machine root, `services.exe`, `winedevice.exe` and a LocalSystem test service report S-1-5-18;
     LocalService and NetworkService test services report S-1-5-19 / S-1-5-20 as user, minimal privileges and
     no Administrators group;
   - a copy of `services.exe` started by the user from a user-writable directory gets no system role, and in a
     user-writable prefix every service reports the user's limited token;
   - a limited process cannot create or reconfigure a service.
   - remove `Control\ServiceCurrent` and `ActiveComputerName` from the Users-writable table in
     `server/registry.c` once services.exe and `wineboot --init` write them with the system role, and prove the
     limited token can no longer write them.
6. **Breakage sweep under enforcement**, with the B4 smoke test in `WEX_B4_HELPER=1` mode plus:
   - `wineboot -u`;
   - `regedit` reading `HKLM`;
   - `notepad` saving to the user's documents;
   - a 32-bit and a 64-bit console program;
   - `taskmgr` listing processes.

   Fix in the component only what Windows also allows a filtered admin. List anything left in the PR.

## Acceptance

- The step 2 probe fails on `0e402796` and passes after; both architectures; unprivileged user.
- Helper unset: the full harness (`--all`) matches `0e402796` (`advapi32 security` 3726/0, `ntdll info` 0
  failures, `shlexec` 2 known), the sentinel passes, and B4 passes.
- Helper configured: the focused harness passes, B4 `WEX_B4_HELPER=1` passes, and the breakage sweep is
  recorded.
- Native probe under Wine, helper configured: `CreateProcessWithTokenW(linked)` gives 1314. The process token
  matches the D1 reference within Wine's privilege list.
- `STOCK_CHANGES.md` and the plan status are updated. Hashes are taken from the pushed blobs.

## Out of scope

- The broker, consent prompt and secure desktop (plan steps 4 to 6).
- Making elevation work again in enforced prefixes.
- Integrity-level enforcement beyond what `check_object_access` already does (mandatory label checks exist).
- Changing tokens with the helper unset.

## How the work is split

- **Planning and review: the main session (Opus).**
- **Implementation: a Sonnet agent.** It takes scope items 2 to 4 first, then 5 once D2 is decided, then 6,
  from this file.
- **Review:** the main session reviews each push against this order before merging.

## Results (2026-10-22, scope items 2 to 4 and the scriptable part of 6)

Wine branch `ccr-work` on top of `0e402796`, parent branch `ccr-08223ee4-p830bf`. WoW64 build, both architectures,
unprivileged user `wextest`. Item 5 (system role): done in C3, see below. Not done: the
interactive part of item 6 (`regedit`, `notepad`, `taskmgr`). Wine commits (working-branch hashes): `4fced06b`
filter helper, `839c0f31` root token, `4fa6f4ac` registry default descriptor. Parent commits: the probe, then
this documentation.

### Probe design (item 2)

`limited_token_shape()` in `scripts/test_token_security_regressions.cpp` asserts: elevation Limited; Administrators
deny-only and not enabled; Medium integrity; the privileges are exactly `limited_privileges[]` (one array, marked
provisional, `SeChangeNotifyPrivilege` only); enabling `SeTcbPrivilege` gives `ERROR_NOT_ALL_ASSIGNED`;
`ProcessWineGrantAdminToken` on a suspended child stays refused and the child reports Limited; the linked token is
Full, has Administrators enabled, High integrity and `SeTcbPrivilege`; `CreateProcessWithTokenW(linked)` gives 1314.

The root token is built when a client connects to the server without a parent (`server/request.c`), which every
Unix-launched `wine program.exe` does, and it is filtered only if the helper is already set at that moment. A child
inherits its parent's token, so the probe cannot run from the `--shell-tests` process. Instead the harness
(`check-token-security.py`) makes a second throw-away prefix per architecture, runs `exe --helper-set` there, kills
the server (so the registry is saved), and starts `exe --limited-shape` as a new process. It prints
`limited_token_shape=PASS|FAIL`; the harness requires it. Deviation: the marker comes from that second command, not
from `--shell-tests`. In the `--shell-tests` process the token is unfiltered by design, so its N2 check keeps the
1346/1314 dual expectation; the 1314 expectation lives in `limited_token_shape()`.

| Run | Result |
| :--- | :--- |
| Focused harness on `0e402796` + probe (`/tmp/wexout/p1`) | FAIL as expected: Administrators enabled (`0xf`), integrity `0x3000`, 21 privileges, `SeTcbPrivilege` enabled without error, grant-admin succeeded (child `101`), `CreateProcessWithTokenW(linked)` 1346 |
| After the root token only (`p2`) | shape correct, but the linked token handle failed with error 5 (see deviations) |
| Focused harness, final, x86_64 and i386 | `limited_token_shape=PASS`, `linked_token_routes_closed=PASS`, consent PASS |

### Acceptance

| Check | Result |
| :--- | :--- |
| Full harness, helper unset (`--all`), both architectures | `advapi32 security` 3728 tests / 0 failures; `ntdll info` 2811 (x86_64) and 2744 (i386) / 0; `shell32 shlexec` 1805 / 2 (the known two); consent PASS |
| Runner sentinel | success, failure, timeout, signal PASS |
| B4 smoke test, helper unset and `WEX_B4_HELPER=1` | PASS both |
| Native-contract probe under Wine, helper configured, x64 and x86 | equals the native table; `CreateProcessWithTokenW(linked)` is now 1314; own-token controls succeed; child exit 103 (Limited) |
| Sources in `/home/user/wine-dev` equal `git show HEAD:<file>` | `server/token.c`, `process.c`, `registry.c`, `security.h`: identical |

Counts against the baseline (3726, 2687, 2618): `advapi32 security` is +2 because the test runs one `ok()` per ACE
of the `HKLM\Software` descriptor and it now has four ACEs instead of two. `ntdll info` is +124 and +126; the logs
differ from the baseline only in process IDs, and I did not find which loop scales with them. Failure counts equal
the baseline.

### Deviations and findings

1. **The registry premise of item 4 is wrong.** `check_object_access` grants everything on an object whose `sd` is
   NULL (`server/token.c`), and `key_get_sd` is only used when a descriptor is queried (`server/handle.c`). Default-
   descriptor keys were never protected. So a filtered process could already use `HKCU` and `HKLM`, and still can:
   `reg add HKLM\Software\Wine /v WexX` and `reg add HKLM\Software\WexSweep` both succeed under the filtered token.
   The registry change therefore only changes what `GetSecurityInfo` reports (as Windows would: user full control of
   `HKCU`, Users read on `HKLM`). It is unconditional, and the unset-helper suites show no failure.
   Making `HKLM` writes fail needs two more changes: use `key_get_sd` in `check_object_access` for keys, and check
   `KEY_CREATE_SUB_KEY` on the parent in `create_key` (Wine creates the key first and then grants the handle with
   `alloc_handle_no_access_check`). I tried the first one, gated on enforcement (patch kept in
   `/tmp/wexout/registry-enforce-trial.patch`, not committed): `wineboot -u` then never finished and `wineserver`
   spent 4 minutes at 40% CPU. I reverted it. Decision needed before this item can be finished; I suggest a separate
   work order that includes the system role (D2), because services are the likely cause.
2. **Linked token handle.** A Medium caller got error 5 for `TokenLinkedToken` because the handle was requested with
   `TOKEN_ALL_ACCESS` and the High mandatory label denies the write rights. `create_linked_token` now grants the
   access `check_object_access(MAXIMUM_ALLOWED)` returns, masked with `TOKEN_ALL_ACCESS`. Using `alloc_handle(...,
   MAXIMUM_ALLOWED)` directly changed the mask to `0x1f01ff` and broke `test_elevation`, so it was not used.
3. **Registry descriptor shape.** The advapi32 test expects the last Users ACE to be inherit-only with
   `GENERIC_READ` and the Administrators ACE to have flags 0 (`broken()` values only apply on Windows), so the new
   key-itself Users ACE comes first and the old one stays last.

### Breakage sweep under enforcement (`/tmp/wexout/sweep2.sh`)

Prefix booted unfiltered, helper set, server restarted so that `wineboot`, `services.exe`, `winedevice.exe`,
`plugplay.exe` and `rpcss` start from the filtered root token. Compared with the same script without the helper.

| Step | Helper set | Control |
| :--- | :--- | :--- |
| `wineboot -u` | exit 0 | exit 0 |
| `reg query HKLM\...\CurrentVersion /v ProgramFilesDir` | works | works |
| `reg add HKCU\Software\WexSweep` and query | works | works |
| `reg add HKLM\Software\WexSweep` | **succeeds** (should fail, see finding 1) | succeeds |
| `cmd /c exit 0`, system32 (64-bit) and syswow64 (32-bit) | exit 0 | exit 0 |
| `err:` lines | no line that the control lacks (`winebth` driver load `c00000e5`, `RpcSs`, setupapi `do_file_copyW` appear in both) | |

So nothing breaks yet, because the registry is not enforced (finding 1). The breakage from services running limited
will only show once registry enforcement exists. Not run: `regedit`, `notepad`, `taskmgr` (interactive).

### Open

- System role (item 5, D2): **done** (2026-10-22, C3). Probes: `scripts/test_sysrun_server.sh` section 8 and
  `ELEVATION_SYSTEM_ROLE_WORKORDER.md` "Results, C3". Services report S-1-5-18/19/20, a user copy of `services.exe`
  gets no role, a limited and a standard user cannot create or reconfigure a service or open the system processes,
  and the two temporary Users-writable grants are gone while the system role is active (kept without it). The launcher
  is not installed by default yet (constraint g).
- Registry enforcement for default-descriptor keys (finding 1), then re-run the sweep.
- Plan "Status" and the SHA-256 list of the pushed sources: after the planner pushes and merges.

## Results, item 1 (D1)

The native reference is recorded in `TOKEN_SECURITY_REFERENCES.md` ("Native filtered and elevated token, registry
ACLs"). A native filtered administrator token keeps SeShutdown (disabled), SeChangeNotify (enabled), SeUndock
(disabled), SeIncreaseWorkingSet (disabled) and SeTimeZone (disabled). Wine has no LUID for the last two, so
`token_make_filtered()` keeps the first three. `filter_token(DISABLE_MAX_PRIVILEGE)` is unchanged (change-notify
only, as `CreateRestrictedToken` on Windows). The probe's `limited_privileges[]` now lists the three names and the
probe checks that only SeChangeNotify is enabled.

Commits: Wine `595d604961d` (working branch `ccr-work`, `token_make_filtered`), WineExplorer `4230334` (probe),
then this documentation. WoW64 build, user `wextest`, both architectures.

| Check | Result |
| :--- | :--- |
| Focused harness (`--checkpoint-b --focused`) | `limited_token_shape=PASS`, `linked_token_routes_closed=PASS`, consent PASS, x86_64 and i386; privileges `SeChangeNotifyPrivilege SeShutdownPrivilege SeUndockPrivilege` |
| Full harness (`--all`) `advapi32 security` | 3728 tests / 0 failures, both architectures |
| Full harness `ntdll info` | 2811 (x86_64), 2744 (i386) / 0 failures |
| Full harness `shell32 shlexec` | 1805 / 2 (the known two), both architectures |
| Full harness consent | PASS, both architectures |
| `test_b4_explorer_fresh.sh`, helper unset and `WEX_B4_HELPER=1` | PASS both |

Note: the harness must run from a directory the test user can write (the advapi32 file tests create files in the
current directory); started from the repository checkout it fails 15 `advapi32 security` tests for that reason only.
