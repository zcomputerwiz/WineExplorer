<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Plan: make elevation a real Win32 boundary

Date: 2026-10-22. Status: planned, not started. Base: `gemini/token-security-b` at `6d428225`
(Wine `c01c57ef`). Governing orders: `TOKEN_SECURITY_CHECKPOINT_B_WORKORDER.md` (B1, B2),
`TOKEN_SECURITY_B_ACCEPTANCE.md` (open items).

## Why this item

Every elevation control built so far (the consent dialog, the helper audit, manifest checks, the B2 work still
to come) can be skipped by any non-elevated Win32 program in two calls. Until that is fixed, B2 authorization
has nothing to protect, and the browser's medium-to-high boundary does not exist.

Reproduced on the `c01c57ef` WoW64 build, as an unprivileged Unix user, in a fresh prefix:

```text
parent elevation type 3                        <- TokenElevationTypeLimited
linked token type 2 impersonation level 1      <- TokenImpersonation, SecurityIdentification
CreateProcessAsUser(linked) ok=1 err=0
child exit 102 (102 = full token)              <- TokenElevationTypeFull, no prompt
```

The probe calls `GetTokenInformation(TokenLinkedToken)` and passes the result to `CreateProcessAsUserW`.
Windows refuses this: a non-TCB caller only gets an identification-level token, which cannot become a primary
token. Wine accepts it, because `new_process` and `set_process_info` in `server/process.c` turn any token into a
primary token with no impersonation-level check.

There is a second route. `NtSetInformationProcess(GetCurrentProcess(), ProcessWineGrantAdminToken, NULL, 0)`
gives the caller a full admin token (`grant_process_admin_token` in `server/process.c`). Stock Wine uses it so
that `explorer.exe` runs its desktop elevated, but any process can call it.

## Goal and scope

When elevation is enforced, a non-elevated Win32 program can obtain a full-token process only through the
elevation broker, after polkit/PAM authentication for that exact request. When it is not enforced, stock Wine
behaviour is unchanged.

In scope: the Win32 API boundary, under the documented same-Unix-UID compatibility route.

Out of scope (T5): native code using raw syscalls, `ptrace` or writes to the wineserver socket. Also out of
scope: standard-user over-the-shoulder elevation, which keeps failing closed with `ERROR_NOT_SUPPORTED` until
the identity channel from `ELEVATION.md` section 6 D exists.

## Native contract to establish first

Write these as Wine conformance tests (both architectures) and confirm each one on a native Windows 10/11
control before changing Wine. The XP VM cannot answer them.

| ID | Case | Expected on Windows (to verify) |
| :--- | :--- | :--- |
| N1 | `TokenLinkedToken` from a limited token, caller without `SeTcbPrivilege` | impersonation token, `SecurityIdentification` (Wine already does this) |
| N2 | `CreateProcessAsUserW`, `CreateProcessWithTokenW`, `NtCreateUserProcess` with an impersonation token | fails, `ERROR_BAD_TOKEN_TYPE` |
| N3 | `DuplicateTokenEx` of an identification-level token to `TokenPrimary` or `SecurityImpersonation` | fails, `ERROR_BAD_IMPERSONATION_LEVEL` |
| N4 | `NtSetInformationProcess(ProcessAccessToken)` with a non-primary token, or a primary token that is not a filtered version of the caller's, without `SeAssignPrimaryTokenPrivilege` | fails |
| N5 | Impersonating an identification-level token, then opening a secured object | fails, `ERROR_BAD_IMPERSONATION_LEVEL` |
| N6 | Same as N1, but the caller holds `SeTcbPrivilege` | usable linked token |

## Design

### D1. Enforcement switch

Enforcement is on exactly when the elevation helper is configured: the same "not unset" rule that
`kernelbase` and `ntdll` already use. Stock Wine and helper-unset prefixes keep today's behaviour, including
silent elevation and `ProcessWineGrantAdminToken`. The server reads `HKLM\Software\Wine\Elevation\Helper` from
its in-memory registry each time it checks. That is cheap and needs no new protocol. See Q1.

