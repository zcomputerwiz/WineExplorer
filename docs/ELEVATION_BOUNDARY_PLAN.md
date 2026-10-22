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

Write these as Wine conformance tests (both architectures) and confirm each one on native Windows 10/11 before
changing Wine. The XP VM cannot answer them.

| ID | Case | Microsoft documentation (checked 2026-10-22) | Needs native run |
| :--- | :--- | :--- | :--- |
| N1 | `TokenLinkedToken` from a limited token, caller without `SeTcbPrivilege` | Not documented: `TOKEN_INFORMATION_CLASS` and `TOKEN_LINKED_TOKEN` say only "a handle to another token that is linked" | yes: type and level |
| N2 | `CreateProcessAsUserW`, `CreateProcessWithTokenW` with an impersonation token | Documented: `hToken` must be a primary token; tokens from `DuplicateToken` "are not valid for CreateProcessAsUser". The error code is not documented | yes: error codes |
| N3 | `DuplicateTokenEx` of an identification-level token to `TokenPrimary` or `SecurityImpersonation` | Partly: `NtDuplicateToken` returns `STATUS_BAD_IMPERSONATION_LEVEL` when the requested level is higher than the existing one. The primary case is not stated | yes: primary case |
| N4 | `NtSetInformationProcess(ProcessAccessToken)` and `CreateProcessAsUserW` with a token other than a restricted version of the caller's | Documented: assigning a primary token needs `TOKEN_ASSIGN_PRIMARY` and `SeAssignPrimaryTokenPrivilege`, except for a restricted version of the caller's primary token | yes: status codes |
| N5 | Impersonating an identification-level token, then opening a secured object | Documented in outline: at Identify level the server "can impersonate the client for ACL checking, but it cannot access system objects as the client". The error code is not documented | yes: error codes |
| N6 | Same as N1, but the caller holds `SeTcbPrivilege` | Not documented | yes |

Sources: learn.microsoft.com pages for `CreateProcessAsUserW`, `CreateProcessWithTokenW`, `DuplicateToken`,
`DuplicateTokenEx`, `NtDuplicateToken`, `TOKEN_INFORMATION_CLASS`, Restricted Tokens, Access Rights for
Access-Token Objects, and the COM impersonation level constants.

The documentation fixes the direction of N2 to N5 but not the exact error codes, and says nothing about N1 or
N6. A native run is needed.

### Native probe

`scripts/native_token_contract_probe.cpp` prints one `key=value` line per case and asserts nothing. Build it
with MinGW for x64 and x86:

```bash
x86_64-w64-mingw32-g++ -O2 -static-libgcc -static-libstdc++ scripts/native_token_contract_probe.cpp \
    -o probe-x64.exe -ladvapi32 -lwtsapi32
i686-w64-mingw32-g++ -O2 -static-libgcc -static-libstdc++ scripts/native_token_contract_probe.cpp \
    -o probe-x86.exe -ladvapi32 -lwtsapi32
```

Run on Windows 10 or 11 x64, signed in as a member of Administrators with UAC at its default setting:

1. From a normal (non-elevated) command prompt: `probe-x64.exe > user-x64.txt` and
   `probe-x86.exe > user-x86.txt`.
2. As SYSTEM, for N6, with the same administrator signed in at the console:

   ```bat
   schtasks /create /tn wexprobe /sc once /st 23:59 /ru SYSTEM /f /tr "cmd /c C:\wexprobe\probe-x64.exe --tcb > C:\wexprobe\tcb-x64.txt 2>&1"
   schtasks /run /tn wexprobe
   schtasks /delete /tn wexprobe /f
   ```

   Repeat with `probe-x86.exe` and `tcb-x86.txt`.
3. Commit the four outputs, the Windows build number and the probe SHA-256 to
   `TOKEN_SECURITY_REFERENCES.md`.

### Native results (Windows 10.0.26300, x64 and x86 identical, 2026-10-22)

Run as a filtered administrator. The own-token rows are controls.

