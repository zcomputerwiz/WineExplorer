<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Deferred custom actions without impersonation run as SYSTEM (msi; B108)

Status: design note, draft 2, 2026-11-26. **The orchestrator's go for B (2026-11-26), with four conditions** written
in below: B is an interim (see "B is an interim"); the gate exactly as proposed, with refusal tests; the per-install
folder made by the SYSTEM side; its own branches and batch, with B108 as the acceptance case. The measurement it asked
for first is done ("What Windows does"). Asked for by the orchestrator after the `\BaseNamedObjects` audit
(`SESSION0_CROSS_SERVER.md`, "Order: a proposal").

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
2. **What is asked.** One request per install and architecture: "start the custom action host for install N". Session
   0 starts `msiexec -Embedding` as SYSTEM in session 0 and ends it when the install ends or the client goes away.
   The host only runs actions the client sends over its own connection; it accepts no other client.
3. **Transport.** The bridge carries a session's opens to session 0's pipes, not the reverse, so the endpoints reverse
   for this host: the session 0 host creates its pipe (name from session 0, with a random part, DACL for the client's
   SID only) and the client connects; the callbacks (`MsiGetProperty`, `MsiSetProperty`, `MsiProcessMessage`, the
   database calls) go over that connection instead of `ncalrpc` to the client.
4. **Files.** Session 0 (the SYSTEM side) creates the per-install folder under the machine root
   (`C:\Windows\Installer\<random>`), owned by SYSTEM with access for SYSTEM and Administrators only, before any binary
   goes there, and tells the client its name. The client writes the action's DLL (or Binary-table EXE) into it; the
   host loads it from there. **The client never names a path the host executes:** a request names the action, and the
   host finds the binary in its own folder by the action's Binary-table key. An EXE action naming an installed file
   is started by the host from the package's own directory table (machine paths); one naming a file in the user's
   profile is refused with a FIXME (Windows can, here session 0 cannot read the profile).
5. **Children.** Processes an action starts are SYSTEM in session 0, which is what B108 needs.
6. **Log.** Session 0 writes an event (the package, the user, the action name) for each host it starts.

**The boundary.** An elevated administrator can already reach SYSTEM in session 0 by other ways the system allows:
installing a service (session 0 starts it as LocalSystem), writing machine COM registrations. So B gives an
administrator, after consent, no power they lack; it gives a standard user nothing. What must not happen: a process
that is not that elevated install (another program of the session, a limited token, an elevated program that is not
msiexec) obtaining a SYSTEM host. Hence the caller check on the described token, the per-install request, the host's
single client, and the host's end with the install.

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
- Refusals besides: an elevated non-msiexec program asking, a second client on the host's pipe, a request after the
  install ended, a request naming a path (none is accepted: the host takes binaries only from its own folder).
- The folder: created by session 0 before the first binary, owner SYSTEM, DACL SYSTEM and Administrators only, gone
  after the install.
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
2. Which session 0 process takes the role: a new `msiserver` service (the Windows name) or `wexelevate`.
3. Whether A is wanted as well later, for packages whose no-impersonation actions need the user's session (UI, the
   session's objects); none is known.
