<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Deferred custom actions without impersonation run as SYSTEM (msi; B108)

Status: design note, draft 3, 2026-11-26. **The reviewer's GO on B** with points a-g folded in (the boundary stated
plainly, binaries written by the host, lifetime through rollback and commit, script actions, deferred-action limits,
a separate `msiserver`, the measurement, done). **The orchestrator's go for B (2026-11-26), with four conditions** written
in below: B is an interim (see "B is an interim"); the gate exactly as proposed, with refusal tests; the per-install
folder made by the SYSTEM side; its own branches and batch, with B108 as the acceptance case. The measurement it asked
for first is done ("What Windows does"). Asked for by the orchestrator after the `\BaseNamedObjects` audit
(`SESSION0_CROSS_SERVER.md`, "Order: a proposal").

**Draft 4 (2026-11-26), before building:** reading the code for the implementation found five places where draft 3
assumed more than exists; "Draft 4: what the code allows" (after the proposal) corrects them. Points 1 (the gate) and
3 (transport) change, the rest of B stands. **GO from both (2026-11-26)**, with conditions written into that section:
the reviewer's (no full-token requirement, the endpoint not squattable, one host per architecture, GetMode, untrusted
replies, leftovers, tests) and the orchestrator's (an audit event per action, logged refusals, the folder).

## The problem

Windows Installer runs a deferred custom action that has `msidbCustomActionTypeNoImpersonate` (0x800, with
`msidbCustomActionTypeInScript`, 0x400) as LocalSystem when the installation runs elevated (a per-machine install by an
administrator), and as the installing user otherwise. Wine's msi ignores the bit
(`dlls/msi/custom.c`, `ACTION_CustomAction`: "msidbCustomActionTypeNoImpersonate not handled"), so under WineExplorer
such an action runs as the elevated user, in the elevated `msiexec` that the broker started after the consent prompt
(`msi_install_through_broker`, B4's decision: Vista's way).

That is B108's remaining gap for an Edge or Copilot MSI run by the user: the MSI's `DoInstall` (deferred, no
impersonation) starts Omaha (`MicrosoftEdgeUpdate.exe /silent /install`, `/handoff`, the mini installer, `setup.exe`).
On Windows 11 (measured on `WinRef-EFR`, B108) that whole chain runs as SYSTEM and the update service as SYSTEM in session
0: one identity, one kernel, so the service sees its worker and does not uninstall itself. With the whole chain as
SYSTEM in session 0 (the components' way, `MachineWine`), the MSI installs under WineExplorer today (B108, after class
60). Run by the user it fails with "CanUninstallGoogleUpdate returned 1".

The rule is a conformance fix in its own right: any package whose deferred actions expect SYSTEM (writing ACLs for
SYSTEM-only keys, starting services, registering machine COM servers with a SYSTEM owner) behaves differently today.

## What Windows does (behaviour; documented API and black box)