| Case | Plan expectation | Native | Wine `c01c57ef` |
| :--- | :--- | :--- | :--- |
| self | limited | primary, `TokenElevationTypeLimited` | same |
| N1 linked token | impersonation, Identification | impersonation, Identification, full | same |
| N2 `CreateProcessAsUserW(linked)` | `ERROR_BAD_TOKEN_TYPE` | **1346** `ERROR_BAD_IMPERSONATION_LEVEL` | succeeds, full-token child |
| N2 `CreateProcessWithTokenW(linked)` | `ERROR_BAD_TOKEN_TYPE` | **1314** `ERROR_PRIVILEGE_NOT_HELD` | succeeds, full-token child |
| N3 duplicate linked to primary | 1346 | 1346 | succeeds; the copy starts a full-token child |
| N3 duplicate linked to impersonation | 1346 | 1346 | 1346 (already correct) |
| N4 assign linked to a child | fails | `0xC00000A5` `STATUS_BAD_IMPERSONATION_LEVEL` | succeeds, full-token child |
| N5 impersonate linked, open file / process token | 1346 | `SetThreadToken` succeeds; file and process token 1346; registry key 5 | all opens succeed |
| N2 control: own token as impersonation token (`SecurityImpersonation`) | `ERROR_BAD_TOKEN_TYPE` | **succeeds** | succeeds |
| N4 control: own primary copy, own restricted copy | first fails | **both succeed** (status 0) | both succeed |
| N6 linked token for a `SeTcbPrivilege` caller (SYSTEM, user token from `WTSQueryUserToken`) | usable | **primary** token, full; see below | n/a |

Corrections to the design:
- **Impersonation level, not token type.** Windows accepts an impersonation token at `SecurityImpersonation`
  for `CreateProcessAsUserW`, and an unrestricted copy of the caller's own token for `ProcessAccessToken`.
  The rule that blocks the linked token is its Identification level, so D2 checks the level and does not
  reject impersonation tokens or own-token copies.
- **`CreateProcessWithTokenW`** fails earlier, on the missing `SeImpersonatePrivilege` (1314), for a filtered
  user.

N6, run as SYSTEM through a scheduled task with `--tcb`, x64 (x86 not rerun):

| Case | Native |
| :--- | :--- |
| user token from `WTSQueryUserToken` | primary, limited |
| linked token | **primary**, `TokenElevationTypeFull` (no impersonation level) |
| `CreateProcessAsUserW(linked)` | succeeds, child reports full (102) |
| `CreateProcessWithTokenW(linked)` | succeeds; the child exited `0xC0000142` (`STATUS_DLL_INIT_FAILED`), most likely because a SYSTEM task has no access to the user's interactive desktop; not a token result |
| `DuplicateTokenEx(linked)` to primary / to impersonation | both succeed; the impersonation copy is at `SecurityImpersonation` |
| `ProcessAccessToken(linked)` | `0xC0000061` `STATUS_PRIVILEGE_NOT_HELD`: assigning another user's or session's token needs `SeAssignPrimaryTokenPrivilege` enabled, which the probe does not enable |
| `SetThreadToken(linked)` | 1349 `ERROR_BAD_TOKEN_TYPE`, because the token is primary |

So Windows does not hand a TCB caller a higher impersonation level: it hands it a different kind of token, a
usable primary token. The child exit codes are as expected: 101 is `TokenElevationTypeDefault` (SYSTEM), 103
is `TokenElevationTypeLimited` (filtered user).

### Wine baseline (`c01c57ef`, both architectures, identical)

```text
user.N1.linked.type=2 level=1 elevation=2                    impersonation, Identification, full
user.N2.create_as_user_linked.ok=1 child_exit=102            full-token child
user.N2.create_with_token_linked.ok=1 child_exit=102         full-token child
user.N3.dup_primary.ok=1, then create.ok=1 child_exit=102    identification raised to primary
user.N3.dup_impersonation.ok=0 error=1346                    already refused (ERROR_BAD_IMPERSONATION_LEVEL)
user.N4.assign_linked.status=0x00000000 child_exit=102       full token assigned to a child
user.N5.impersonate_linked open_file/open_key/open_token errors=0   identification impersonation opens objects
user.N2.create_as_user_own_impersonation.ok=1                impersonation token accepted for a process
user.N4.assign_own_primary_copy.status=0                     non-restricted primary assigned without privilege
```

There are five routes to a full-token process: two in N2, one in N3, one in N4, plus
`ProcessWineGrantAdminToken`. N5 shows that an identification-level token works like an impersonation token.

## Prior art

No open project has a UAC broker with a secure desktop to reuse whole. These parts have usable prior art.
ReactOS code may be read and ported under the project's rules, but the Wine server is LGPL: re-implement the
logic there instead of copying GPL code, so the change stays upstreamable.

