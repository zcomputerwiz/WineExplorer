<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Work order: make the limited token real (plan step 2b)

Date: 2026-10-22. Status: scoped; decisions D1 and D2 below are needed before the system-role part.
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

## Scope

1. **Native reference (worker).** Run `whoami /all` non-elevated and elevated. Record both in
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
5. **System role (after D2).** Implement the chosen option, with a probe:
   - under enforcement, `services.exe` and `winedevice.exe` report the full token;
   - a copy of `services.exe` started by the user from a user-writable directory does not.
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