### D2. Server enforcement (`server/process.c`, `server/token.c`)

These apply only while enforced:

1. `new_process` and `set_process_info(SET_PROCESS_INFO_TOKEN)`:
   - reject a non-primary token below `SecurityImpersonation` with `STATUS_BAD_IMPERSONATION_LEVEL`;
   - reject any non-primary token with `STATUS_BAD_TOKEN_TYPE` where N2 and N4 say so.
2. `token_duplicate` refuses to raise an identification-level token to primary or impersonation (N3). Check
   whether the existing level check already covers the primary case.
3. `grant_process_admin_token` requires `SeTcbPrivilege` in the caller's token. Find out whether explorer still
   needs an elevated desktop under enforcement (Q3).
4. `create_linked_token` returns a usable token only to a caller holding `SeTcbPrivilege` (N6).

### D3. The broker (the AppInfo role)

Windows starts elevated processes from the AppInfo service, which holds `SeTcbPrivilege`, not from the
caller. Do the same:

1. **Broker process.** `wexelevate.exe`, a new program under `src/programs`, started once per prefix session by
   wineboot or services. The server gives `SeTcbPrivilege` only to a process it starts in a system role, never
   to an arbitrary client. Exactly how the server recognises the broker is decision Q2. One option is a
   server-started service flag, similar to `make_process_system`.
2. **Request.** The caller sends: image path, argv vector, cwd, environment policy, parent process. Transport
   is a named pipe owned by the broker; the broker reads the caller's PID and token from the server, not from
   the request. The broker freezes the request, assigns a single-use ID, and rejects truncated or changed
   targets.
3. **Authorization.** The broker calls polkit `CheckAuthorization` for itself, for action
   `org.wineexplorer.elevation.runas`, with `AllowUserInteraction`. The details include the exact image and
   command line. Default policy is `auth_admin`, never `*_keep`.
   - The existing wexhost agent (`src/dlls/wexshell/uac_agent.c`, `src/dlls/wexhost/unixlib.c`) shows the
     frozen request.
   - PAM checks the password through `polkit-agent-helper-1`.
   - A synthetic click cannot supply a password; that is why D3 requires credentials (Q4).
4. **Launch.** The broker:
   - duplicates the linked token, which it can do because it holds TCB;
   - creates the process with `PROC_THREAD_ATTRIBUTE_PARENT_PROCESS` set to the caller;
   - duplicates the process handle into the caller.

   The caller gets a genuine `hProcess` for the real target, preserving the repair's wait and exit-code contract.
5. **Errors.**
   - Cancel or polkit dismissal returns 1223 (`ERROR_CANCELLED`).
   - No agent, no polkitd or a timeout returns a distinct error, never success.
   - Standard users get `ERROR_NOT_SUPPORTED`.
   - A request is used once and destroyed on completion, error or caller exit. Concurrent requests stay
     independent.

### D4. In-tree callers that rely on the bypass

Each must work through the broker when enforced, or be shown not to need elevation:

| Caller | Today | Under enforcement |
| :--- | :--- | :--- |
| `dlls/shell32/shlexec.c` `runas` and `ERROR_ELEVATION_REQUIRED` | linked token + `CreateProcessAsUserW` after the helper approves | broker request; the helper exit code is no longer trusted |
| `dlls/ntdll/loader.c` `elevate_token` | silent elevation when helper unset | unchanged (never runs when enforced) |
| `dlls/msi/custom.c` elevated custom actions | linked token + `CreateProcessAsUserW` | broker request, or run unelevated with an explicit FIXME; decide after testing an installer |
| `programs/explorer/desktop.c` | `ProcessWineGrantAdminToken` | Q3 |
| `programs/winetest/main.c` | linked token for elevated tests | test-only; leave, document |
| `programs/consent` | consent dialog, helper exit code | becomes the broker's UI for click-only policy outside enforcement; under enforcement the polkit agent is the UI |