| Area | Prior art | Use |
| :--- | :--- | :--- |
| Primary token assignment (D2) | ReactOS `PspSetPrimaryToken` (`ntoskrnl/ps/security.c`): a new primary token must be a child or a sibling of the current one (`SeIsTokenChild`, `SeIsTokenSibling`, comparing parent and authentication IDs); otherwise `SeAssignPrimaryTokenPrivilege` is required. The sibling test was added in 2018 | The rule behind the native N4 controls (own primary and restricted copies succeed). Re-implement in `server/process.c` |
| Linked tokens, `runas`, msi (D4) | Wine-Staging's UAC series, now mostly upstream: limited admin token by default, linked token, `start /elevate`, shell32 `runas`, msi's elevated custom-action server. Bug 40613 lists the applications that need it | Explains today's call sites; its discussions list the installers that fail without elevation, which become step 7 test cases |
| Conformance tests (step 2) | `dlls/advapi32/tests/security.c` already queries the linked token's elevation and impersonation level (around lines 7195 and 8321) | Add N1-N5 there as Wine tests, besides the harness probes |
| Secure desktop (D5) | ReactOS winlogon (`base/system/winlogon/security.c`, `wlx.c`) creates the Winlogon desktop with a descriptor that admits only winlogon and administrators; its history records handle-inheritance and access-right pitfalls (2009, 2015) and an open FIXME on restricting the desktop. Microsoft documents WinSta0's Default, ScreenSaver and Winlogon desktops and the switch to Winlogon for Ctrl+Alt+Del and UAC | Model for the desktop, its DACL and creation sequence. Wine already has `NtUserSwitchDesktop` (`dlls/win32u/winstation.c`) and an input desktop in the server |
| Broker (D3) | Microsoft's UAC model: a process cannot elevate itself and must ask the system to start a new elevated process (AppInfo) | Shape only; ReactOS has no UAC, so there is no code to port |
| Credential prompts (D3.3) | polkit authentication agents; the existing wexhost agent | Already in the tree. Samba (GPLv3) stays design guidance only |

Sources: ReactOS `ntoskrnl/ps/security.c` and ros-diffs threads for the sibling check; ReactOS doxygen for
`base/system/winlogon/security.c`; Wine-Staging and wine-bugs threads for bug 40613; Microsoft Learn
"Desktops", "Winlogon" and "UAC distilled". These were read as summaries and excerpts; read the current source
before porting.

## Order of work

Do this plan before the remaining B2 and B3 work, because both depend on it:
- B2 authorization means nothing while any program can elevate itself;
- B3's "non-elevated Win32 writes fail" cannot hold while elevation is free.

Inside the plan, the order is by value and size:
1. **Steps 2 and 3 first.** They are small and close all five routes. With the helper unset, stock behaviour
   is unchanged. With it configured, `runas` fails closed until the broker lands, which is acceptable in the
   meantime.
2. **Step 4 (broker), then step 6 (consent) with a plain consent window.** This restores elevation through a
   boundary that holds against direct token use, though not yet against synthetic clicks.
3. **Step 5 (secure desktop).** The largest and most uncertain part. Its research sub-step can start early, in
   parallel, because it only reads and probes.
4. **Step 7 (caller migration), then step 8 (documents).**

Checkpoint C (the Chromium and Edge sandbox) does not depend on this plan. The browser's sandbox uses
restricted tokens, job objects and AppContainer, not UAC elevation, so C can proceed in parallel. The one
exception: a C acceptance claim that medium-integrity browser code cannot gain a full token needs steps 2 and
3.

## Design

### D1. Enforcement switch

Enforcement is on exactly when the elevation helper is configured: the same "not unset" rule that
`kernelbase` and `ntdll` already use. Stock Wine and helper-unset prefixes keep today's behaviour, including
silent elevation and `ProcessWineGrantAdminToken`. The server reads `HKLM\Software\Wine\Elevation\Helper` from
its in-memory registry each time it checks. That is cheap and needs no new protocol. (Q1: decided.)

### D2. Server enforcement (`server/process.c`, `server/token.c`)

These apply only while enforced, and follow the native results above:

1. `new_process` and `set_process_info(SET_PROCESS_INFO_TOKEN)` reject a non-primary token below
   `SecurityImpersonation` with `STATUS_BAD_IMPERSONATION_LEVEL` (N2, N4). Impersonation tokens at
   `SecurityImpersonation` or above, and copies of the caller's own token, stay accepted.
   For assignment, follow ReactOS's rule: a token that is a child or sibling of the current one (same parent
   and authentication IDs) needs no privilege; any other token requires `SeAssignPrimaryTokenPrivilege`.
2. `CreateProcessWithTokenW` requires `SeImpersonatePrivilege` in the caller's token and fails with
   `ERROR_PRIVILEGE_NOT_HELD` otherwise (N2).
