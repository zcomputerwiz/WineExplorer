<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Event logs (design note)

Status: design draft 2, 2026-11-24, by session 0 (the orchestrator). Draft 1 was reviewed by the Session 0 reviewer
(static): **go on L1 with six conditions (E1-E6)**, all written in below and listed at the end. It is the back end
that Computer Management's Event Viewer needs (GPO's Computer Management plan, step 4), and what every program that
calls `ReportEvent` has been writing into the void.

## What exists

- Wine's classic event log API in `dlls/advapi32/eventlog.c` is stubs: `OpenEventLog`, `ReadEventLog`,
  `GetNumberOfEventLogRecords`, `ClearEventLog`, `BackupEventLog`, `NotifyChangeEventLog` return success or fixed
  values and store nothing; `ReportEvent` writes to the debug channel only. `wevtapi` (the Vista+ API) is stubs too.
- An `EventLog` service exists (`loader/wine.inf.in`: `svchost.exe -k LocalServiceNetworkRestricted`, auto start) and
  runs, so Safe Mode's checks see it running (`installer/tests/safe-mode.sh`, `safeboot-probe.c`), but it keeps
  nothing.
- With the system tier, session 0 runs the machine's services and holds the machine registry; programs in a session
  reach session 0's named pipes through the pipe bridge (T5.2), which tells session 0 the caller's token and real
  process id (R6 step 2).

## What Windows does (documented behaviour, black box)

- Three classic logs per machine: Application, System, Security, plus any custom log registered under
  `HKLM\System\CurrentControlSet\Services\EventLog\<log>`. Event sources are registered under
  `...\EventLog\<log>\<source>` (`EventMessageFile`, `CategoryMessageFile`, `TypesSupported`).
- The logs are machine-wide, kept by the Event Log service. Every session's programs write into the same logs.
- `ReportEvent` sends the event type, category, id, the user SID (optional), insertion strings and binary data; the
  service stores the record with the computer name, time generated and time written. Message text is not stored: a
  reader formats it from the source's message file (`FormatMessage` with the insertion strings).
- Access (the defaults, documented): any user may write Application; System is written by the system and services;
  reading Application and System is open to users; the Security log is read and cleared only with
  `SeSecurityPrivilege` (administrators), and written only by the system (audit).
- Each log has a maximum size and a retention policy (overwrite as needed, overwrite older than N days, do not
  overwrite), in the log's registry key (`MaxSize`, `Retention`). Clearing can save a backup file first.
- `NotifyChangeEventLog` sets an event when a record is written.
- The record layout returned by `ReadEventLog` is the documented `EVENTLOGRECORD` structure, sequential or seek
  reading, forwards or backwards.
- A remote server name in `OpenEventLog` or `RegisterEventSource` reaches another machine's log over RPC; we answer
  `RPC_S_SERVER_UNAVAILABLE` (1722).

## The design

### 1. One service in session 0, the only writer

The `EventLog` service in session 0 owns the logs. It is the only process that opens the log files. Each session's
own `EventLog` service entry is not started (session 0's services are forwarded to sessions by name, as for other
machine services), so there is one store per machine, as on Windows. Source registration and each log's `MaxSize`,
`Retention` and `CustomSD` are read by the service from session 0's registry, never taken from a client. No named
objects are created anywhere (not even for "service ready"), so nothing here is touched by the cross-server design's
global namespace (X1).

### 2. The client side: advapi32 over a bridged pipe

`advapi32`'s classic API (and later the basic `wevtapi` calls Event Viewer needs) becomes a client of the service
over a named pipe, `\pipe\WexSession0\eventlog` (bridged from every session to session 0). The protocol is our own,
small and versioned: Windows programs reach the log only through advapi32, so no Microsoft protocol is needed.
Operations: open log or source, report, read (sequential or seek, forwards or backwards, into an `EVENTLOGRECORD`
buffer the client returns as is), count, oldest record, information (full), clear, stream records (for backups,
below), notify-change subscription.

- **Identity comes from the pipe and the bridge, never from the message (E6).** The service checks the caller's token
  at identification level, made by the bridge with `link_bound_token()` (the cross-server design's X0 rule: account
  and session are the link's, never a description's), and refuses anonymous tokens. The real process id is used for
  tracing only, never for decisions. The record's user SID is the one the caller passed (Windows stores what the
  caller gives), checked to be a well-formed SID of bounded size.
