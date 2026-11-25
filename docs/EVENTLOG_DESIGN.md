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