- The `msiserver` service (LocalSystem, session 0) runs the execution script of an elevated install. Impersonated
  deferred actions run as the user (the elevated user's token); actions without impersonation run as LocalSystem.
  Immediate actions run in the client, as the user.
- Custom action DLLs run in a separate host process (`msiexec.exe -Embedding`), one per architecture and identity.
- **The session:** B108 saw the no-impersonation chain as SYSTEM in the user's session 1, not session 0, and the
  measurement below confirms it for DLL and EXE actions: the host runs with a SYSTEM token whose session is the
  client's, and processes it starts (Omaha) are SYSTEM in session 1.
- Not elevated (a per-user install by a standard user, no `AlwaysInstallElevated`): no-impersonation actions run as the
  user; there is no SYSTEM.

**Measured 2026-11-26** on the lab's Windows oracle `Copilot-Win10-B` (Windows 10 Pro; `WinRef-EFR` is no longer
registered), with our own test package: no files, nothing registered, five actions that write the token of the
process they run in (probe sources in this session's scratch, `caprobe`). `lab` (an administrator) at the console,
session 1. Elevated: from the console's Run box with the VM's own consent prompt, `ALLUSERS=1`. Not elevated: the
limited token (guest control), `ALLUSERS="" MSIINSTALLPERUSER=1`. Both installs returned 0.

| Install | Action | Process | User | Session | Elevation | Integrity |
|---|---|---|---|---|---|---|
| elevated, per machine | immediate DLL; deferred DLL and EXE with impersonation | `MsiExec.exe` host; the EXE from `C:\Windows\Installer\MSI*.tmp` | the user | 1 | full (type 2) | High |
| elevated, per machine | deferred DLL and EXE **without** impersonation | the "Elevated Non-remapped custom action server"; the EXE from `C:\Windows\Installer\MSI*.tmp` | **SYSTEM** | **1** | type 1 | System (19 privileges) |
| not elevated, per user | all five, the two without impersonation included | `MsiExec.exe` host; the EXE from `C:\Windows\Installer\MSI*.tmp` | the user | 1 | limited (type 3) | Medium |

So: SYSTEM only for an elevated install, and then **in the user's session**; the binaries Windows runs are extracted
by the service to `C:\Windows\Installer\MSI*.tmp`, not to a path the client names. This is option A's identity and
session.

## What WineExplorer has to work with

- The elevated `msiexec` (full token, after the consent prompt) runs in the user's server, in the machine root: its
  `C:` is the machine's, the package is opened by its `Z:` path (`path_for_broker`), so the package, the temporary
  folder and the binaries msi extracts are files session 0 can open too.
- Custom action DLLs run in `msiexec -Embedding <client pid>` (`custom_start_server`): the host connects to the client's
  pipe `\\.\pipe\msica_<pid>_<arch>` and calls back over `ncalrpc` (`MsiGetProperty` and the rest). EXE actions are
  started by the client itself (`CreateProcessW`).
- A SYSTEM identity exists in two places: session 0's processes (SYSTEM in session 0's server), and the session's system
  role (SYSTEM in the user's own server, E1).
- Named pipes are bridged from a session's programs to session 0's pipes (T5.2).

## Options

**A. SYSTEM in the user's server (the session's system role).** The elevated `msiexec` starts the host for
no-impersonation actions with the system role's token. Matches Windows' identity and session exactly and needs no
cross-server transport. **Does not fix B108**: Omaha's service is in session 0's server and still cannot see a process
of the user's server, whatever its identity; that needs M3 (X3, X4). Also a new way for an elevated user to obtain the
session's SYSTEM role (today the role is for the session's own services), so its own review.