- **No impersonation by a fake server (E2).** advapi32 opens the pipe with `SECURITY_SQOS_PRESENT |
  SECURITY_IDENTIFICATION`: a squatter could at most identify an elevated Event Viewer, never act as it. The service
  needs only identification. To confirm in L1, with a test: a program in a session cannot create a pipe under
  `\pipe\WexSession0\`, because the bridge reserves that prefix (otherwise a squatter in a session would catch every
  client there).
- **Connected lazily, and again after a restart.** `RegisterEventSource` returns a handle even while the service is
  down. `ReportEvent` while the service cannot be reached returns success and writes the debug channel, as today, and
  the next call tries to connect again, so a program started early still logs once the service is up, also after a
  session 0 restart. Readers get `RPC_S_SERVER_UNAVAILABLE` (1722), as on Windows.
- **Notifications without cross-server objects.** `NotifyChangeEventLog` keeps one notification request outstanding
  on the pipe per subscription; a client-side threadpool wait on that request sets the caller's own event locally.
  No object crosses servers, and nothing waits for X1.
- **Without session 0** (plain prefixes, the setup account, development mode) the same client talks to the prefix's
  own `EventLog` service over the same pipe name, unbridged; behaviour is the same, the store is the prefix's.

### 3. Backups are made by the client (E3)

The service never opens a path a client sends: session 0's account has Unix rights a session's administrator does not,
and its `C:` is not the client's. So:

- `BackupEventLog`, and `ClearEventLog` with a backup file: the client asks the service to stream the log's records
  (with the same read check as `ReadEventLog`), and writes the file itself, with its own rights and in its own
  namespace; for a clear, the service clears only after the stream completed and the client confirms.
- `OpenBackupEventLog` reads and parses the file in the client; the service is not involved.
- Backup files use our own format (the storage format below); backups made on Windows (`.evt`) are not readable yet.

### 4. Storage (E5)

- One file per log under the machine root's system directory (`C:\windows\system32\config\` in the machine prefix,
  `AppEvent.evt`, `SysEvent.evt`, `SecEvent.evt` and `<log>.evt` for custom logs), our own format: a fixed header
  (version, size limit, wrap state, the oldest and next record numbers) and records, each an `EVENTLOGRECORD` followed
  by a short trailer: a CRC32 of the record and its record number. Reads copy the record part only.
- Written by appending, with an fsync at most once a second, and as a ring when the policy is "overwrite as needed".
- **Load:** scan from the oldest record and accept the run of records whose CRCs are valid and whose record numbers
  rise by one; the header's end offset is not trusted beyond that run. A torn record in the middle, and stale records
  with valid lengths after the end, are cut.
- `MaxSize` (default 512 KiB, as XP) and `Retention` from the log's key are honoured; a full log with "do not
  overwrite" refuses writes (`ERROR_LOG_FILE_FULL`) and logs one System event about it, as Windows does.
- Files are owned by session 0's account, mode 0600: no session reads them directly.

### 5. Access rules

| Log | Write | Read | Clear / backup |
|---|---|---|---|
| Application, custom logs | any user | any user | administrators (elevated) |
| System | session 0's own processes; L2 or later: a session's process whose server reports it runs with the system group (E1) | any user | administrators |
| Security | nobody until auditing exists (created now, empty) | `SeSecurityPrivilege` | `SeSecurityPrivilege` |

- **E1: System is decided on the calling process, not its token.** A session's system role is SYSTEM inside the
  user's own server, under the user's uid; tokens can be duplicated or impersonated within a session (and Wine
  enforces process descriptors weakly), so "SYSTEM SID in the token" is the wrong test. The right fact is that the
  process runs with the system group, which the user's server knows from `SO_PEERCRED` at connect (the bit
  `server/sysrun.c` already uses for machine-root paths). The bridge forwards that flag next to the real pid, and one
  shared predicate ("is the system role", used by every session 0 service that needs it) decides. **In L1 the bridge
  does not carry the flag yet:** a session's system role counts as that session's user (Application only), so System
  is written only by session 0's processes, which is where Windows' services run anyway.
- Custom logs take their `CustomSD` when present (read by the service from session 0's registry), else the
  Application rules.
- **The Security log is created now, empty** (the reviewer's answer to draft 1's question 3): Event Viewer's tree
  then matches XP's, reads need `SeSecurityPrivilege`, and every write is refused until auditing exists.

### 6. Protocol limits (E4)

Stated here and tested in L1:

- a maximum frame of 64 KiB; one request in flight per connection;
- UTF-16 strings: even length, terminated, at most 32767 characters each;
- insertion strings at most 256, binary data and the whole record capped at Windows' record maximum (0x3FFFF bytes
  for a record);
- the read buffer the client asks for capped at 0x7FFFF bytes, and the server never allocates more than that whatever
  the request says;
- seek record numbers checked against the log's oldest and next numbers;
- per account: at most 256 connections, 1024 open log handles and 256 notify subscriptions;
- an unknown operation or version is refused, and a malformed frame closes the connection.

### 7. Sources and message files

Source registration stays in the registry (session 0 holds it). The service does not format messages: Event Viewer
formats them in the reader, from the source's message file, as on Windows. A missing message file shows Windows'
"The description for Event ID ... cannot be found" text, built by the viewer.

### 8. What Wine itself logs

Nothing new at first. Candidates later, each a small change: service start failures (the service manager, System
log), Safe Mode boot notes, the elevation broker's consent results (Security, once auditing exists).

### 9. What Event Viewer needs beyond the classic API (GPO, 2026-11-24)

- **Log properties** (file size, created, last written, last read, record count, oldest and next record numbers, and
  the `MaxSize` and `Retention` in force): the "information" operation returns all of them, the times from the store's
  header. advapi32 exposes them through `GetEventLogInformation` with a Wine-private information level (a value
  outside Windows' documented ones, defined in a Wine header next to the protocol), so no new export is needed.
  `EVENTLOG_FULL_INFORMATION` stays as documented.
- **Changing `MaxSize` and `Retention`**: Event Viewer writes the log's registry key (session 0's registry, as an
  elevated administrator). The service watches the `EventLog` key with a change notification and reloads a log's
  settings when it changes; no restart and no "reload" operation.
- **The log tree**: `HKLM\System\CurrentControlSet\Services\EventLog\Application`, `System` and `Security` exist in the
  machine hive by default (L1 adds whichever Wine's `wine.inf` lacks), so the viewer shows all three before any
  record exists. Reading Security without `SeSecurityPrivilege` gives "Access is denied", as on XP.
- **Virtual list reading**: `GetNumberOfEventLogRecords` and `GetOldestEventLogRecord` are exact, and backward
  sequential plus seek reads return many records per call (up to the read cap).
- **Wine's own sources** (Service Control Manager, EventLog and the others of section 8) get a small message resource
  module, so their events format instead of showing "the description cannot be found". It comes with the first Wine
  source that logs (a follow-up to L1, not part of it).

## Order of work

1. **L1: the service and storage**, with the pipe protocol, in session 0 and in plain prefixes. Tests:
   - write from a program in a session and read it from another session;
   - the access table: a standard user cannot write System or read Security; a session process holding a duplicated
     SYSTEM token cannot write System (E1);
   - a program in a session cannot create the pipe under `\pipe\WexSession0\`, and the client opens with
     identification level only (E2);
   - a backup made through the client, in the session's namespace (E3);
   - malformed and oversized frames, and each per-account cap (E4);
   - a torn middle record and stale tail records cut on load (E5);
   - size limit and each retention policy;
   - session 0 restarted while a client holds a handle and a subscription: the client reconnects.
2. **L2: advapi32's classic API** as the client (all calls listed above, A and W), with Wine's own `advapi32` tests
   plus new ones on both architectures; and the system-group flag on the bridge (E1's full form), if L1 shows it is
   needed.
3. **L3: Event Viewer** (GPO's snap-in) on the classic API, and the `wevtapi` basics it needs, if any.

## Review record

Draft 1 (2026-11-24), the Session 0 reviewer: go on L1 with six conditions.

| Condition | Draft 2 |
|---|---|
| E1: System decided on the calling process (system group from the server), one shared predicate; until then a session's system role counts as the user | Access rules |
| E2: identification-level client, anonymous refused, the `WexSession0` prefix reserved against squatters (tested) | Client side |
| E3: the service never opens a client's path; backups streamed to the client; `OpenBackupEventLog` in the client | Backups |
| E4: protocol limits stated and tested; the stored SID checked; the pid for tracing only | Protocol limits, client side |
| E5: records with a CRC32 and record-number trailer; load accepts only a valid rising run | Storage |
| E6: the link-bound token, session from the link | Client side |
| Answers: own protocol yes; ReportEvent succeeds while the service is down, lazy reconnect; Security created now, empty | Client side, access rules |
| Notifications over the pipe, no X1 dependency; no named objects; registry read in the service | Service, client side |

## L1 as built (branch claude/eventlog-l1)

Written by the L1 session (2026-11-24) and sent to the reviewer before most of the code; the answers to its
questions are recorded here when they come.

- **Where the service lives:** the existing svchost DLL. `loader/wine.inf.in` already runs EventLog as
  `svchost.exe -k LocalServiceNetworkRestricted` with ServiceDll `wevtsvc.dll`, a stub until now; the service is that
  DLL (`dlls/wevtsvc/wevtsvc.c`, `store.c`). No new service entry, no new program. `programs/services/services.c`
  takes EventLog off the per-session list, so a session forwards it to session 0 and one store serves the machine.
- **Pipes:** the service listens on `\.\pipe\WexSession0\eventlog`, which its own server's processes reach (session
  0's in session 0, every process in a plain prefix). In session 0 it also listens on `\.\pipe\eventlog`, which a
  session's open of `\.\pipe\WexSession0\eventlog` reaches through the bridge (`eventlog` is on
  `pipe_bridge_allowed`). The caller's class comes from the pipe it came in on, cross-checked with
  `GetNamedPipeClientSessionId`: a local connection in session 0 is a session 0 process; a bridged one is a session's.
  A session cannot reach session 0's local name: the bridge strips one prefix and allows exactly `eventlog`. Creating
  a pipe under `WexSession0\` in a session is refused by the bridge (`create_named_pipe`, E2), tested.
- **Identity (E2, E6):** the bridge's `PIPE` request turns the description into a token with `link_bound_token()`
  (account and session of the link). The service impersonates only to read the token (user, groups, elevation,
  `SeSecurityPrivilege`) and reverts at once; it never acts as the caller; anonymous callers are refused. Wine drops
  the client's SQOS today (the server never sees the requested level), so a server end gets impersonation level
  whatever the client asks; the client asks for identification level as documented, and carrying the level into the
  server is a step of its own (agreed in review; a general KNOWN_BUGS item, since every pipe server is affected).
- **Protocol (`include/wine/wex_eventlog.h`):** a message-mode pipe, one request and one reply message, one request in
  flight per connection, a 32-byte versioned header. A record may be 0x3FFFF bytes and a read 0x7FFFF, which 64 KiB
  frames cannot carry, so a message is at most 0x7FFFF + 1 + 64 bytes (agreed in review); the service
  allocates per the operation's limit, never what a request says. Unknown versions get `ERROR_REVISION_MISMATCH`,
  unknown operations `ERROR_INVALID_FUNCTION`; a malformed message (bad magic, sizes that do not match, over the
  limit) closes the connection.
- **Serving (N3, Q2):** one thread and an I/O completion port: overlapped connects, reads and writes, so a waiting
  subscription holds no thread. Each message's first 4 KiB are read; a larger one is read whole after a peek, into a
  buffer of exactly its size. The service holds at most 16 MiB of requests and replies at once and stops reading new
  requests while over it. A read reply is sized to the records that fit. A `NOTIFY` keeps a read posted, so the
  client's disconnect ends it (N4); a request while it waits closes the connection. Both pipes are made with
  `FILE_FLAG_FIRST_PIPE_INSTANCE` (N2), and a session 0 process that connects to the bridged name is classed as a
  session's caller (a harmless downgrade).
- **Clear after a backup (N6):** `CLEAR` names the last record the client's backup has; newer records give
  `ERROR_RETRY`, and the client streams the rest and clears again. After 8 retries against a steady writer it clears
  through the backup's last record only (mode 2): the newer records stay, so nothing is lost and the clear ends.
- **Section 9:** `INFO` returns `wex_evtlog_information` (full, count, oldest, next, `MaxSize` and `Retention` in force,
  file size, and the created, last written and last read times kept in the file's 48-byte header);
  `WEX_EVTLOG_INFORMATION_LEVEL` is the Wine-private level L2's `GetEventLogInformation` will answer. The service
  watches the EventLog key (`RegNotifyChangeKeyValue`) and reloads every log's settings when it changes. `wine.inf`
  adds the `Security` key (Application and System were there).
- **N1:** `eventlog` is a bridged name that needs the opener's server to have a session link (as the broker's), so a
  caller without a link is refused at the bridge, never described.
- **Caps:** per account (the token's user SID): 256 connections, 1024 handles (64 a connection), 256 subscriptions.
  A subscription is a `NOTIFY` in flight on a connection of its own, so the subscription cap is reached together with
  the connection cap; the test fills both from session 0 (the bridge's own per-user cap is also 256).
- **Access (decided in review, N7):** every log's rights are read (1), write (2) and clear (4), decided by
  `AccessCheck` against the log's `CustomSD` (SDDL, from session 0's registry), or else Windows' built-in descriptor,
  measured on Windows 10 with `wevtutil gl` (configuration values, read-only on the lab VM):
  - Application (and custom logs without a `CustomSD`): `O:BAG:SYD:(A;;0x2;;;S-1-15-2-1)(A;;0xf0007;;;SY)(A;;0x7;;;BA)(A;;0x7;;;SO)(A;;0x3;;;IU)(A;;0x3;;;SU)(A;;0x3;;;S-1-5-3)(A;;0x3;;;S-1-5-33)(A;;0x1;;;S-1-5-32-573)`
    (Windows also lists one app capability SID with write, left out);
  - System: `O:BAG:SYD:(A;;0xf0007;;;SY)(A;;0x7;;;BA)(A;;0x3;;;BO)(A;;0x5;;;SO)(A;;0x1;;;IU)(A;;0x3;;;SU)(A;;0x1;;;S-1-5-3)(A;;0x2;;;S-1-5-33)(A;;0x1;;;S-1-5-32-573)`;
  - Security: read and cleared with `SeSecurityPrivilege` enabled, never written (its descriptor needs an elevated
    query on Windows and was not measured).

  So, as on Windows: interactive users write and read Application and read System; SYSTEM, the service accounts and
  elevated administrators write System; elevated administrators clear. The same table holds in plain prefixes and in
  session 0, with **one exception for a session's caller over the bridge (E1, L1's form):** its SERVICE group
  (S-1-5-6) and per-service SIDs (S-1-5-80-*) are made deny-only before the check (`CreateRestrictedToken`), and a
  token whose user is SYSTEM, LocalService or NetworkService is masked after the check to a user's rights (0x3 on
  Application and custom logs, 0x1 on System, nothing on Security), whatever groups it holds (Wine's SYSTEM token
  carries BUILTIN\Administrators, which would otherwise write and clear System). An elevated administrator of a session
  keeps BA (T1 believes it); session 0's own processes are checked as they are. Streaming for a backup needs read; a
  clear needs clear. Wine gives the SERVICE group only to service-account tokens, so the session test of the
  SERVICE rule is the SYSTEM token's; a token with SERVICE alone cannot be made from a session without forging one.
- **Storage (E5):** `C:\windows\system32\config\{App,Sys,Sec}Event.evt` and `<log>.evt` in session 0's prefix, as
  designed; files are written again in place (never replaced by a rename, so they keep their owner and mode), and
  created with a DACL for SYSTEM only, which Wine maps to the owner's mode bits. The test checks the owner and that no
  user reads the files.
- **Tests:** `scripts/eventlog-probe.c` (64 and 32 bit) speaks the protocol directly, so it can also send what a
  client never would; `scripts/eventlog-corrupt.py` damages a stopped log for the load rule; section `s0_eventlog` of
  the normal suite has one check per item of L1's test list, and `own_prefix` checks the plain prefix's own log.
