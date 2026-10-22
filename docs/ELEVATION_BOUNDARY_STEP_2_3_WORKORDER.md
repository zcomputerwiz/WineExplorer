<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: close the linked-token elevation bypass (plan steps 2 and 3)

Date: 2026-10-22. Status: ready to start. Parent plan: `docs/ELEVATION_BOUNDARY_PLAN.md` (read its "Native
results", "Prior art" and D1/D2 sections first).

## Goal

When the elevation helper is configured, a non-elevated Win32 process cannot obtain a full-token process or
use the linked admin token as an identity, by any of the five routes measured on the current build. Wine
returns the same errors as native Windows 10.0.26300. With the helper unset, behaviour is unchanged.

## Starting point

| Repository | Branch to start from | Commit |
| :--- | :--- | :--- |
| `zcomputerwiz/wine` | `gemini/token-security-b` | `f179c7e2` (contains `c01c57ef`) |
| `zcomputerwiz/WineExplorer` | `gemini/token-security-b` + PR #8 | PR #8 head |

Build WoW64 (`configure --enable-archs=i386,x86_64`) after `scripts/assemble.sh`. Tests run as an
unprivileged user on an owned display.

## In scope

1. Probes (plan step 2), written first and shown to fail on the starting build.
2. Server enforcement (plan step 3, D1 and D2 items 1 to 5).
3. Updating the existing harness expectations that relied on the bypass.
4. `docs/STOCK_CHANGES.md`, the plan's step status, and a short acceptance note.

## Out of scope

- The broker, consent UI, secure desktop and polkit work (plan steps 4 to 6). Under enforcement, `runas`,
  manifest elevation and msi elevated custom actions fail closed until step 4. That is expected.
- Migrating `shell32`, `msi` or explorer (step 7). Do not patch them to work around the new checks.
- N6 (`create_linked_token` returning a primary token to `SeTcbPrivilege` callers): nothing holds TCB until the
  broker exists. Leave a `shortcut:` comment where it will go.
- Changing behaviour with the helper unset. Stock-Wine upstreamability of the checks is a follow-up.

## The five routes and the expected results

Measured with `scripts/native_token_contract_probe.cpp`.

| Route | Wine today | Required under enforcement (native) |
| :--- | :--- | :--- |
| `CreateProcessAsUserW(linked)` | full-token child | `ERROR_BAD_IMPERSONATION_LEVEL` (1346) |
| `CreateProcessWithTokenW(linked)` | full-token child | `ERROR_PRIVILEGE_NOT_HELD` (1314) |
| `DuplicateTokenEx(linked, TokenPrimary)` | succeeds | 1346 |
| `NtSetInformationProcess(ProcessAccessToken, linked)` | succeeds | `STATUS_BAD_IMPERSONATION_LEVEL` (`0xC00000A5`) |
| `NtSetInformationProcess(ProcessWineGrantAdminToken)` | full admin token | refused (`STATUS_PRIVILEGE_NOT_HELD`) |
| N5: `SetThreadToken(linked)`, then open a file / a process token / a registry key | all succeed | `SetThreadToken` succeeds; then 1346 / 1346 / 5 |

These must keep working, because native accepts them:
- `CreateProcessAsUserW` with the caller's own token duplicated as an impersonation token at
  `SecurityImpersonation`;
- `ProcessAccessToken` with an own primary copy or an own restricted copy;
- `DuplicateTokenEx(linked, SecurityImpersonation, TokenImpersonation)`, which is already refused with 1346.

## Design

### Enforcement switch (D1)

Add one server helper, for example `int elevation_enforced(void)`, in `server/registry.c`. It reads
`HKLM\Software\Wine\Elevation\Helper` from the in-memory registry and applies the same rule as
`is_elevation_helper_unset()` in `dlls/ntdll/loader.c`.

The helper is unset only when:
- the key or the value is absent, or
- the type is `REG_SZ` or `REG_EXPAND_SZ`, the length is even, and the data is empty or starts with NUL.

Every other state enforces, including a value that cannot be read. Use the existing static lookups there
(`find_value`, the key walk from `root_key`). Read it at each check; do not cache.

### Server checks (D2), only when enforced

| Place | Check |
| :--- | :--- |
| `server/process.c`, `DECL_HANDLER(new_process)`, after `get_token_obj( ..., req->token, ... )` (~line 1257) | Reject a non-primary token whose `impersonation_level < SecurityImpersonation`: `STATUS_BAD_IMPERSONATION_LEVEL`. `token_duplicate( token, TRUE, ... )` at ~line 690 then stays as is |
| `server/process.c`, `DECL_HANDLER(set_process_info)`, `SET_PROCESS_INFO_TOKEN` (~line 1705) | Same level check. Then apply ReactOS's `PspSetPrimaryToken` rule (re-implemented, not copied): a token that is a child or sibling of the target's current token needs no privilege; otherwise require `SeAssignPrimaryTokenPrivilege` in the caller's token, else `STATUS_PRIVILEGE_NOT_HELD` |
| `server/process.c`, `DECL_HANDLER(grant_process_admin_token)` (~line 1798) | Require `SeTcbPrivilege` in the caller's token; else `STATUS_PRIVILEGE_NOT_HELD` |
| `server/token.c`, `token_duplicate` (~line 595) | Refuse `primary` when the source is a non-primary token below `SecurityImpersonation`: `STATUS_BAD_IMPERSONATION_LEVEL`. This check applies always, not only under enforcement, because it is native semantics and nothing in-tree depends on the opposite. If a caller turns up that does, gate it and note why |
| `server/token.c`, `check_object_access` (~line 1268) | When the effective token is `current->token`, non-primary and below `SecurityImpersonation`, fail with `STATUS_BAD_IMPERSONATION_LEVEL`. Find where registry opens check access and return `STATUS_ACCESS_DENIED` there instead, to match native (registry 5). Confirm file opens and `OpenProcessToken` reach this path; if one does not, add the same check where it is decided |
| `dlls/advapi32/security.c`, `CreateProcessWithTokenW` (~line 2728) | Require `SeImpersonatePrivilege` in the caller's process token under enforcement, else `ERROR_PRIVILEGE_NOT_HELD`. This is a client-side check for the native error code only; the server check above is what blocks the token |

Do not touch `create_linked_token`, except for the N6 `shortcut:` comment.

For the child/sibling test, the token needs a parent token ID. Check what `struct token` already stores
(`modified_id`, `token_id`, an authentication or logon ID). Add only what the rule needs.

## Tests (write first, see them fail)

1. **Wine conformance tests**, `dlls/advapi32/tests/security.c`, next to the existing linked-token checks
   (around lines 7195 and 8321). Add N1 to N5 and the native-accepted controls with the native error codes.
   - These run with the helper unset in the normal upstream suite, where the routes still succeed. So:
     - assert the native result when `HKLM\Software\Wine\Elevation\Helper` is configured (the test sets it and
       restores it);
     - otherwise mark the route cases `todo_wine`;
     - the `token_duplicate` case is native everywhere and must pass unconditionally.
2. **Harness probes**, `scripts/test_token_security_regressions.cpp`: a new `linked_token_routes()` with the
   five routes, N5 and the controls, run with the helper configured, plus a `--shell-tests` marker
   `linked_token_routes_closed=PASS`, required by `scripts/check-token-security.py`.
3. **Existing expectations that change.** With a configured helper, these no longer launch, because shell32
   still uses the linked token:
   - `shell_helpers()`: the mock-approved `runas` child;
   - `helper_states()`: "mock helper approves" through `ShellExecuteExW`.

   Change them to expect a refusal with no child handle and `ERROR_BAD_IMPERSONATION_LEVEL`, with a comment
   that step 4 restores elevation through the broker. Unset-helper cases keep launching a full-token child.
   Do not delete any case.
4. **Explorer.** With a configured helper, explorer's desktop now runs with the limited token. Run
   `scripts/test_b4_explorer_fresh.sh` with the helper configured and record anything that breaks. Fix only
   what is needed to keep the desktop and tray working, and only in explorer.

## Acceptance

All on a WoW64 build, both architectures, as an unprivileged user:

- the new probes and Wine tests fail on the starting build and pass after the change;
- `scripts/native_token_contract_probe.cpp` run under Wine with the helper configured gives the native values
  in the table above;
- with the helper unset: the focused harness, `advapi32 security`, `ntdll info` and `shell32 shlexec` match the
  starting build (`shlexec` keeps its 2 inherited failures, no new ones);
- with the helper configured: the focused harness passes with the updated expectations, and the B4 smoke test
  passes;
- `scripts/test-runner-sentinel.sh` passes;
- no unhandled exception in any harness log.

## Deliverables

- Wine commits, one per check, each listed in `docs/STOCK_CHANGES.md` with its native source (probe results).
- WineExplorer commits for the probes, harness expectations and docs.
- In `docs/ELEVATION_BOUNDARY_PLAN.md`: steps 2 and 3 marked done, with the commands and results.
- Hashes of the changed sources and built test binaries, taken from the pushed blobs and the same build tree.
- Push Wine first, then the parent gitlink. Open PRs against `gemini/token-security-b`.

## Risks

- **Server-side registry read.** Reading the registry from the server is new; keep it read-only and cheap.
- **Ordinary tokens.** The new `check_object_access` level check runs for every impersonating thread. The
  unset-helper suite run is what proves ordinary impersonation still works.
- **Hidden callers.** Something besides `shell32`, `msi`, `explorer` and `winetest` may depend on the bypass.
  Grep for `TokenLinkedToken`, `CreateProcessAsUser`, `ProcessAccessToken` and `ProcessWineGrantAdminToken`
  before changing the server, and list every hit in the PR.
