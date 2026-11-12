<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# System tier (session 0): design

Date: 2026-11-11. Status: design for decision, nothing implemented. Roadmap: "Future: system tier (session 0)" in
[ROADMAP.md](ROADMAP.md). Code facts below were read from the tree on this date (file and line where given); effort
figures are estimates, not measurements.

## Goal

Windows has one registry for the machine and one set of services, run by SYSTEM in session 0; each logged-on user
adds a session with their own `HKEY_USERS\<SID>`. A change to `HKLM` is seen by every session at once, only
administrators and SYSTEM can make it, and a user cannot touch the processes that hold it.

The system tier gives WineExplorer the same shape: one always-running machine-level instance that owns `HKLM`, the
account and policy data and the machine's services, and user sessions that use it. The user's direction from
2026-10-22 (`ELEVATION_SYSTEM_ROLE_WORKORDER.md`, decision S1): "an always-running system service, so that user
sessions are fully separate", with the system role tied to peer credentials and image path, not to "same server".

## Where we are

| Area | Today | Source |
|---|---|---|
| Server | One `wineserver` per user and prefix, started by the first client, socket in `/tmp/.wine-<uid>/server-<dev>-<ino>`, mode 0600; `accept()` has no uid check, every client is the server's own user | `server/request.c` 716-808, 900-975, 675-706 |
| System group | The server is started through the setgid launcher with saved gid `wexsys`; it runs with the user's gid and raises the group only to load and save the machine hives and for system-group clients | `tools/wex-sysrun`, `server/sysrun.c` |
| `HKLM` | Each server loads its own in-memory copy of the machine root's `system.reg` and `userdef.reg`; saved every 30 s and at exit, merge-on-save under a file lock; other sessions see a change at their next logon | `server/registry.c` 3719-3866, 4056-4310 |
| Hive save | In place, not atomic ("upgrade when the system role has its own uid") | `registry.c` 4016-4047 |
| Limited writes to `HKLM` | Access checks under enforcement; where a write passes but the file is not writable, it stays in memory and is dropped | `registry.c` 4113-4147 |
| Key permissions | In memory only; the `.reg` format stores time, class, link and values | `registry.c` 883-904 |
| Notifications | `RegNotifyChangeKeyValue` works inside one server only | `registry.c` 4670-4716, 995-1013 |
| Services | Every session starts `services.exe` (single instance per server, SYSTEM token only for the machine root's image with the system group) and `rpcss`, `plugplay`, `winedevice` | `programs/wineboot/wineboot.c` 1465-1504, `programs/services/services.c` 1396-1463 |
| Sessions | Session id 1 is a constant; window stations are one global list per server | `server/process.h` 197, `server/winstation.c` 45 |
| Policy readers outside Wine | The password verifier parses the saved `system.reg` (WO-38), so a change counts only after a save | `tools/e1s6/e1s6-lab.py` |
| Known gap (T5) | A user can kill their server and start their own; it then controls that session only | `ELEVATION_SYSTEM_ROLE_WORKORDER.md` 29-30 |

## Options

### A. One server for every user

All users' processes connect to one machine-wide `wineserver`.

This is the closest picture to Windows and the wrong shape for Wine. The server is single-threaded and does the
work that the Windows kernel does with the caller's identity, but with its own Unix identity: it opens files for
clients (`server/fd.c`), signals and reads client processes, and maps shared memory with them. With clients of
several Unix users, every such path becomes a confused deputy and needs a per-request identity switch or a
redesign (file opens moved to the client, signalling across uids, `/proc` access). Window stations, the object
namespace and session ids have no per-session scoping today. One crash or hang takes down every user's desktop.
Estimate: months, touching most of `server/`, with no path upstream. Not recommended.

### B. Machine registry service, full proxy

A machine daemon holds `HKLM`; each user's server forwards its registry requests for the machine trees to it
(option (c) of `PREFIX_SPLIT.md`, lines 859-879: about 17 requests, 1500 to 2500 lines).

One image and no merge races, but every `HKLM` read becomes a round trip, and the user's server either blocks on
it (head-of-line blocking for the whole session) or `registry.c` is rewritten to be asynchronous.

### C. Machine server with replicated `HKLM` (recommended)

A second, always-running Wine server instance, the machine server, is the only writer of the machine hives and the
home of session 0. Each user's server keeps what it has today, an in-memory copy of `HKLM`, and becomes a replica:

- **Reads** stay local and unchanged.
- **Writes** to the machine trees are not applied locally first. The user's server sends the change (key, value,
  deletion, security descriptor) to the machine server together with the writer's identity; the machine server
  checks it, applies it, persists it and sends the committed change to every connected user server, the writer's
  included, which apply it to their copies and fire their registry notifications.
