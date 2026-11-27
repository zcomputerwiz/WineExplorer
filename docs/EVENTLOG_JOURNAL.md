<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Event Log and the systemd journal (design note)

Status: design draft 2, 2026-11-27. Draft 1 was reviewed by the Session 0 reviewer: the architecture is right, J1 may
start once this draft has R1-R6 (listed at the end with where each is answered). It builds on
[EVENTLOG_DESIGN.md](EVENTLOG_DESIGN.md) (the EventLog service in session 0, `wevtsvc.dll`, its pipe protocol, store
and access rules: "L1 as built").

**The owner's words (2026-11-27):** Linux system logs in Event Viewer are worth a lot; streaming debug output must not
go through the Event Log.

## Scope

1. **Journal to Event Log (the main part).** Selected journal entries become Windows events: a systemd unit that fails
   shows as a Service Control Manager event in System, logons by ssh and on a text console in Security, disk errors
   in System, kernel text in an administrators-only log. An explicit table maps trusted journal fields to log,
   source, event id and type. Rate-limited and filtered, never a copy of the journal.
2. **Event Log to journal (cheap).** Every event the service stores is also written to the journal as a structured
   entry, so `journalctl` sees what Windows programs report.
3. **Never:** `WINEDEBUG` traces in the Event Log. They stay in `/var/log/wineexplorer` (the session 0 logs) and the
   sessions' own log files.

## Clean room

- Event ids, source names and log names are the ones XP shows, taken from Microsoft's public documentation of those
  events or from our own black-box observation in the lab VM. No message text is copied: our message resource module
  (EVENTLOG_DESIGN section 9, "Wine's own sources") has our own wording with the same insertion strings in the same
  order, so a program or script that reads `EVENTLOGRECORD`s sees the documented ids and strings.
- **Insertion strings (R5):** Service Control Manager 7000 has two (the service's display name, and the error's text,
  not a bare token) and 7034 two (the service, the number of times it failed), both per Microsoft's published event
  pages; 7031 has four (the service, the count, the delay in milliseconds, the corrective action), from a third-party
  description only, so J1 confirms it from the lab VM's provider metadata (`wevtutil gp` on the Service Control
  Manager provider, a read; its ids are unchanged since XP) before the row is coded. **`wevtutil gp /gm` prints
  Microsoft's message templates: only the number and order of the `%n` insertions are recorded, nothing of the
  wording, in notes, commits or the repository.**
- Security 528, 529 and 538 are not in Windows 10's metadata (its auditing provider uses 4624, 4625 and 4634), so J2
  takes their insertion strings from Microsoft's archived, public Windows Server 2003 and XP security event
  documentation. No Windows 10 Security id is relied on without asking the reviewer first.
- systemd's message ids (`MESSAGE_ID`) come from systemd's own catalog (LGPL-2.1, the installed `journalctl
  --list-catalog`), checked against Debian 13's before code.

## Who reads the journal: a native bridge, not session 0

Reading the journal needs the `systemd-journal` (or `adm`) group, which reads every unit's and user's messages. Session
0 is deliberately not root-equivalent (owner, 2026-11-24) and runs Windows code as SYSTEM; giving its account the whole
journal would put every user's journal behind any flaw in that code. So:

- **`wex-journal-bridge`**, a small native C program (GPL-2.0-or-later, `src/wex-journal-bridge/`), runs as its own
  system account `wexjournal` (member of `systemd-journal` only), from `wex-journal-bridge.service`: `NoNewPrivileges`,
  `ProtectSystem=strict`, `ProtectHome=yes`, `PrivateNetwork=yes`, `RestrictAddressFamilies=AF_UNIX`, a
  `RuntimeDirectory` for its socket, no capabilities. It opens the journal with `SD_JOURNAL_SYSTEM`: users' own
  journals are never read. (Debian's default `SplitMode=uid` also keeps users' entries out of the system journal, but
  that is configuration; what holds is the trusted-field rule below.)
- It reads with `sd-journal`, applies the mapping table and the rate limits, and serves **only mapped events** on a
  Unix socket, `/run/wineexplorer-journal/events` (owner `wexjournal`, group `wexsys`, mode 0660). **Each connection is
  checked by uid with `SO_PEERCRED`: session 0's account only.** The group alone would not do: elevated processes run
  with the system group (`wexsys`) too.
- The EventLog service in session 0 (`wevtsvc.dll`) connects with Winsock's `AF_UNIX` and stores what it receives as
  records of the mapped log and source. The bridge never writes the Event Log's files, and session 0 never reads the
  journal.
