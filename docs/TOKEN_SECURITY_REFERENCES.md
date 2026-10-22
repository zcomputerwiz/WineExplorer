<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Research: tokens, elevation and Chromium compatibility

Researched 2026-10-17. Companion: `TOKEN_SECURITY_WORKORDER.md`.
This is a source review, not a new browser test or a security certification.

## Findings and recommended source hierarchy

Build on Wine, not a new kernel or a parallel token database. Use public API
contracts and observable tests for behavior; ReactOS for implementation ideas;
Samba for Windows/Unix translation design; Chromium for consumer requirements.
No reviewed source supplies a complete drop-in Windows token layer for Linux.

URLs below track moving upstream branches. Before adapting code or tests, record
the exact revision, path, file license and any relevant dependencies in the
implementation report. Do not describe these research links as pinned sources.

### R1. Wine: primary implementation and regression baseline

- Local `wine/server/token.c`: token objects, duplication/filtering, access checks,
  linked-token construction. Start here rather than replacing the server model.
- Local `wine/server/security.h`, `object.c`, `handle.c`, `process.c`, `thread.c`,
  `registry.c`, `file.c`, `winstation.c`, and `protocol.def`: inspect effective
  identity, object access, handle rights and client/server serialization.
- Local `wine/dlls/ntdll/unix/security.c`, `sync.c`, and
  `wine/dlls/kernelbase/process.c`: token, job and mitigation entry points.
- Local `wine/dlls/advapi32/tests/security.c`: existing token, label, duplication,
  process-token and object-security tests. Also inspect ntdll `om.c`, `sync.c`,
  `process.c`, and shell32 `shlexec.c` tests.