- **A change made anywhere** therefore reaches every session within milliseconds, without a logon.

This reuses the code we have: the dirty-key and tombstone replay of merge-on-save already turns a set of changes
into operations on another copy. `registry.c` stays synchronous. The machine server is a normal `wineserver` built
from the same tree, so the registry, token and object code are not duplicated in a separate daemon.

Why not only "save at once and signal the others" (the live-refresh step): it keeps several writers of one file,
keeps the hive group-writable by every user's server, and leaves services and account data where they are. It can
still be the first increment of C (see T1).

## Design (option C)

### Identity and start

- A dedicated Unix account and group for the machine server (working name `wexsys`, the existing group; the account
  is new). It is not root. The machine root's hives and the service data are owned by this account, which removes
  the in-place save shortcut: saves become temporary file, `fsync`, rename.
- Started at boot by a systemd unit, before the display manager; restarted on failure. Its prefix is the machine
  root. It listens on a socket in `/run/wineexplorer/` that user servers connect to.
- Elevated programs keep today's model: the user's uid with the system group as effective group, in the user's
  session, for writing `Program Files`. Only registry writes change path.

### Trust between a user's server and the machine server

The machine server cannot see the Windows token of a writer; it sees a Unix peer. Rule:

- It reads the peer's credentials at accept (`SO_PEERCRED`) and applies the test that `sysrun_server_ok`
  (`ntdll/unix/sysrun.c` 163-193) applies today: all uids are the user's, saved gid is the system group, not
  dumpable. Such a peer is a launcher-started server that the user cannot replace or trace, and its statement
  "this write comes from a token with these groups and this integrity" is believed. The machine server then runs
  the normal key access check with that identity.
- Any other peer (a server the user started by hand, T5) is a read-only replica: it receives changes and every
  write is refused. This closes the registry half of T5.
- The user's uid is always the peer's, never a field in the message.

### Registry

- **Replicated trees:** `HKLM` and `HKU\.DEFAULT`, minus the per-session keys (`is_per_session_key`, `registry.c`
  383-472: display, PnP, volatile session state) and minus tier-only trees (below). `HKCU` stays in the user's
  server.
- **Initial state:** at connect the user's server receives a snapshot (or loads the saved file and then the
  changes since its sequence number).
- **Writes:** synchronous request to the machine server with a short timeout. `HKLM` writes are privileged and
  comparatively rare; the cost per write is one local round trip. To be measured in T0 with an installer run
  before it is fixed.
- **Refused writes fail** with access denied, as on Windows, instead of staying in memory. Keys that Wine or the
  shell must write from a limited session are either per-session keys or get an explicit grant; T0 lists them
  from a traced session.
- **Key security descriptors** are stored by the machine server (a `#sd` line in its hive file, or a side file)
  and replicated, so permissions survive a restart.
- **Tier-only trees:** `HKLM\SAM` and `HKLM\SECURITY` exist in the machine server only and are not replicated
  to user servers (reads go through an API of the tier). The account lockout policy and later the local accounts
  database move there; `NetUserModals` and `net accounts` then call the tier, and the password verifier asks
  the tier instead of parsing the saved file.
- **Machine server down:** user sessions keep running with their last copy, `HKLM` writes fail, the user's
  server reconnects and resynchronises. Nothing is written to the hive file by anyone else.

### Services (session 0)

Decided per service, as the roadmap says:

