<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Event logs (design note)

Status: design draft 1, 2026-11-24, by session 0 (the orchestrator), for the Session 0 reviewer before any code. It
is the back end that Computer Management's Event Viewer needs (GPO's Computer Management plan, step 4), and what
every program that calls `ReportEvent` has been writing into the void.

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

## The design

### 1. One service in session 0, the only writer

The `EventLog` service in session 0 owns the logs. It is the only process that opens the log files. Each session's
own `EventLog` service entry is not started (the services of session 0 are forwarded to sessions by name, as for
other machine services), so there is one store per machine, as on Windows.

### 2. The client side: advapi32 over a bridged pipe

`advapi32`'s classic API (and later the basic `wevtapi` calls Event Viewer needs) becomes a client of the service
over a named pipe, `\pipe\WexSession0\eventlog` (bridged from every session to session 0). The protocol is our own,
small and versioned (no Microsoft protocol is implemented): open log or source, report, read (sequential or seek,
forwards or backwards, into an `EVENTLOGRECORD` buffer the client returns as is), count, oldest record, information
(full), clear (with optional backup path), backup, notify-change subscription.

- **Identity comes from the pipe, never from the message.** The service takes the caller's token from the pipe
  (impersonation of the bridged client) and its real process id from the bridge (R6 step 2). The record's user SID
  is the one the caller passed (Windows stores what the caller gives), but the right to write a log is checked
  against the caller's token.
- **Without session 0** (plain prefixes, the setup account, development mode) the same client talks to the prefix's
  own `EventLog` service over the same pipe name, unbridged; behaviour is the same, the store is the prefix's.
- When the service cannot be reached, `ReportEvent` keeps today's behaviour (debug channel, success), so programs
  never fail because the log is down; readers get `RPC_S_SERVER_UNAVAILABLE` (1722), as on Windows.

### 3. Storage

- One file per log under the machine root's system directory (`C:\windows\system32\config\` in the machine
  prefix, `AppEvent.evt`, `SysEvent.evt`, `SecEvent.evt` and `<log>.evt` for custom logs), our own format: a fixed
  header (version, record numbers, size limit, wrap state) and records in the `EVENTLOGRECORD` layout, so reading is
  a copy. Written by appending, with an fsync at most once a second, and as a ring when the policy is "overwrite as
  needed".
- `MaxSize` (default 512 KiB, as XP) and `Retention` from the log's key are honoured; a full log with "do not
  overwrite" refuses writes (`ERROR_LOG_FILE_FULL`) and logs one System event about it, as Windows does.
- Files are owned by session 0's account, mode 0600: no session reads them directly.
- Crash safety: a record is written whole or not at all (length at both ends, checked on load); a torn tail is cut
  on load.

### 4. Access rules

| Log | Write | Read | Clear / backup |
|---|---|---|---|
| Application, custom logs | any user | any user | administrators (elevated) |
| System | services and the system (session 0's processes, the system role of each session) | any user | administrators |
| Security | the system only (audit events, later) | `SeSecurityPrivilege` | `SeSecurityPrivilege` |

The checks use the caller's token from the pipe. Custom logs take their `CustomSD` value when present (Windows
supports it), else the Application rules. Wine programs that write System events today (services) write through the
same pipe.

### 5. Sources and message files

Source registration stays in the registry (session 0 holds it). The service does not format messages: Event Viewer
formats them in the reader, from the source's message file, as on Windows. A missing message file shows Windows'
"The description for Event ID ... cannot be found" text, built by the viewer.

### 6. What Wine itself logs

Nothing new at first. Candidates later, each a small change: service start failures (the service manager, System
log), Safe Mode boot notes, the elevation broker's consent results (Security, once auditing exists).

## Order of work

1. **L1: the service and storage**, with the pipe protocol, in session 0 and in plain prefixes; tests: write from a
   program in a session and read it from another session; the access table (a standard user cannot write System or
   read Security); size limit and each retention policy; a torn record cut on load; a session 0 restart keeps the
   logs.
2. **L2: advapi32's classic API** as the client (all calls listed above, A and W), with Wine's own `advapi32` tests
   plus new ones on both architectures.
3. **L3: Event Viewer** (GPO's snap-in) on the classic API, and the `wevtapi` basics it needs, if any.

## Open questions for review

1. Our own pipe protocol versus implementing Windows' event log RPC interface: this note proposes our own (smaller,
   no published protocol needed, the client is ours). Anything that would make a Windows program talk to the pipe
   directly?
2. Is "ReportEvent never fails because the log is down" right for programs that check its result?
3. The Security log: worth creating now (read-only, empty) so Event Viewer shows it, or leave it out until auditing
   exists?