- **Why a socket and not D-Bus through wexhost** (the project's preference elsewhere): session 0 stays off the system
  bus, the peer check is the kernel's (`SO_PEERCRED`) with no bus policy to get right, and Winsock's `AF_UNIX` needs no
  Unix library in `wevtsvc`. The socket server shares `wexprefetchd`'s peer-check and framing pattern (a small common
  header in `src/`, not a second hand-rolled one).
- Without the bridge (not installed, plain prefixes, development mode) nothing changes.

Alternative considered: `journalctl --follow -o json` started by the service. Rejected: it would need the journal
group on session 0's account (above), and the project prefers a service over spawning tools.

## Trusted fields only (R1)

Any local user can write a journal entry with any `SYSLOG_IDENTIFIER`, `MESSAGE`, `MESSAGE_ID`, `UNIT` or `PRIORITY`
(`logger -t sshd ...`, `sd_journal_send`). Only the fields journald itself sets, the underscore fields (`_PID`, `_UID`,
`_COMM`, `_EXE`, `_SYSTEMD_UNIT`, `_TRANSPORT`, `_BOOT_ID`), say who wrote an entry. So **every rule first requires
trusted fields**, and the other fields are only read once those match:

| Rows | Required trusted fields |
|---|---|
| 1-3 (service manager) | `_PID=1`, `_UID=0` |
| 4-7 (kernel) | `_TRANSPORT=kernel` |
| 10 (PAM failures) | `_UID=0`, and `_SYSTEMD_UNIT` (e.g. `ssh.service`) or `_EXE` (`/usr/sbin/sshd`, `/usr/bin/login`) of the PAM service the row names |
| 8-9 (logind) | `_SYSTEMD_UNIT=systemd-logind.service`, `_UID=0` |
| 11 (coredumps) | `_SYSTEMD_UNIT` of `systemd-coredump@*.service`, its `_UID` |
| 12 (listed units) | `_SYSTEMD_UNIT` equal to a unit of the config's list; never `UNIT` or `SYSLOG_IDENTIFIER` |

Row 12's **source name comes from the config line** (`unit=NetworkManager.service source=NetworkManager`), never from
a field of the entry, so no entry can name a new source key under `HKLM\...\EventLog`.

## The mapping table

