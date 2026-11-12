<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# System tier (session 0): design

Date: 2026-11-11. Status: design for decision, nothing implemented; revised the same day after research into how Windows and other systems do it (option D replaces the first recommendation). Roadmap: "Future: system tier (session 0)" in
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

## How Windows does it, and what follows for us

Researched 2026-11-11 from public documentation and published research (sources at the end). Each row ends in the
change it makes to this design.

| Windows | Consequence here |
|---|---|
| The registry is part of the kernel. Every process calls it directly; there is no service in between. The access check is made once, when a key is opened, against the caller's token; the handle then carries the granted access. | Programs talk to session 0 directly, not through their user's server (option D below). Access is checked at open and stored with the handle. |
| One namespace holds every hive: the machine hives and each logged-on user's hive (`HKEY_USERS\<SID>` and `<SID>_Classes`), loaded at logon and unloaded at logoff. Services and administrators can reach a user's hive there. | Session 0 should in the end host the user hives too, loaded at logon. That removes the split "machine keys there, user keys here" and lets a service or an elevated administrator reach the right user's hive. |
| The check uses the token: user, groups, integrity level, restricted tokens. | A Unix uid and group cannot express a sandboxed or low-integrity process (Chromium's renderer). The user's launcher-started server attests the token to session 0 when it sets up a process's registry connection; an unattested connection is a standard user by its Unix identity. |
| A limited program's write to `HKLM\Software` is not always refused. Since Vista, for a 32-bit interactive program it is redirected to a per-user virtual store (`HKEY_USERS\<SID>_Classes\VirtualStore\Machine\Software`), and reads merge it back. Not virtualized: services, keys an administrator could not write either, and by default `Software\Classes`, `Software\Microsoft\Windows` and `Software\Microsoft\Windows NT`; flags per key (`reg flags`). XP simply refuses. | The third answer to "what happens to a limited write": refuse, except for that class of programs, whose writes go to the user's virtual store and persist. It replaces today's "succeeds in memory and is lost". |
| Hives are binary files with a write-ahead log. A lazy flush runs about 5 s after a write; on a crash the log is replayed. Key permissions are stored in the hive. | Session 0 saves through a change journal plus periodic checkpoint, not by rewriting the whole text file every 30 s, and stores key permissions. |
| `HKEY_CLASSES_ROOT` is merged from the machine and the user classes in user mode, not in the kernel. | No change: Wine already merges in `kernelbase`. |
| Since Windows 10 1607, layered keys and differencing hives stack a per-container hive over a base hive. A published audit of the registry found 39 issues (44 CVEs), with layering and transactions among the main sources of complexity. | Overlays are the right model for per-session keys, the virtual store and per-package registries (track U1), but not built as a general mechanism now. Session 0's registry surface stays small: no transactions, a registry-only client can send nothing else, the request parser is fuzzed. |

Comparable designs elsewhere:

| System | Pattern | Use here |
|---|---|---|
| GNOME dconf | Readers map the database file and read it without any IPC or system call; one service makes all writes; change notices go over the bus; the client keeps its pending writes locally so that it reads back its own change at once | A later optimisation: session 0 publishes the world-readable trees as a read-only shared mapping, reads cost no round trip and survive a restart of session 0. Needs a mappable binary format and cannot check access per key, so protected trees stay on the socket |
| Android system properties | One writer (init), a shared memory area mapped read-only into every process, writes over a socket with a permission check, a serial counter that readers watch for changes | Same pattern; confirms single writer plus shared read-only data as the standard shape for a machine-wide settings store |
| ReactOS | The registry is in the kernel as on Windows. Its hive library (`sdk/lib/cmlib`, GPL-2.0-or-later, about 8800 lines) implements the binary hive format with security cells and log recovery and is also built as a host library for the `mkhive` tool. Its change notification is a stub | A candidate storage engine for session 0 later (real binary hives, mappable, with permissions and logs). Not a first step: Wine's `registry.c` keeps its own in-memory tree |

## Options

### A. One server for every user

All users' processes connect to one machine-wide `wineserver`. The server does with its own Unix identity what the
Windows kernel does with the caller's: it opens files for clients (`server/fd.c`), signals and reads client
processes, shares memory with them. With clients of several Unix users each such path is a confused deputy.
Window stations, the object namespace and session ids have no per-session scoping. One crash takes down every
desktop. Estimate: months across most of `server/`. Rejected.

### B. Registry forwarded by each user's server

Each user's server forwards its machine-registry requests to a machine daemon (option (c) of `PREFIX_SPLIT.md`,
lines 859-879). Every read becomes a second hop through a single-threaded server, which blocks the session or
forces an asynchronous `registry.c`. Rejected.

### C. Replicated `HKLM`