3. `token_duplicate` refuses to raise an identification-level token to a primary token, with
   `STATUS_BAD_IMPERSONATION_LEVEL` (N3). Raising it to impersonation is already refused.
4. Opening a file, a process token or another secured object while impersonating an identification-level
   token fails with `ERROR_BAD_IMPERSONATION_LEVEL`. A registry key fails with `ERROR_ACCESS_DENIED` (N5).
5. `grant_process_admin_token` requires `SeTcbPrivilege` in the caller's token. Under enforcement explorer
   runs with the user's limited token (Q3: decided).
6. `create_linked_token` returns the linked token as a **primary** token to a caller holding
   `SeTcbPrivilege` (N6), and as an identification-level impersonation token to everyone else (N1, as now).
   The broker (D3) uses the primary token directly with `CreateProcessAsUserW`.

### D3. The broker (the AppInfo role)

Windows starts elevated processes from the AppInfo service, which holds `SeTcbPrivilege`, not from the
caller. Do the same:

1. **Broker process.** `wexelevate.exe`, a new program under `src/programs`, started once per prefix session by
   wineboot or services. The server gives `SeTcbPrivilege` only to a process it starts in a system role
   itself, never to an arbitrary client (Q2: decided). This is documented as a Win32 boundary only.
2. **Request.** The caller sends: image path, argv vector, cwd, environment policy, parent process. Transport
   is a named pipe owned by the broker; the broker reads the caller's PID and token from the server, not from
   the request. The broker freezes the request, assigns a single-use ID, and rejects truncated or changed
   targets.
3. **Authorization.** This depends on the consent policy:
   - `ConsentPromptBehaviorAdmin` = 2 (filtered administrator, default): consent on the secure desktop
     (D5). A synthetic click cannot reach it, which is why click consent is acceptable there (Q4: decided).
   - Policies 1 and 3 (credentials): the broker calls polkit `CheckAuthorization` for itself, for action
     `org.wineexplorer.elevation.runas`, with `AllowUserInteraction`. The details carry the frozen image and
     command line. Default policy is `auth_admin`, never `*_keep`. The existing wexhost agent
     (`src/dlls/wexshell/uac_agent.c`, `src/dlls/wexhost/unixlib.c`) shows the request on the secure desktop,
     and PAM checks the password through `polkit-agent-helper-1`.
4. **Launch.** The broker:
   - gets the linked token as a primary token, which it can because it holds TCB (N6);
   - creates the process with `PROC_THREAD_ATTRIBUTE_PARENT_PROCESS` set to the caller;
   - duplicates the process handle into the caller.

   The caller gets a genuine `hProcess` for the real target, preserving the repair's wait and exit-code contract.
5. **Errors.**
   - Cancel or polkit dismissal returns 1223 (`ERROR_CANCELLED`).
   - No agent, no polkitd or a timeout returns a distinct error, never success.
   - Standard users get `ERROR_NOT_SUPPORTED`.
   - A request is used once and destroyed on completion, error or caller exit. Concurrent requests stay
     independent.

### D5. Secure desktop

Windows shows consent on a separate desktop that ordinary programs cannot see, send messages to or inject
input into. Do the same in the Win32 layer:

1. **Research (first).** Start from ReactOS winlogon's Winlogon desktop setup and its recorded pitfalls (see
   Prior art). Wine already tracks an input desktop per window station (`set_input_desktop` in
   `server/winstation.c`, `get_hardware_input_desktop` in `server/queue.c`). Establish, with probes:
   - what `SwitchDesktop` does today under the X11 and Wayland drivers, and with a virtual desktop;
   - whether `SendInput`, `keybd_event`, `PostMessage`, `SendMessage`, `SetWindowsHookEx`,
     `AttachThreadInput`, `SetForegroundWindow` and window enumeration cross desktops;
   - what the host sees: does a second desktop get its own X11 or Wayland surface?
2. **Desktop.** The broker creates `WinSta0\Winlogon` with a DACL that admits only the broker's system role.
   It runs the consent UI there, calls `SwitchDesktop` for the prompt and switches back afterwards, on every
   exit path.
3. **Server enforcement.** While the secure desktop is the input desktop:
   - injected input from a process on another desktop fails, as on Windows;
   - messages, hooks and thread attachment do not cross into it;
   - only the broker can switch back.
4. **Presentation.** The host shows the secure desktop full screen over the user's session, with a dimmed
   snapshot of the previous screen as background. The snapshot is cosmetic and is labelled so in the docs.