| Stays in the user's session | Moves to the machine server |
|---|---|
| `rpcss` (COM activation and the running object table serve the user's processes) | The service database and service control manager state |
| `winedevice`, `plugplay`, `mountmgr` (drivers and devices act on the user's processes and drives) | Services that are machine-wide by nature: the elevation broker, Windows Installer, updater services installed by programs |
| Anything that shows a window | A Linux daemon reached through `wexhost` (D-Bus) where that serves better (time, network, power) |

- The service control pipe (`\pipe\svcctl`) and a short list of service endpoints are relayed from the user's
  server to the machine server, with the caller's identity attested as for registry writes.
- A session-0 service that must start a program in a user's session (the broker launching an elevated program,
  an installer service running a custom action as the user) asks that user's server through the launcher; this
  is the hardest part and is its own phase.
- Session ids: the machine server is session 0; it gives each logon a session id, which the user's server uses
  instead of the constant 1.

### What does not change

Per-user servers, windows, graphics, the DWM broker, `HKCU`, profiles, file access by Unix uid, and the E1 R8
protocol. The verifier stays a Unix root service; it gains a better source for policy.

## Phases

| Phase | Content | Depends on | Estimate |
|---|---|---|---|
| T0 | Measurements and lists: `HKLM` writes per session kind (boot, shell, an installer), which come from limited tokens, write latency budget, the per-session key list, the services inventory with the tier decided per service | none | days |
| T1 | Live change feed without a new process: the saving server signals the others, which reload changed keys and fire notifications. Optional stepping stone; gives the visible benefit early and exercises the replica-apply code | none | about a week |
| T2 | Machine server: account, unit, socket, peer check, snapshot and change feed (read-only replicas). Hives still saved by user servers | T0 | 1 to 2 weeks |
| T3 | Writes through the machine server; it becomes the only writer; atomic saves; refused writes fail; persisted key permissions | T2 | 2 to 3 weeks |
| T4 | Tier-only trees: account policy and accounts; `NetUserModals`, `net accounts`, the verifier and Group Policy (`GROUP_POLICY.md`) write and read there | T3 | 1 to 2 weeks |
| T5 | Session-0 services: service database in the tier, pipe relay, the broker and installer service moved; launching into a user session | T3 | weeks, own design |
| T6 | Session ids per logon; removal of the per-session `services.exe` | T5 | 1 week |

## Tests (each phase adds its own)

- Two users logged on at once. An elevated installer in Alice's session registers a COM class and an uninstall
  entry; a program in Bob's session waiting in `RegNotifyChangeKeyValue` is woken and reads them, without a
  logon. A 32-bit and a 64-bit reader.
- A limited program's `HKLM` write fails with access denied in both sessions and never reaches the file.
- A server started by hand (no launcher) reads `HKLM` and cannot write it.
- The machine server is killed during an installer: no torn hive, sessions keep running, writes fail, and after
  the restart the sessions resynchronise.
- Policy: `net accounts /lockoutthreshold:3` in an elevated prompt takes effect for the verifier at once.
- The existing suites (sysrun server, lab S6, installer acceptance) pass unchanged in T2 and with listed
  changes from T3.

## Stock Wine changes expected

`server/registry.c` (replica apply, write forwarding, descriptor persistence), `server/request.c` and a new
`server/` file for the tier connection, `server/sysrun.c` (peer test shared with ntdll), `server/protocol.def`
only if the tier link reuses request structures, `programs/services` and `programs/wineboot` (T5),
`dlls/netapi32` (T4). Each listed in `STOCK_CHANGES.md` when made. None is a candidate for upstream.

## Risks

- The machine server is a single point for `HKLM` writes and later for services. Mitigation: sessions run on
  their copy when it is away; systemd restarts it; it has no windows and no user code.
- A synchronous write round trip in a single-threaded server: bounded by the timeout; measured in T0.
- Replication order: one writer (the machine server) gives a total order; a user's server never applies its own
  write before the commit comes back.
- Programs that expect a limited `HKLM` write to succeed silently. Windows refuses those too; the list from T0
  shows what Wine and the shell themselves rely on.
- Services split across two servers cannot share handles or windows with user processes. The table above keeps
  such services in the session.

## Decisions needed

1. **Shape:** option C (machine server with replicated `HKLM`), as recommended, or B (full proxy), or A.
2. **Identity:** a dedicated `wexsys` account for the machine server (recommended) or root.
3. **Limited `HKLM` writes:** refused as on Windows from T3 (recommended), or kept in memory as today.
4. **T1 first:** take the live change feed as an early step (recommended: yes, it is small and its code is reused),
   or go straight to T2.
5. **Session-0 services:** the per-service table above as the starting list.