Each user's server keeps its copy and a machine server orders the writes and pushes changes (the first version of
this note). Works, keeps `registry.c` synchronous, but keeps a copy per session, a replication protocol, and trust
in the user's server for every write. Superseded by D.

### D. Session 0 hosts the registry; programs connect to it directly (recommended)

Session 0 is an always-running machine server that holds the only copy of the registry. Each program has its own
connection to it for registry calls. In Wine every registry call is already one round trip from the program to a
server, so this costs the same as today; only the destination changes. There is no local copy, nothing to
replicate or merge, one writer of the files, and a change is visible to everyone at once. This is also the shape
Windows has, with session 0 in the place of the kernel.

## Design (option D)

### Identity and start

- A dedicated Unix account for the machine server (the `wexsys` group exists; the account is new), not root. It
  owns the machine hives and later the service data. Saves become atomic.
- Started at boot by a systemd unit, before the display manager, restarted on failure. Its prefix is the machine
  root. It listens on a socket in `/run/wineexplorer-session0/` (its own directory: `/run/wineexplorer` is root's).
- Elevated programs keep today's model for files: the user's uid with the system group, in the user's session.

### The registry connection

- A registry-only client: a connection that can send the registry requests (about 20) and nothing else. It is not
  a Wine process in session 0: no threads, handles to other objects, windows or signals.
- **Attested set-up.** When a process starts, its user's server asks session 0 for a registry channel for it and
  states the process's token (user, groups, integrity, restrictions). Session 0 believes this only from a peer
  that passes the launcher test (`ntdll/unix/sysrun.c` 163-193: the user's uids, saved gid the system group, not
  dumpable). The channel's descriptor is handed to the process. A process that connects by itself, or through a
  server the user started by hand, is a standard user by its Unix identity: it reads what Users may read and
  writes nothing in the machine trees. A token that changes (impersonation) gets a new attestation.
- **Handles.** A key handle from session 0 is not an entry in the user's server's handle table. `ntdll` keeps
  the mapping and handles the cases that cross servers: duplication, inheritance by a child process, and
  `NtNotifyChangeKey` with an event that lives in the user's server (a helper thread sets it).
- **Reconnect.** If session 0 restarts, `ntdll` reconnects and reopens keys by path; until then registry calls
  fail and programs cannot start. Session 0 has no user code and no windows; systemd restarts it.

### Registry content

- **First: the machine trees** (`HKLM`, `HKU\.DEFAULT`). `ntdll` routes by tree; `HKCU` stays in the user's
  server for now.
- **Then: the user hives**, loaded into session 0 at logon. The user's server opens its hive file and hands the
  descriptor over, so session 0 needs no access to home directories. With that the routing by tree goes away,
  and E1's separate profile-hive load becomes an ordinary load in session 0.
- **Per-session keys** (display, devices, volatile session state; `is_per_session_key`, `registry.c` 383-472) are
  kept per session id inside session 0, as a small overlay, not shared.
- **Tier-only trees:** `HKLM\SAM` and `HKLM\SECURITY`, readable by SYSTEM only. The account lockout policy and
  later the accounts move there; `NetUserModals`, `net accounts`, Group Policy (`GROUP_POLICY.md`) and the
  password verifier use them. The verifier asks session 0 instead of parsing a saved file.
- **Limited writes:** refused with access denied, except the virtual store case above. Keys that Wine or the
  shell itself must write from a limited session are per-session keys or get an explicit grant, listed in T0.
- **Persistence:** key permissions stored; an append-only change journal with a checkpoint every few seconds,
  on Wine's text format first. The binary hive library is evaluated later together with the shared mapping.
- **The user's server** reads a few machine keys itself; it gets them over its own channel at start.

### Services and the broker

- `services.exe` is split into a front (the control endpoint programs talk to, in the user's session) and a back
  (service database, starting service processes). First in place, then the back moves to session 0. The front
  forwards and attests the caller.
- Stay in the session: `rpcss`, `winedevice`, `plugplay`, `mountmgr`, anything with a window. Move: services
  that are only started, stopped and queried first; then, with a pipe proxy in the front, the installer and
  updater services. A service that passes handles, shares memory or shows windows stays.
- The elevation broker's decision and policy move to session 0. The consent dialog and the launch stay in the
  user's session as a thin piece acting on an order from session 0. This reworks the lab protocol (R8), which
  assumes the broker in the user's server.
- Session 0 gives each logon a session id.

## Phases

| Phase | Content | Estimate |
|---|---|---|
| T0 | Spike and measurements: registry calls per program start and the share for machine trees; registry reads made by the server itself; how often key handles are duplicated or inherited and keys watched; the limited-write list from a traced session; a prototype of open, query and close against a second server | about a week |
| T1 | Session 0 exists: account, unit, socket, registry-only client, attested channel; it owns the machine hives; `ntdll` routes the machine trees to it; user servers no longer load or save them. A switch keeps the old mode during development | 3 to 4 weeks |
| T2 | Stored key permissions, journal and checkpoint, refused writes, virtual store | 2 weeks |
| T3 | Tier-only trees; account policy, `net accounts`, the verifier and Group Policy on them | 1 to 2 weeks |
| T4 | User hives hosted in session 0 at logon; routing by tree removed | 2 weeks |
| T5 | `services.exe` front and back; the back and the first services in session 0; pipe proxy; installer and updater services | weeks, own design |
| T6 | Broker decision in session 0 with the session-side piece; session ids per logon | weeks, own design |
| Later | Read-only shared mapping for world-readable trees; binary hives with the ReactOS library | to be measured |

The services split in place (first half of T5) does not depend on T1 and can run alongside.

## Tests (each phase adds its own)

- Two users logged on at once. An elevated installer in Alice's session registers a COM class and an uninstall
  entry; a program in Bob's session waiting in `RegNotifyChangeKeyValue` is woken and reads them at once. A
  32-bit and a 64-bit reader.
- A limited program's write to `HKLM\System` fails with access denied. A 32-bit interactive program's write to
  `HKLM\Software\Vendor` succeeds, is visible to that user only, persists, and is not in the machine hive.
- A sandboxed (restricted, low-integrity) process is refused what its token forbids, with the same Unix uid as
  its parent.
- A server started by hand, and a process that connects by itself, read `HKLM` and cannot write it.
- Session 0 is killed during an installer: no torn hive, the journal is replayed, programs reconnect.
- `net accounts /lockoutthreshold:3` in an elevated prompt takes effect for the verifier at once.
- The existing suites pass with the switch off, and with listed changes with it on.

## Stock Wine changes expected

`dlls/ntdll/unix` (registry calls routed to the registry channel, handle mapping, reconnect), `server/registry.c`
(registry-only client, check at open by attested token, per-session overlay, permissions, journal),
`server/request.c` and a new server file for the session 0 socket, `server/sysrun.c` (peer test), `programs/services`
and `programs/wineboot` (T5), `dlls/netapi32` (T3). Each listed in `STOCK_CHANGES.md` when made. None is a
candidate for upstream.

## Risks

- Session 0 is a hard dependency: without it programs cannot read the registry. Mitigation: no user code in it,
  restart by systemd, reconnect in `ntdll`; later the shared mapping keeps reads alive.
- One single-threaded server answers every user's registry calls. T0 measures the load; the shared mapping is
  the relief if needed.
- Handles from two servers in one process: the cases are few (duplicate, inherit, notify) but each must be
  right; T0 counts how often they occur.
- The registry service is an attack surface for every local user. Keep it small (see the audit lesson above).
- Reworking R8 for a broker in session 0 touches finished, tested lab work; it comes last.

## Decisions needed

1. **Shape:** option D, session 0 hosts the registry and programs connect directly.
2. **Identity:** a dedicated `wexsys` account for session 0 (recommended) or root.
3. **Limited machine writes:** refused, with the virtual store for 32-bit interactive programs (recommended); or
   plain refusal as on XP; or kept in memory as today.
4. **User hives in session 0** as the target (recommended, as on Windows), machine trees first.
5. **Token attestation** by the user's launcher-started server (recommended), or Unix identity only.
6. **Order:** T0 now; then T1 with the services split alongside.

## Sources

- Registry virtualization: https://learn.microsoft.com/en-us/previous-versions/dotnet/articles/bb530198(v=msdn.10)
  and https://learn.microsoft.com/en-us/archive/blogs/patricka/tales-of-application-compatibility-weirdness-demystifying-uac-virtualization
- Inside the Registry: https://learn.microsoft.com/zh-cn/previous-versions//cc750583(v=technet.10)
- The Windows Registry Adventure (hives, the hive format, the kernel side, layered keys):
  https://projectzero.google/2024/04/the-windows-registry-adventure-1.html ,
  https://projectzero.google/2024/10/the-windows-registry-adventure-4-hives.html ,
  https://projectzero.google/2024/12/the-windows-registry-adventure-5-regf.html
- Registry files of a user profile: https://winreg-kb.readthedocs.io/en/latest/sources/windows-registry/Files.html
- dconf: https://man.archlinux.org/man/dconf.7 and https://github.com/gnome/dconf
- Android system properties: https://android.googlesource.com/platform/bionic/+/189394b/libc/bionic/system_properties.cpp
- ReactOS hive library: `references/reactos/sdk/lib/cmlib` (local reference tree)

Not verified: whether a manifest with a requested execution level switches virtualization off (commonly stated,
not found in the sources read); the lazy flush interval on current Windows versions; the details of layered keys
beyond the summary above.