5. **Limits.** Native X11 or Wayland clients outside Wine (for example `xdotool`), and same-UID native code,
   can still reach the host surface. That stays T5 and is stated in every status line.

### D4. In-tree callers that rely on the bypass

Each must work through the broker when enforced, or be shown not to need elevation:

| Caller | Today | Under enforcement |
| :--- | :--- | :--- |
| `dlls/shell32/shlexec.c` `runas` and `ERROR_ELEVATION_REQUIRED` | linked token + `CreateProcessAsUserW` after the helper approves | broker request; the helper exit code is no longer trusted |
| `dlls/ntdll/loader.c` `elevate_token` | silent elevation when helper unset | unchanged (never runs when enforced) |
| `dlls/msi/custom.c` elevated custom actions | linked token + `CreateProcessAsUserW` | broker request, or run unelevated with an explicit FIXME; decide after testing an installer |
| `programs/explorer/desktop.c` | `ProcessWineGrantAdminToken` | runs with the user's limited token (Q3); fix what breaks |
| `programs/winetest/main.c` | linked token for elevated tests | test-only; leave, document |
| `programs/consent` | consent dialog, helper exit code | becomes the broker's UI for click-only policy outside enforcement; under enforcement the polkit agent is the UI |

## Steps

Commit after each working step. Each step lists how it is accepted.

1. **Native control.** Done on Windows 10.0.26300 (section "Native results"): N1 to N5 on x64 and x86, N6 on
   x64.
2. **Bypass probes.** Add N1–N5 and `ProcessWineGrantAdminToken` cases to
   `scripts/test_token_security_regressions.cpp`, and N1-N5 as conformance tests in
   `dlls/advapi32/tests/security.c` next to the existing linked-token checks. Run them with the helper
   configured.
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
5. **Secure desktop (D5).** Research probes first, then the desktop, the server enforcement and the
   presentation.
   Accept, with a probe on the default desktop:
   - `SendInput` and `PostMessage(BM_CLICK)` aimed at the consent dialog have no effect;
   - hooks and `AttachThreadInput` fail;
   - the user's real keyboard and mouse reach the dialog;
   - every exit path (approve, cancel, timeout, broker crash) returns to the default desktop.
6. **Consent and polkit authorization (D3.3).** Click consent on the secure desktop for policy 2, and the
   polkit policy file and `CheckAuthorization` for policies 1 and 3. Test with a system `dbus-daemon` and
   `polkitd` started in the test container, and two accounts with known test passwords: one administrator
   (in `sudo`), one not.
   Accept:
   - approve launches exactly one target;
   - cancel, close, no agent, no polkitd and a timeout launch nothing and return distinct errors;
   - for credential policies, a valid admin password launches the target, while wrong, empty and arbitrary
     passwords and a valid non-admin password do not;
   - no password or cookie appears in any log or argv.
7. **Caller migration (D4).** Move `shell32` and `msi` to the broker under enforcement, and settle explorer.
   Accept:
   - `runas`, `requireAdministrator` and `highestAvailable` work under standard, filtered and full tokens;
   - a 32-bit and a 64-bit benign installer elevate through the broker;
   - installers named in Wine bug 40613 and its Wine-Staging discussions that need elevation still install;
   - the B4 smoke test and the full harness pass.
8. **Documents.** Update `ELEVATION.md` (B1 design becomes implemented for the filtered-admin route),
   `STOCK_CHANGES.md`, `TOKEN_SECURITY.md` and the acceptance matrix. B2 can move to PASS only for the
   filtered-administrator route; standard-user elevation stays NOT SUPPORTED.

## Decisions (2026-10-22)

- **Q1.** Enforcement is tied to a configured elevation helper.
- **Q2.** The server recognises the broker as a process it started in a system role. This is a Win32 boundary
  only.
- **Q3.** Explorer runs with the user's limited token under enforcement.
- **Q4.** Instead of requiring credentials for every elevation, implement a secure desktop (D5). Click consent
  on the secure desktop is the default; credentials stay for policies 1 and 3.
- **Q5.** The Microsoft documentation settles the direction of N2 to N5 but not the exact codes, N1 or N6. A
  worker on one of your Windows machines ran the native probe (step 1, done).

## Risks

- Installers and launchers that silently relied on the bypass will prompt or fail under enforcement. Step 6
  must test real installers, not only probes.
- polkitd and a system bus are not normally running in containers; the tests must start their own and must not
  touch a real machine's polkit configuration.
- Same-UID native code can still attack the server or the broker directly. That stays T5 and must be stated in
  every status line.