An explicit table, compiled in with defaults and extended by a root-owned file, `/etc/wineexplorer/journal-events.conf`
(the bridge refuses a file that is not root's or that group or others can write). Rules are tried in order; the first
match wins; an entry that matches no rule is dropped.

| # | Journal entry (after its trusted fields) | Log | Source | Event id | Type | Insertion strings |
|---|---|---|---|---|---|---|
| 1 | A start job of a system unit failed | System | Service Control Manager | 7000 | Error | the unit's description, the failure's text (from the unit's result: "the process exited with code 3", "the start timed out", ...) |
| 2 | A running system unit failed, no restart scheduled | System | Service Control Manager | 7034 | Error | the unit's description, its failures this boot |
| 3 | A running system unit failed and a restart is scheduled | System | Service Control Manager | 7031 | Error | the description, its failures this boot, the restart delay in ms, "Restart the service" |
| 4 | Kernel, block layer I/O error (`_KERNEL_SUBSYSTEM` `block` or `scsi`, `PRIORITY` <= 3) | System | Disk | 11 | Error | the Linux device name |
| 5 | Kernel, file system error (ext4/btrfs/xfs, `PRIORITY` <= 3) | Linux | Linux Kernel | 2 | Error | the message |
| 6 | Kernel, out of memory kill | Linux | Linux Kernel | 3 | Error | the message |
| 7 | Kernel, any other entry with `PRIORITY` <= 2 (critical) | Linux | Linux Kernel | 1 | Error | the message |
| 8 | logind, new session of a login the session manager does not report (ssh, text console) | Security | Security | 528 | Audit Success | user, domain, logon id, logon type (3 for ssh, 2 for a console) |
| 9 | logind, such a session removed | Security | Security | 538 | Audit Success | user, domain, logon id, logon type |
| 10 | PAM authentication failure of `sshd` or `login` | Security | Security | 529 | Audit Failure | user, domain, logon type |
| 11 | systemd-coredump, a program dumped core | Application if `COREDUMP_UID` < 1000, else Linux | Linux Kernel | 4 | Error | the program, the signal |
| 12 | A listed unit's own entry, `PRIORITY` <= 3 | Linux | the config line's source | 1 | Error | the message |

- **Telling 7000, 7031 and 7034 apart (R5)** from PID 1's entries: PID 1 logs a job's end with its type and result
  (`JOB_TYPE=start`, `JOB_RESULT=failed`, `UNIT`): a failed start job is row 1. A unit that was active and fails gets
  PID 1's "unit failed" entry with no failed start job before it; the bridge then waits for that unit's next PID 1
  entry, at most 2 s: a "restart scheduled" entry (with the restart counter) makes it row 3, anything else or the end
  of the wait row 2. So 7031 and 7034 exclude each other. The restart delay is the unit's `RestartUSec`, read once per
  unit over systemd's D-Bus interface (a property any account may read; the bridge's one bus use, read-only). The
  failure count is the bridge's own, per unit and boot.
- **The "Linux" log (R2)** is a custom log, `HKLM\System\CurrentControlSet\Services\EventLog\Linux`, made by `wine.inf`
  with a `CustomSD` of `O:SYG:SYD:(A;;0xf0007;;;SY)(A;;0x5;;;BA)`: SYSTEM full, elevated administrators read and clear,
  nobody else. Free-text kernel and unit messages go there: Debian restricts the kernel log (`kernel.dmesg_restrict=1`)
  and out-of-memory lines name other users' processes, so System (readable by every interactive user, as on Windows)
  would undo that. Rows 1-4 stay in System: coarse facts, as Windows shows them. Row 11's split keeps other users'
  crashes out of Application; a config switch (`coredumps_to_application=all`) gives Windows' behaviour.
- **Types:** `PRIORITY` 0-3 Error, 4 Warning, 5-6 Information; 7 (debug) is never mapped.
- **New sources** ("Linux Kernel", and row 12's sources from the config) are registered by the service under their
  log's key the first time they log, with our message module (event 1: "%1"). XP's sources (Service Control Manager,
  Disk, Security) are registered by `wine.inf` with the same module.
- **Service names:** Windows shows a service's display name; the unit's `Description=` (the entry's
  `UNIT_DESCRIPTION`, else the unit name) is the closest.

## Logons (R4): WineExplorer reports its own, the journal the rest

- A 528 or 538 carries the logon id of the session's token (its `AuthenticationId`), so that the 538 and any later
  event naming that logon match. Only session 0's logon path knows that LUID and the account's SID, not logind. So
  **the greeter's logons (logonui/msgina through session 0) are reported by session 0's side**: 528 at the logon, 538
  at the logoff, 529 for its own failures, including the refusal of a blank password away from the console (1327,
  the owner's rule of 2026-11-25).
- The journal rows 8-10 cover the other logins (ssh and the text console) and exclude the greeter's PAM service and
  sessions, so nothing is counted twice.
- In both, the user is the Windows account (`DOMAIN\user`, through the accounts service) and the record's user SID is
  set; a raw Unix name only when the account is not mapped. A journal-row logon id is logind's session id mapped to a
  64-bit value per boot.
- **Security events are gated by the audit policy**, as on Windows: they are stored only when the machine's "Audit
  logon events" policy (`secpol`, success and failure separately; GPO lab) enables them. XP's default is no auditing,
  so by default nothing is written to Security. The service decides, since it owns the Security log and reads the
  policy. This is the Security log's first writer (it was created empty in L1).

## Both directions are untrusted input (R6)

- The bridge parses text written by any user's processes, and its account is less privileged than session 0, so
  `wevtsvc` parses the bridge's frames as untrusted, as with the MSI host: a version and size header, a frame cap
  (64 KiB), strings counted and terminated, every count checked against the frame.
- Caps: each insertion string at most 1024 characters (cut, with "..." at the end), at most 16 strings a record, the
  record within L1's record limit.
- Control characters and ANSI escape sequences are stripped (replaced by a space) in both directions: `journalctl`
  shows escape sequences, and Windows programs write anything with `ReportEvent`.

## Rate limits and filters

- Journal to Event Log, in the bridge, per rule and source: a token bucket of 10 events a second with a burst of 50.
  Over it, entries are counted, not sent; once a minute while any were dropped, one event says so (System, source
  "Linux Kernel", id 5, Warning: the rule, the source and the count). Overall at most 100 events a second; the socket's
  back-pressure does the rest (the bridge stops reading the journal while the service does not read; the journal keeps
  the entries, and the cursor resumes).
- **Event Log to journal, in the bridge too, per source:** the same bucket, so a program calling `ReportEventW` in a
  loop cannot flood the journal (journald's own rate limit would then drop the bridge's legitimate entries); a
  once-a-minute journal entry reports the count.
- `PRIORITY` filters per rule (above); debug and info entries are only mapped where a row asks for them (logons).
- **No debug output, ever (scope 3):** entries of Wine's own processes (by the trusted `_COMM` `wine`, `wine64`,
  `wineserver`, `wine-preloader`, or `_EXE` under the Wine installation) are dropped before the table; a second net
  drops any `MESSAGE` shaped like a Wine debug line (`^[0-9a-f]{4}:(trace|warn|err|fixme):`), whatever wrote it. The
  Event Log to journal direction writes event records only.

## The cursor: at-least-once (R3)

Each event carries its journal cursor in the record's data (a small binary trailer the viewer shows as data). At start
the service reads its logs' last journal-sourced record back and sends that cursor; the bridge resumes after it. There
is no separate cursor file, so a crash between a record and its cursor cannot happen; what remains is at-least-once
across a crash of the bridge between sending and the service's store, so a reader may see one entry twice after a
crash, never a gap. First start: from the current boot. **When the stored cursor is gone** (the journal vacuumed or
rotated past it), the bridge resumes from the oldest entry and the service writes one Warning (System, "Linux
Kernel", id 6: entries between the stored cursor and the oldest kept one were lost).

## Event Log to journal

Each record the service stores is sent back to the bridge on the same socket (a separate frame type), and the bridge
writes it with `sd_journal_send`: `SYSLOG_IDENTIFIER=wex-eventlog`, `PRIORITY` from the type (Error 3, Warning 4,
Information and audit 6), `MESSAGE` "<log>/<source> <event id>: <insertion strings joined by ' | '>" (the message is not
formatted: the message module lives in Windows), and the fields `WEX_EVENTLOG_LOG`, `WEX_EVENTLOG_SOURCE`,
`WEX_EVENT_ID`, `WEX_EVENT_TYPE`, `WEX_EVENT_RECORD`, `WEX_EVENT_USER_SID`. Records that came from the journal are not
sent back (they carry their cursor). **Loop prevention by trusted fields:** the bridge drops entries with
`_UID` = the bridge's own account and `SYSLOG_IDENTIFIER=wex-eventlog`, never the identifier alone. Security records go
to the journal only with `security_to_journal=yes` (default `no`: the journal is readable by `adm`, the Security log
only with `SeSecurityPrivilege`).

## Order of work

1. **J1: the bridge, System and Linux.** The bridge, its account, unit and socket; rows 1-7 and 11-12 with their
   trusted fields; the Linux log; the rate limits both ways; the cursor in the record; Event Log to journal. Tests
   (suite and an installed-image stage): a unit that fails to start gives 7000, one that fails while running 7034, one
   with `Restart=` 7031 only, each with the unit's description; `logger -t sshd` and `systemd-cat` entries forging
   each row are not mapped; a unit failing in a loop is rate-limited and reports the count; a Wine debug line in a
   unit's output is never stored; strings over the cap are cut and escape sequences stripped; a session 0 restart
   resumes without a gap; a vacuumed cursor gives the gap Warning; the socket refuses any uid but session 0's (an
   elevated process with the system group included); a program's `ReportEvent` appears in `journalctl
   SYSLOG_IDENTIFIER=wex-eventlog` with its fields, and a flood of them is rate-limited.
2. **J2: Security.** Session 0's own logon reports (528, 538, 529 with the token's logon id and the account's SID) and
   rows 8-10 for ssh and the console, gated by the audit policy, with the GPO lab's `secpol` test: no events by
   default; once success auditing is on, 528 and 538 at a greeter logon and logoff with matching logon ids, and at an
   ssh login; once failure auditing is on, 529 at a wrong password, and at the blank-password refusal over ssh.
3. **J3: EventLog's own boot events** (6005 at the service's start, 6006 at a clean stop, 6009 with the version, 6008
   when the previous boot did not stop cleanly, found from the journal's boot list). 6008 needs persistent journal
   storage (`Storage=persistent`, or `auto` with `/var/log/journal` present: Debian 13's default); without it there is
   no previous boot to look at, and 6008 is not written.

## Review record

Draft 1 (2026-11-27), the Session 0 reviewer: architecture right; J1 after draft 2 with:

| Item | Draft 2 |
|---|---|
| R1: rules match trusted underscore fields only; row 12's source from the config; debug drop by `_COMM`/`_EXE` first | "Trusted fields only", rate limits and filters |
| R2 (Q1): free-text rows to an administrators-only "Linux" log; rows 1-4 in System; coredumps split by uid | The mapping table |
| R3: at-least-once stated; cursor in the record; the gap Warning | The cursor |
| R4 (Q3): session 0 reports its own logons with the token's logon id; the journal covers ssh and the console | Logons |
| R5: insertion strings checked against the public docs (7031 from the lab VM); 7031 and 7034 exclusive, how start and run failures are told apart | Clean room, the mapping table |
| R6: both directions untrusted; string and record caps; control characters and escapes stripped; Event Log to journal rate-limited; loop drop by `_UID` | Both directions, rate limits, Event Log to journal |
| Q2: the boundary is right; `SO_PEERCRED` by uid (the group would admit elevated processes) | Who reads the journal |
| Nits: why a socket and not D-Bus; `wexprefetchd`'s peer-check pattern shared; `security_to_journal=no` kept; 6008 needs persistent storage | Who reads the journal, Event Log to journal, order of work |
