<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# System tier T5: services in session 0 (design)

Date: 2026-11-12. Status: design, not implemented; implementation starts after the Safe Mode and Vintner Boot
branches are merged (they change `programs/services`). Context: [SYSTEM_TIER_DESIGN.md](SYSTEM_TIER_DESIGN.md),
[SYSTEM_TIER_WORK_ORDERS.md](SYSTEM_TIER_WORK_ORDERS.md).

## Goal

On Windows one service control manager runs in session 0 as SYSTEM; every session's programs reach it through
the `svcctl` RPC endpoint, and it checks each caller with the caller's own token. Machine-wide services
(installer, updaters, the elevation broker) run once, in session 0, out of the users' reach.

Today every WineExplorer session starts its own `services.exe` (single instance per server, SYSTEM role only for
the machine root's image with the system group), with its own copy of every service.

## How services are reached today

* `services.exe` (`programs/services`, about 4200 lines) serves `svcctl` over RPC on the named pipe
  `\pipe\svcctl` of the session's server; `advapi32`'s `OpenSCManager`, `sc.exe` and the Services control panel
  are its clients. Service processes talk to it on `\\.\pipe\net\NtControlPipe<n>`.
* The service manager's access checks impersonate the RPC caller on the pipe (`RpcImpersonateClient`, a named
  pipe impersonation in the server).
* The service database is `HKLM\System\CurrentControlSet\Services`, now held by session 0 (T1); per-session
  state such as `Control\ServiceCurrent` stays in the session (T1 per-session keys).
* Safe Mode (Integration session, merged with the Safe Mode branch): only `SafeBoot\Minimal` or `Network`
  services start automatically, `StartService` refuses others with `ERROR_NOT_SAFEBOOT_SERVICE`; the check is in
  the auto-start loop and in `StartService`, not in `service_start` (which also starts the driver hosts).

## Design

### Split, first in place

`services.exe` is cut into a **back** (database, starting and stopping service processes, the Safe Mode filter,
the `svcctl` server) and a **front** (a relay). Step one keeps both in the session, joined by the relay
described below, so that the split is tested with today's suites before anything moves.

### The back in session 0

Session 0 runs the back as SYSTEM (`--session0` server started with the machine root as its prefix; it starts
`services.exe` itself at start-up, the image from the machine root as today's system role requires). It serves
`\pipe\svcctl` in session 0's namespace. Services whose image is a machine-wide program run there, with the
service account's token (LocalSystem, LocalService, NetworkService), as the session 0 Unix account: they can
write the machine root through the system group and cannot read users' homes.

Prerequisite: programs of session 0's own Unix account on the root-owned machine root. The server already
accepts that prefix with `--session0`; `ntdll`'s `setup_config_dir` needs the same rule for session 0's own
programs (owner root accepted when the server is session 0, checked through the server connection, not the
environment).

### The front in each session: a pipe relay with the caller's token

The front is a small program of the session that creates the session's `\pipe\svcctl`. For each client
connection it:

1. impersonates the client on the pipe (`ImpersonateNamedPipeClient`), takes the impersonation token;
2. asks its own server for a **pipe channel** to session 0 for that token: the same mechanism as the registry
   channel (`get_registry_channel`), extended with the token of the calling thread instead of the process's,
   and a client kind "pipe" whose only allowed requests open `\Device\NamedPipe\svcctl` and read, write and wait
   on that one pipe;
3. relays bytes in both directions until either side closes.

Session 0's `services.exe` then impersonates the pipe client and gets the caller's real token, so every access
check is the caller's, as on Windows. The attestation rule of T1 applies unchanged: session 0 believes the token
only from a launcher-started server.

### What stays in the session

`winedevice` (drivers act on the session's processes and devices), `plugplay`, `mountmgr`, `rpcss` and anything
with a window. The back in session 0 does not start driver services; the session's back does, as today. So a
session keeps a reduced `services.exe` back for its drivers, and the front sends a call for a driver service to
it and every other call to session 0. The service type (`SERVICE_KERNEL_DRIVER`, `SERVICE_FILE_SYSTEM_DRIVER`)
decides.

### Services that need the user's session

A session 0 service that must start a program in a user's session (the broker, installer custom actions run as
the user) asks that user's server through the launcher: this is T6 and is not part of T5.

## Order of work

| Step | Content |
|---|---|
| T5.1 | Front and back in the session, joined by an in-session relay; today's suites pass |
| T5.2 | Pipe channel: thread-token attestation, client kind "pipe", the allowed requests |
| T5.3 | Session 0 starts its own `services.exe` (ntdll prefix rule for session 0's programs); the back serves there |
| T5.4 | The front sends non-driver services to session 0, driver services to the session's back |
| T5.5 | Tests: two sessions, `sc query` sees one state; a service started from one session is running for the other; a standard user cannot stop it; Safe Mode filter in session 0 (Safe Mode VM phase) |

## Open questions

* The `NtControlPipe` of each service process: in session 0 it is session 0's pipe; nothing in a session needs it.
* Service status notifications (`NotifyServiceStatusChange`) across the relay.
* Interactive services (`SERVICE_INTERACTIVE_PROCESS`): refused, as on Vista and later.