- [Upstream security tests](https://github.com/wine-mirror/wine/blob/master/dlls/advapi32/tests/security.c).

Parent HEAD at research time: `a5945fdf`; recorded Wine gitlink:
`ed5fe967ed471ca524666631ad70dabbed6af076`. These identify the inspected checkout's
recorded base, not necessarily a future implementation base or all working edits.
Inspected Wine token/test files carry LGPL-2.1-or-later notices.

### R2. Public Windows API contracts: behavioral authority, not source code

- [Access tokens](https://learn.microsoft.com/en-us/windows/win32/secauthz/access-tokens):
  the model is structured identity and attributes, not a numeric privilege rank.
- [Restricted tokens](https://learn.microsoft.com/en-us/windows/win32/secauthz/restricted-tokens):
  ordinary and restricting SID checks both participate in access decisions.
- [CreateRestrictedToken](https://learn.microsoft.com/en-us/windows/win32/api/securitybaseapi/nf-securitybaseapi-createrestrictedtoken):
  flag behavior, deny-only SIDs and privilege changes. In particular,
  `DISABLE_MAX_PRIVILEGE` has an exception for `SeChangeNotifyPrivilege`.
- [Mandatory integrity control](https://learn.microsoft.com/en-us/windows/win32/secauthz/mandatory-integrity-control):
  token integrity, object labels and mandatory policy are distinct from DACLs;
  an unlabeled object is normally treated as medium integrity.
- [Impersonation levels](https://learn.microsoft.com/en-us/windows/win32/secauthz/impersonation-levels):
  identification is not permission to act as the caller.
- [SHELLEXECUTEINFO](https://learn.microsoft.com/en-us/windows/win32/api/shellapi/ns-shellapi-shellexecuteinfow):
  process-handle behavior matters to callers, not merely launching a Unix PID.

Use documentation as contracts and write original code. No proprietary Windows
source, binaries, copied/traced artwork or undocumented-source leaks belong in
this repository. Public API documentation is not permission to copy proprietary
implementation code. A modern Windows VM, if separately available, is a useful
behavioral control; the XP VM cannot validate Vista+ UAC/MIC/AppContainer behavior.

### R3. ReactOS: strongest open NT-token implementation reference

- [tokenlif.c](https://github.com/reactos/reactos/blob/master/ntoskrnl/se/tokenlif.c):
  token creation, duplication and filtering.
- [token.c](https://github.com/reactos/reactos/blob/master/ntoskrnl/se/token.c):
  base token routines, comparisons and context handling.
- [tokencls.c](https://github.com/reactos/reactos/blob/master/ntoskrnl/se/tokencls.c):
  information-class sizing and query/set handling.
- [accesschk.c](https://github.com/reactos/reactos/blob/master/ntoskrnl/se/accesschk.c):
  access-check organization and rights evaluation.

All four inspected headers say GPL-2.0-or-later. They are kernel code with ReactOS
dependencies, not a ready-made wineserver library. Do not assume modern MIC,
AppContainer or Chromium completeness from API names. Validate individual paths.
GPL-2.0-or-later fits the project's overall license, but copying it into an LGPL
Wine module may prevent retaining an LGPL-only upstream patch. Prefer original
Wine-compatible implementations; document any deliberate GPL import and obtain
review of the licensing/upstreamability decision before importing it.

### R4. Samba: strongest Windows-to-Unix translation precedent

- [source4/auth/unix_token.c](https://github.com/samba-team/samba/blob/master/source4/auth/unix_token.c):
  converts Windows security-token SIDs into a Unix UID/GID/group context.
- [libcli/security/access_check.c](https://github.com/samba-team/samba/blob/master/libcli/security/access_check.c):
  Windows-style security descriptor and access-mask evaluation.

Both inspected files are GPL-3.0-or-later: **design reference only for this work
order; do not copy code without a separately approved licensing decision**.
Samba is request-oriented server software, not a full local Windows process
security layer. Its SYSTEM-to-root mapping is not our policy: Windows SYSTEM and
Windows administrators must not automatically become Linux root.

### R5. Chromium: concrete consumer behavior and tests

- [Sandbox design](https://chromium.googlesource.com/chromium/src/+/HEAD/docs/design/sandbox.md):
  broker/target separation and startup-token lowering. The browser's sandbox
  broker is an ordinary-user process, not our privileged elevation service.
- [sandbox_win.cc](https://github.com/chromium/chromium/blob/main/sandbox/policy/win/sandbox_win.cc):
  actual per-process policy, including AppContainer/LPAC variants. Check this
  alongside the design document, which includes historical descriptions.
- [restricted_token_utils.cc](https://github.com/chromium/chromium/blob/main/sandbox/win/src/restricted_token_utils.cc)
  and [restricted_token.cc](https://github.com/chromium/chromium/blob/main/sandbox/win/src/restricted_token.cc):
  token-level construction and required operations.
- [restricted_token_unittest.cc](https://github.com/chromium/chromium/blob/main/sandbox/win/src/restricted_token_unittest.cc):
  a useful starting acceptance suite, not proof of full browser containment.
- [process_mitigations.cc](https://github.com/chromium/chromium/blob/main/sandbox/win/src/process_mitigations.cc):
  inventory of requested mitigations; accepted flags are not evidence of enforcement.
- [Chromium LICENSE](https://github.com/chromium/chromium/blob/main/LICENSE):
  BSD-style terms for Chromium-authored code. Check each copied file and its
  dependencies; third-party subtrees can have other licenses. Preserve notices.

Pin the tested Chromium executable version and corresponding source revision;
HEAD policy alone cannot establish the needs of that executable. Prefer freely
redistributable Chromium for the first test; do not add vendor browser binaries
or runtime payloads to Git or the ISO without a separate distribution review.

### R6. Linux and polkit: backend mechanisms, not NT replacements

- [Kernel credentials](https://docs.kernel.org/security/credentials.html):
  `struct cred`, credential lifetime and kernel-side subjective overrides.
  `override_creds()` is not a userspace Windows impersonation API.
- [polkit](https://www.freedesktop.org/software/polkit/docs/latest/polkit.8.html):
  authorization of privileged mechanisms and authentication agents; it is not
  the token/object-access engine. Reuse the existing wexhost agent.
- [no_new_privs](https://docs.kernel.org/userspace-api/no_new_privs.html),
  [seccomp](https://docs.kernel.org/userspace-api/seccomp_filter.html), and
  [Landlock](https://docs.kernel.org/userspace-api/landlock.html): later hardening
  mechanisms. Irreversible restrictions need careful placement around Wine's
  loader and thread-token startup sequence. Seccomp alone is not a full sandbox.

These are design references. No kernel-code import or custom Linux kernel is
required by the initial functional milestones.

## Locally confirmed gaps, not an exhaustive audit

At the recorded Wine base, `NtFilterToken` ignores flags/restricting SIDs;
integrity setters return success without storing the requested level and queries
report high; LowBox creation returns success with a null handle; job UI limits
and kernelbase mitigation APIs are success-returning stubs. Existing DACL/token
machinery does exist: saying Wine has no security model at all would be wrong.
Linked-token queries construct an opposite-elevation admin token on demand.
`runas` currently obtains an admin token without the planned approval flow.

Reproduce these observations against the selected implementation base before
editing. They describe compatibility/semantics gaps, not newly demonstrated
exploits. `WEBVIEW2.md` records a working rendering workaround but explicitly does
not establish sandbox enforcement. Linux confinement and attack-resistance are
deferred; ordinary functional permission denials remain part of initial scope.

## Research access limitations

Raw GitHub retrieval of Samba `unix_token.c` failed; the official repository's
GitHub file view succeeded. Some Gitiles Chromium source views failed; the
Chromium project's GitHub source mirror succeeded. A guessed ReactOS
`modules/rostests/apitests/ntdll/NtFilterToken.c` URL could not be retrieved and is
not a verified test reference. Use the inspected Wine and Chromium tests above.

## Native UAC linked-token contract (2026-10-22)

Probe: `scripts/native_token_contract_probe.cpp` (x64 SHA-256 `07a76e4534f0fff6a87d23209bfab4eca71243bdf57325156faa26639ae0805a`,
x86 `bd5846e4ed807f66f7724e243fdca1ebb0a298c425def10787f5e472d27afe4b`). Windows 10.0.26300, x64 and x86 builds
gave identical results. Run by the project owner's worker as a filtered administrator. The raw output files
are kept outside the repository.

| Case | Result |
| :--- | :--- |
| own token | primary, `TokenElevationTypeLimited` |
| `TokenLinkedToken` | impersonation, `SecurityIdentification`, `TokenElevationTypeFull` |
| `CreateProcessAsUserW(linked)` | 1346 `ERROR_BAD_IMPERSONATION_LEVEL` |
| `CreateProcessWithTokenW(linked)` | 1314 `ERROR_PRIVILEGE_NOT_HELD` |
| `DuplicateTokenEx(linked)` to primary and to impersonation | 1346 |
| `NtSetInformationProcess(ProcessAccessToken, linked)` | `0xC00000A5` `STATUS_BAD_IMPERSONATION_LEVEL` |
| `SetThreadToken(linked)`, then open file / process token / registry key | succeeds; 1346 / 1346 / 5 |
| `CreateProcessAsUserW` with own token duplicated as impersonation token | succeeds |
| `ProcessAccessToken` with own primary copy and own restricted copy | both succeed |

SYSTEM with `--tcb` (scheduled task, x64; the first SYSTEM run took the non-TCB path and printed
`user.linked_error=1312`):

| Case | Result |
| :--- | :--- |
| `WTSQueryUserToken` | primary, `TokenElevationTypeLimited` |
| `TokenLinkedToken` | **primary**, `TokenElevationTypeFull` |
| `CreateProcessAsUserW(linked)` | succeeds, full-token child |
| `CreateProcessWithTokenW(linked)` | succeeds; child exit `0xC0000142` (desktop access from a SYSTEM task, not a token result) |
| `DuplicateTokenEx(linked)` to primary / impersonation | both succeed (impersonation copy at `SecurityImpersonation`) |
| `ProcessAccessToken(linked)` | `0xC0000061` `STATUS_PRIVILEGE_NOT_HELD` (probe does not enable `SeAssignPrimaryTokenPrivilege`) |
| `SetThreadToken(linked)` | 1349 `ERROR_BAD_TOKEN_TYPE` (primary token) |

## Native filtered and elevated token, registry ACLs (D1, 2026-10-22)

Measured on Windows 10.0.26300 (26H2) with `whoami /all`, `reg` and the registry security descriptors, for an
administrator account.

Filtered (non-elevated) token:

| Item | Value |
| :--- | :--- |
| Privileges | SeShutdownPrivilege (Disabled), SeChangeNotifyPrivilege (Enabled), SeUndockPrivilege (Disabled), SeIncreaseWorkingSetPrivilege (Disabled), SeTimeZonePrivilege (Disabled) |
| Groups | Administrators and S-1-5-114 deny-only; Everyone, Users and Authenticated Users enabled |
| Integrity label | Medium, S-1-16-8192 |

Elevated token: 24 privileges, of which SeDebugPrivilege, SeChangeNotifyPrivilege, SeImpersonatePrivilege and
SeCreateGlobalPrivilege are enabled (SeDebug may be enabled only because of how the window was launched).
Administrators and S-1-5-114 are enabled, and Administrators is the "Group owner". Integrity label High,
S-1-16-12288.

Registry security descriptors (SDDL; `…` marks a truncated capability SID):

| Key | SDDL |
| :--- | :--- |
| `HKLM\SOFTWARE` | `O:BAG:SYD:PAI(A;CI;KA;;;CO)(A;CI;KA;;;SY)(A;CI;KA;;;BA)(A;CI;KR;;;BU)(A;CI;KR;;;AC)(A;CI;KR;;;S-1-15-3-1024-…)` |
| `HKLM\SOFTWARE\Microsoft`, `HKLM\SYSTEM\CurrentControlSet` | `O:BAG:SYD:(A;;KA;;;BA)(A;CIID;KR;;;BU)(A;CIID;KA;;;BA)(A;CIID;KA;;;SY)(A;CIIOID;KA;;;CO)(A;CIID;KR;;;AC)(A;CIID;KR;;;S-1-15-3-…)` |
| `HKCU\` | `O:BAG:SYD:P(A;OICI;KR;;;RC)(A;OICI;KA;;;SY)(A;OICI;KA;;;BA)(A;OICI;KA;;;<user SID>)(A;;KR;;;AC)(A;;KR;;;S-1-15-3-…)` |
| `HKCU\Software` | as `HKCU\`, with owner SY |
| `HKU\.DEFAULT` | like `HKLM\SOFTWARE`, with the AC and capability entries non-inheriting |

Write tests with the filtered token:

| Test | Result |
| :--- | :--- |
| Create a key under `HKCU` | succeeds |
| Create a key under `HKLM` | denied |
| `reg add HKLM\SOFTWARE\Microsoft` | `ERROR: Access is denied.`, exit 1 |

Wine today: the default descriptors in `server/registry.c` (`key_alloc_default_sd`) already grant Users read and
SYSTEM and Administrators full control, and the user tree goes to the user. They lack the CREATOR OWNER,
RESTRICTED and AppContainer entries. Wine does not access-check keys that have no explicit descriptor, so the
write tests above do not hold in Wine yet; that is the job of the registry enforcement work order. The
descriptors are unchanged here. For the privilege list, Wine defines no SeIncreaseWorkingSet or SeTimeZone, so
its filtered token keeps SeChangeNotify (enabled) and SeShutdown and SeUndock (disabled).

## SYSTEM, LocalService and NetworkService tokens (C3, 2026-10-22): not measured

`token_create_service` (`server/token.c`) follows the documented defaults and the author's memory of `whoami /priv` in
a service; no native run was made, so every privilege state and group below is **provisional**. To replace them, run
`native_token_contract_probe.cpp` (or `whoami /all`) as a LocalSystem, a LocalService and a NetworkService service on the
Windows 10.0.26300 worker and record the groups, the primary group, the owner, the integrity level, the default DACL and
the privileges with their states here.

| Account | Wine today |
| :--- | :--- |
| LocalSystem | user S-1-5-18; groups Administrators (enabled, owner, primary), Everyone, Authenticated Users; System integrity; 17 privileges enabled, 8 disabled (the work order's table) |
| LocalService | user S-1-5-19; groups Everyone, Users, Authenticated Users, SERVICE, LOCAL; owner the account; System integrity; ChangeNotify, CreateGlobal, Impersonate enabled; AssignPrimaryToken, Audit, Shutdown, Undock disabled |
| NetworkService | user S-1-5-20; same groups; ChangeNotify, CreateGlobal, Impersonate enabled; AssignPrimaryToken, Audit, IncreaseQuota disabled |

Wine defines no SeIncreaseWorkingSet or SeTimeZone, which the native service accounts also hold. Not recorded: the native
primary group of the service accounts, CONSOLE LOGON, the logon SID, BUILTIN\Users in the SYSTEM token, and whether
SeBackup and SeRestore are enabled in a LocalSystem service (Wine enables them).