**B. SYSTEM in session 0 (an `msiserver` role in session 0).** The elevated `msiexec` asks a session 0 service to start
the host as SYSTEM in session 0; no-impersonation DLL and EXE actions run there, impersonated ones stay in the client
as today. This is B108's measured working configuration for the part that matters (Omaha's chain and its service in
one server, as SYSTEM). Differences from Windows: the session (0 instead of the user's): such an action cannot show UI
on the user's desktop (Windows advises against UI in these actions, and session 0 isolation hides it there too) and
does not see the user's server's objects (a named object of the client, its windows).

**C. The whole execution script in session 0** (Windows' model: `msiserver` executes, the client only shows UI).
Closest to Windows, but impersonated actions would then need the user's identity in session 0, which cannot open the
user's home (another Linux account); and it reverses B4's decision (the elevated `msiexec` runs the install). Not
proposed.

## Proposal: B

1. **Who may ask.** Only an elevated `msiexec` running an install that is per machine, i.e. one that the consent prompt
   approved: its token is the full token of a member of Administrators (`TokenElevationTypeFull`), checked by session 0
   on the caller's described token (`link_bound_token`), and the install is per machine (`ALLUSERS=1` resolved, the
   condition `msi_package_file_is_machine` already uses). A standard user's per-user install never asks; its
   no-impersonation actions run as the user, as on Windows.
   **The decision rests on session 0's own record**: the token session 0 bound for the caller over its link
   (`link_bound_token`, the account binding and the elevation session 0 itself recorded for the consent), never on a
   description the user's server sends with the request.
2. **What is asked.** One request per install and architecture: "start the custom action host for install N". Session
   0 starts `msiexec -Embedding` as SYSTEM in session 0. The host only runs actions the client sends over its own
   connection; it accepts no other client.
3. **Transport.** The bridge carries a session's opens to session 0's pipes, not the reverse, so the endpoints reverse
   for this host: the session 0 host creates its pipe (name from session 0, with a random part, DACL for the client's
   SID only) and the client connects; the callbacks go over that connection instead of `ncalrpc` to the client.
   **Only what a deferred action gets** (the reviewer's point e): `CustomActionData`, `ProductCode`, `UserSID`, the
   message and log calls (`MsiProcessMessage`), as msi already restricts deferred actions today. The client enforces
   it on its side, as msi does now, and opens no database call wider than that.
4. **Files.** The client sends each action's binary (the DLL, the Binary-table EXE or the script) over the host's
   pipe; **the session 0 host writes it** into a folder it owns (under the machine root, created by the SYSTEM side
   before the first binary, SYSTEM and Administrators only) that the user's Linux uid cannot write, and loads or
   starts it from there. Nothing can swap a binary between write and load (no other admin's uid either), and the
   client never names a path the host executes: a request names the action, and the host takes the binary it wrote.
   This matches Windows, where the service extracts the binaries to `C:\Windows\Installer\MSI*.tmp` (measured). An
   EXE action naming an installed file is started by the host from the package's own directory table (machine
   paths); one naming a file in the user's profile is refused with a FIXME (Windows can, here session 0 cannot read
   the profile).
5. **Action kinds** (the reviewer's point d): DLL actions (type 1 and its in-script forms) and EXE actions (types 2,
   18, 34, 50 with in-script and no impersonation) run in the host, as measured on Windows. Script actions (VBScript
   type 6, JScript type 5, and their forms, in script with 0x800) run in the host on Windows too; they are covered the
   same way (the script text sent like a binary) if msi's script host works in the host process, else they stay in the
   client with an explicit FIXME naming the action ("script custom action without impersonation runs as the user").
6. **Lifetime** (the reviewer's point c): the host lives through the whole script, including the **rollback** and
   **commit** phases, whose no-impersonation actions also run as SYSTEM after the install failed or finished; it ends
   after the last of them, or when the client goes away. Ending the host never ends the processes an action started
   (Omaha's service stays, as on Windows).
7. **Children.** Processes an action starts are SYSTEM in session 0, which is what B108 needs.
8. **Log.** Session 0 writes an event (the package, the user, the action name) for each host it starts.
9. **Who serves it** (open question 2, the reviewer's choice): a separate `msiserver` service in session 0, as XP has,
   which keeps parity and keeps `wexelevate`'s surface small. The caller check is shared with `wexelevate` as a
   library, not a second copy.

**The boundary, plainly** (the reviewer's point a). The elevated `msiexec` runs as the same Linux uid as the user's
limited processes, which can ptrace it or talk to the user's server, so "only an elevated msiexec" protects no more
than WineExplorer's existing elevation does. That is accepted: an elevated administrator can already reach SYSTEM in
session 0 by other ways the system allows (installing a service, which session 0 starts as LocalSystem; machine COM
registrations), so B gives an administrator, after consent, no power they lack, and gives a standard user nothing.
The boundary is session 0's own record of the consent (point 1). The refusal of an elevated program that is not
`msiexec`, and its test, are **hygiene, not a boundary**. The per-install request, the host's single client and the
binaries the host writes itself keep the SYSTEM host tied to the install it was started for.

## Draft 4: what the code allows

1. **Session 0 has no record of the consent.** `link_bound_token` (`server/session0.c`) vouches for the account (the
   link's uid maps to the token's user SID) and the session (the link's); elevation, groups and integrity are what the
   user's server describes ("rests on the launcher-started server", the comment there), as for every bridged caller.
   No server keeps a per-link consent. So point 1 becomes: the gate is **the server's own caller class** on the
   bridged connection, `wex_caller_class_of_pipe` = `WEX_CALLER_ELEVATED_ADMIN` (`token_caller_class`: Administrators
   enabled, High integrity or more, not limited; the same decision wevtsvc's and the accounts service's elevated
   verdicts use). No further full-token requirement (the reviewer): a default-type token with Administrators enabled
   at High integrity is an unfiltered administrator (UAC off, the built-in Administrator), whose per-machine install
   Windows also runs as SYSTEM; only a filtered (limited) token is refused. **Elevation and the Administrators
   membership are the user's server's attestation (T1)**, as for wevtsvc and the accounts service; the account and the
   session are session 0's own record of the link. A link-bound SYSTEM, LocalService or NetworkService gets the USER
   class, so a session's system role cannot pass. A per-link consent record in session 0 would be new work across E1
   and T1, not part of B; the boundary paragraph above already says what elevation protects. Follow-up scheduled by
   the orchestrator: `token_caller_class` checks a link-bound token's Administrators membership against session 0's
   own accounts record, so that only the elevation rests on attestation.
   **In place of the consent record, an audit** (the orchestrator): session 0 writes a System-log event (source
   `WexMsiServer`) for every host it starts (event 1), every refusal (2, with the reason) and every action the host
   runs (3, written by the host after the action: the action, its status and exit code, the package's path and
   ProductCode, the caller's account and session as the service told the host).
2. **Per machine is the client's statement.** Session 0 cannot evaluate the install's context (command-line
   `ALLUSERS`); the elevated `msiexec` asks only when `package->Context == MSIINSTALLCONTEXT_MACHINE`, and states it in
   the request. A per-user install therefore never asks (test: its no-impersonation actions run as the user); session 0
   refuses a request that states per user (hygiene, like the next point).
3. **"Is it msiexec" cannot be checked across the bridge**: session 0 sees a leased pid that names no process it can
   open. Dropped (it was hygiene, the boundary paragraph says so); the request carries no executable path.
4. **The service name.** `MSIServer` is taken (Wine's stub service, `msiexec /V`, a session service). The new one is
   `WexMsiServer`, session 0 only (`WexStartInSession0Only`), auto start, LocalSystem; pipe
   `\\.\pipe\wexmsiserver`, bridged as `WexSession0\wexmsiserver` (one stock line in `pipe_bridge_allowed`, needs a
   session link, like `wexelevate.r9`).
5. **Transport: one connection per install and architecture, handed to the host.** The bridge carries only a session's
   opens to session 0's pipes, so the client opens `wexmsiserver`; the service checks the gate on that connection,
   starts the host (`msiexec -WexSysHost <handle>` as SYSTEM in session 0, a plain child of the LocalSystem service)
   and gives it the connected pipe instance by inheritance; the next client gets a new instance. The connection is the
   host's only client by construction (point 2 of B). No per-install pipe name, so no bridge rule beyond point 4.
6. **The host's msi calls.** Wine's DLL host calls back the client over `ncalrpc` (`msi<client pid>`) and the client
   waits on the host's thread handle; neither crosses servers. The session 0 host registers its own local `ncalrpc`
   endpoint and implements `IWineMsiRemote` there by forwarding each call over the connection to the client, which
   answers from its package. msi's remote-handle code in the action is unchanged.
7. **Deferred limits are new code.** Wine's msi gives a custom action server the whole package
   (`s_remote_GetActionInfo` returns the package handle; `s_remote_GetProperty` any property). The client answers the
   host's forwarded calls from an allow-list only: `GetProperty` of `CustomActionData`, `ProductCode` and `UserSID`
   (what Windows gives a deferred action), `ProcessMessage`, `GetMode`, `GetLanguage`, `FormatRecord`, `CloseHandle`,
   `GetActionInfo` (its own action's info); everything else returns `ERROR_ACCESS_DENIED` (test: a database call from a
   SYSTEM action is refused). Impersonated actions keep today's behaviour.
   **As built:** the forwarding is a relay of the action's own ncalrpc channels, filtered in the client by call and
   argument: a bind only to msi's interface; a request only of those operations, each on the restricted handle the
   client gave a host action (`GetActionInfo`: an action of the host), otherwise a fault with `ERROR_ACCESS_DENIED`.
   The restricted handle is never the whole package: on it `GetProperty` answers the three properties (others empty),
   `GetMode` answers `SCHEDULED`, `ROLLBACK` and `COMMIT` and FALSE otherwise, `FormatRecord` formats without the
   package's properties. The host reads the client's frames as untrusted (sizes that add up, terminated strings, RPC
   answers with a valid header); a malformed one ends the connection.
   **The endpoint** (the reviewer): a random name chosen by the host, created with `FILE_FLAG_FIRST_PIPE_INSTANCE`
   before the host reports ready (so before any action loads), DACL SYSTEM only.
8. **Script actions** run in the client in Wine (not in the `-Embedding` host). Point 5 of B's answer: they stay in the
   client with the FIXME naming the action, until the host can run msi's script host.
9. **The event** for each host started: written with the event log protocol (`wine/wex_eventlog.h`, the System log;
   `ReportEventW` is still a stub).
10. **The folder** is protected by Unix permissions too (owned by the session 0 account, mode 0700): the elevated
    client keeps the system group's raised file access, so an NT ACL alone would not keep it out. As built the host
    sets only the NT ACL and relies on the session 0 account's ownership and umask for the Unix mode; whether the
    elevated client's group access can write it is still to be checked on a run (not yet verified). The host removes it
    when the install's script ends, successfully or not (the client's END, or its connection breaking); the service
    removes a crashed host's folder when it starts.
11. **An existing case changes:** `c7_msi` asserts `elevation=2` for its 3138 action (deferred, no impersonation);
    under B that action is SYSTEM in session 0, so the case moves to the new section's expectations. As built, the
    tests are in `c7_msi` itself: the EXE and 32- and 64-bit DLL actions as SYSTEM in session 0 with a deferred
    action's view, an impersonated DLL action as the elevated user, the folder gone, the gate's refusals (a limited
    administrator, a standard user, an elevated per-user request) logged, every SYSTEM action audited, and an elevated
    per-user install whose action without impersonation runs as the user. Not covered: a default-type administrator's
    install (no fixture makes such a token yet).

## B is an interim

Windows runs the host as SYSTEM in the **user's** session (option A); B runs it in session 0 because only there does
B108's chain share a server with Omaha's service today. Once X3 and X4 (M3 across servers) land, B108 is retested
with option A; if A works, B goes.

What behaves differently in session 0 (known kinds, for a package whose no-impersonation action does them):

- **A window.** An action that shows a dialog or a message box (against Windows' advice for these actions): session 0
  has no visible desktop. The host does not refuse it: the window is created on session 0's desktop, invisible, as
  under session 0 isolation on Windows when a service shows one; a modal one waits for its own timeout or the
  install's cancellation. The host logs a FIXME once per action that creates a top-level window.
- **Starting the installed program** for the user ("launch after install" done in a deferred no-impersonation
  action): it starts as SYSTEM in session 0, invisible, where Windows starts it as SYSTEM in the user's session
  (visible). Usually done as an immediate action in the client instead, which is unaffected.
- **Session-scoped things:** `Local\` and unprefixed names (the host's are session 0's, which is `\BaseNamedObjects`),
  the session id (`ProcessIdToSessionId`, `WTSGetActiveConsoleSessionId` comparisons), the user's processes
  (enumerating them, waiting on one: not visible from session 0 before X3), the user's window station and clipboard.
- **The user's profile** (files in the user's home: not readable by session 0's account). Windows' SYSTEM can read them.

## Tests (when built)

- A test MSI with three deferred actions (impersonated, not impersonated, an EXE without impersonation) that each
  write their token (user, session, elevation) to a file: elevated per-machine install: user, SYSTEM in session 0,
  SYSTEM in session 0; a standard user's per-user install: the user three times. 32-bit and 64-bit actions.
- **The gate (the orchestrator's condition):** a non-elevated caller asking is refused; a per-user install asking is
  refused (even elevated); an impersonated action of an elevated per-machine install stays in the elevated `msiexec`.
- Refusals besides: a second client on the host's pipe, a request after the install ended, a request naming a path
  (none is accepted: the host runs only binaries it wrote), a callback beyond what a deferred action gets; and, as
  hygiene, an elevated program that is not `msiexec` asking.
- The folder: written only by the host (the user's uid cannot write it), gone after the install.
- Rollback and commit: a no-impersonation rollback action (forced failure) and a commit action run as SYSTEM in the
  host; a process an action started outlives the host.
- A script action without impersonation: in the host, or the FIXME (whichever point 5 settles).
- **Acceptance (B108):** the Edge enterprise MSI run by the elevated user on a clean image (no WebView2) installs.
  B is built on its own WineExplorer and Wine branches and goes into its own batch, with this case retested.
- Plain prefixes and development mode keep stock behaviour (no session 0: the bit stays ignored, with the WARN).

## Stock changes (expected)

`dlls/msi/custom.c` (the no-impersonation path: ask session 0, the reversed connection, the files), `programs/msiexec`
(the host's side of the reversed connection), and the session 0 side as a WineExplorer service or a role of an
existing one (`wexelevate` already serves elevated requests in session 0). Each listed in `STOCK_CHANGES.md`.

## Open questions

1. Windows' token for the host: answered (measured above): SYSTEM in the user's session for an elevated install,
   the user otherwise.
2. Which session 0 process takes the role: answered (the reviewer): a separate `msiserver` service, with the caller
   check shared with `wexelevate` as a library.
3. Whether A is wanted as well later, for packages whose no-impersonation actions need the user's session (UI, the
   session's objects); none is known.
