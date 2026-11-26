<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Deferred custom actions without impersonation run as SYSTEM (msi; B108)

Status: design note, draft 1, 2026-11-26, for the reviewer and the orchestrator. Not built. Asked for by the
orchestrator after the `\BaseNamedObjects` audit (`SESSION0_CROSS_SERVER.md`, "Order: a proposal").

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
- **The session:** B108 saw the no-impersonation chain as SYSTEM in the user's session 1, not session 0. So the host
  runs with a SYSTEM token whose session is the client's, and processes it starts (Omaha) are SYSTEM in session 1.
- Not elevated (a per-user install by a standard user, no `AlwaysInstallElevated`): no-impersonation actions run as the
  user; there is no SYSTEM.

To confirm on `WinRef-EFR` before building (the lab can do it in one run): the token of a no-impersonation DLL
action's host (user, session, integrity, privileges) in an elevated install and in a standard user's per-user install;
the same for an EXE action (types 2, 18, 34, 50), which Windows starts from the host rather than the client.

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
4. **Files.** The DLL binary of an action is written by the client to a folder session 0 creates per install under the
   machine root (`C:\Windows\Installer\<random>`, SYSTEM and Administrators only), and the host loads it from there.
   An EXE action from the Binary table goes the same way; an EXE action naming an installed file starts it by its
   machine path; one naming a file in the user's profile is refused with a FIXME (Windows can, here session 0 cannot
   read the profile).
5. **Children.** Processes an action starts are SYSTEM in session 0, which is what B108 needs.
6. **Log.** Session 0 writes an event (the package, the user, the action name) for each host it starts.

**The boundary.** An elevated administrator can already reach SYSTEM in session 0 by other ways the system allows:
installing a service (session 0 starts it as LocalSystem), writing machine COM registrations. So B gives an
administrator, after consent, no power they lack; it gives a standard user nothing. What must not happen: a process
that is not that elevated install (another program of the session, a limited token, an elevated program that is not
msiexec) obtaining a SYSTEM host. Hence the caller check on the described token, the per-install request, the host's
single client, and the host's end with the install.

## Tests (when built)

- A test MSI with three deferred actions (impersonated, not impersonated, an EXE without impersonation) that each
  write their token (user, session, elevation) to a file: elevated per-machine install: user, SYSTEM in session 0,
  SYSTEM in session 0; a standard user's per-user install: the user three times. 32-bit and 64-bit actions.
- Refusals: a limited token asking, an elevated non-msiexec program asking, a second client on the host's pipe, a
  request after the install ended.
- B108: the Edge MSI run by the elevated user on a clean image (no WebView2) installs.
- Plain prefixes and development mode keep stock behaviour (no session 0: the bit stays ignored, with the WARN).

## Stock changes (expected)

`dlls/msi/custom.c` (the no-impersonation path: ask session 0, the reversed connection, the files), `programs/msiexec`
(the host's side of the reversed connection), and the session 0 side as a WineExplorer service or a role of an
existing one (`wexelevate` already serves elevated requests in session 0). Each listed in `STOCK_CHANGES.md`.

## Open questions

1. Windows' token for the host (above), to measure on `WinRef-EFR`.
2. Which session 0 process takes the role: a new `msiserver` service (the Windows name) or `wexelevate`.
3. Whether A is wanted as well later, for packages whose no-impersonation actions need the user's session (UI, the
   session's objects); none is known.