## Steps

Commit after each working step. Each step lists how it is accepted.

1. **Native control.** Run N1–N6 on Windows 10/11 x64 with a 32-bit and a 64-bit probe. Record the results in
   `TOKEN_SECURITY_REFERENCES.md`.
   Accept: every expectation above is confirmed or corrected.
2. **Bypass probes.** Add N1–N5 and `ProcessWineGrantAdminToken` cases to
   `scripts/test_token_security_regressions.cpp`. Run them with the helper configured.
   Accept: they fail on `c01c57ef`. This reproduces the bypass in the harness.
3. **Server enforcement (D1, D2).**
   Accept: the step 2 probes pass, and the existing focused, `advapi32 security` and `ntdll info` results are
   unchanged with the helper unset. `shell32` `runas` now fails closed under enforcement until step 5.
4. **Broker skeleton (D3.1, D3.2).** Request protocol, freezing, single use, handle return. Use a test-only
   approval stub that is built only into the test harness and never into shipping code.
   Accept:
   - the repair's target `hProcess` contract passes through the broker;
   - a request whose target changes after freezing is rejected;
   - A/B concurrency: approving A does not approve B;
   - long, Unicode and quoted arguments survive unchanged.
5. **polkit authorization (D3.3).** Policy file, `CheckAuthorization` from the broker, agent display of the
   frozen request. Test with a system `dbus-daemon` and `polkitd` started in the test container, and two
   accounts with known test passwords: one administrator (in `sudo`), one not.
   Accept:
   - a valid admin password launches exactly one target;
   - wrong, empty and arbitrary passwords, a valid non-admin password, cancel, no agent, no polkitd and a
     timeout all launch nothing and return their distinct errors;
   - no password or cookie appears in any log or argv.
6. **Caller migration (D4).** Move `shell32` and `msi` to the broker under enforcement, and settle explorer.
   Accept:
   - `runas`, `requireAdministrator` and `highestAvailable` work under standard, filtered and full tokens;
   - a 32-bit and a 64-bit benign installer elevate through the broker;
   - the B4 smoke test and the full harness pass.
7. **Documents.** Update `ELEVATION.md` (B1 design becomes implemented for the filtered-admin route),
   `STOCK_CHANGES.md`, `TOKEN_SECURITY.md` and the acceptance matrix. B2 can move to PASS only for the
   filtered-administrator route; standard-user elevation stays NOT SUPPORTED.

## Decisions needed before step 3

- **Q1. Enforcement trigger.** Tie it to "helper configured", as above, or use a separate
  `HKLM\Software\Wine\Elevation\Enforce` value? Recommendation: helper configured, so there is one switch.
- **Q2. How the server recognises the broker.** Options:
  - a server-started system-role process;
  - a fixed path checked by the server (weak);
  - a broker under a separate Unix UID, which needs the cross-UID handle design the B1 order warns about.

  Recommendation: the system-role process, documented as a Win32 boundary only.
- **Q3. Explorer's admin desktop under enforcement.** Refuse `ProcessWineGrantAdminToken` and run the desktop
  with the user's limited token, or let the broker start explorer elevated? Recommendation: limited token
  first, then fix whatever breaks.
- **Q4. Click-only consent (`ConsentPromptBehaviorAdmin` = 2) under enforcement.** Without a secure desktop,
  any same-desktop program can click "Yes". Recommendation: require credentials whenever enforcement is on;
  keep click consent only when it is off.
- **Q5. Native control.** Is a Windows 10/11 VM available for step 1? Without it, N1–N6 stay "per
  documentation" and the server changes carry that risk.

## Risks

- Installers and launchers that silently relied on the bypass will prompt or fail under enforcement. Step 6
  must test real installers, not only probes.
- polkitd and a system bus are not normally running in containers; the tests must start their own and must not
  touch a real machine's polkit configuration.
- Same-UID native code can still attack the server or the broker directly. That stays T5 and must be stated in
  every status line.
