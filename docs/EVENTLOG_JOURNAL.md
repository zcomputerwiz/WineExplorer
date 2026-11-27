<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Event Log and the systemd journal (design note)

Status: design draft 1, 2026-11-27, for review before any code. It builds on [EVENTLOG_DESIGN.md](EVENTLOG_DESIGN.md)
(the EventLog service in session 0, `wevtsvc.dll`, its pipe protocol, store and access rules: "L1 as built").

**The owner's words (2026-11-27):** Linux system logs in Event Viewer are worth a lot; streaming debug output must not
go through the Event Log.

## Scope

1. **Journal to Event Log (the main part).** Selected journal entries become Windows events: a systemd unit that fails
   shows as a Service Control Manager event in System, logons and logoffs from logind and PAM in Security, kernel and
   disk errors in System. An explicit table maps journal fields to log, source, event id and type. Rate-limited and
   filtered, never a copy of the journal.
2. **Event Log to journal (cheap).** Every event the service stores is also written to the journal as a structured
   entry, so `journalctl` sees what Windows programs report.
3. **Never:** `WINEDEBUG` traces in the Event Log. They stay in `/var/log/wineexplorer` (the session 0 logs) and the
   sessions' own log files.

## Clean room

- Event ids, source names and log names are the ones XP shows, taken from Microsoft's public documentation of those
  events or from our own black-box observation in the lab VM. No message text is copied: our message resource module
  (EVENTLOG_DESIGN section 9, "Wine's own sources") has our own wording with the same insertion strings in the same
  order, so a program or script that reads `EVENTLOGRECORD`s sees the documented ids and strings.
- systemd's message ids (`MESSAGE_ID`) come from systemd's own catalog (LGPL-2.1, the installed `journalctl
  --list-catalog`); the ids in the table below are checked against Debian 13's catalog before code (draft values).

## Who reads the journal: a native bridge, not session 0

Reading the journal needs the `systemd-journal` (or `adm`) group, which reads every unit's and user's messages. Session
0 is deliberately not root-equivalent (owner, 2026-11-24) and runs Windows code as SYSTEM; giving its account the whole
journal would put every user's journal behind any flaw in that code. So:

- **`wex-journal-bridge`**, a small native C program (GPL-2.0-or-later, `src/wex-journal-bridge/`), runs as its own
  system account `wexjournal` (member of `systemd-journal` only), from `wex-journal-bridge.service`: `NoNewPrivileges`,
  `ProtectSystem=strict`, `ProtectHome=yes`, `PrivateNetwork=yes`, `RestrictAddressFamilies=AF_UNIX`, a
  `RuntimeDirectory` for its socket, no capabilities.
- It reads with `sd-journal` (libsystemd, already a dependency of the session tools), applies the mapping table and the
  rate limits, and serves **only mapped events** on a Unix socket, `/run/wineexplorer-journal/events` (owner
  `wexjournal`, group `wexsys`, mode 0660). It checks each connection with `SO_PEERCRED`: session 0's account only.
- The EventLog service in session 0 (`wevtsvc.dll`) connects with Winsock's `AF_UNIX` (Wine supports it; no Unix
  library needed) and stores what it receives as records of the mapped log and source. The bridge never writes the
  Event Log's files, and session 0 never reads the journal.
- **Exactly once across restarts:** each event carries its journal cursor. The service stores the cursor of the last
  event it wrote (`C:\windows\system32\config\journal.cursor` in session 0's prefix, written after the record) and
  sends it when it connects; the bridge resumes after it. First start: from the current boot.
- Without the bridge (not installed, plain prefixes, development mode) nothing changes.

Alternative considered: `journalctl --follow -o json` started by the service. Rejected: it would need the journal
group on session 0's account (above), and the project prefers a service over spawning tools.

## The mapping table

An explicit table, compiled in with defaults and extended by a root-owned file, `/etc/wineexplorer/journal-events.conf`
(the bridge refuses a file that is not root's or that group or others can write). Rules are tried in order; the first
match wins; an entry that matches no rule is dropped. Fields: `MESSAGE_ID`, `_SYSTEMD_UNIT`, `UNIT` (the unit an entry
of PID 1 is about), `SYSLOG_IDENTIFIER`, `_TRANSPORT`, `_KERNEL_SUBSYSTEM`, `PRIORITY`.

| # | Journal entry | Log | Source | Event id | Type | Insertion strings |
|---|---|---|---|---|---|---|
| 1 | PID 1, unit failed to start (`MESSAGE_ID` unit failed, during start) | System | Service Control Manager | 7000 | Error | service name (the unit's `Description=`), the result (`exit-code`, `timeout`, ...) |
| 2 | PID 1, unit failed after running (unit failed, or process exited with a failure, while active) | System | Service Control Manager | 7034 | Error | service name |
| 3 | PID 1, unit restarted after a failure (`Restart=` scheduled) | System | Service Control Manager | 7031 | Error | service name, the restart count, the restart delay |
| 4 | Kernel, block layer I/O error (`_TRANSPORT=kernel`, `_KERNEL_SUBSYSTEM=block` or `scsi`, `PRIORITY` <= 3) | System | Disk | 11 | Error | the device (`/dev/sda` as `\Device\Harddisk0\DR0` is not attempted: the Linux name is given) |
| 5 | Kernel, file system error (ext4/btrfs/xfs, `PRIORITY` <= 3) | System | Linux Kernel | 2 | Error | the message |
| 6 | Kernel, out of memory kill | System | Linux Kernel | 3 | Error | the process, the message |
| 7 | Kernel, any other entry with `PRIORITY` <= 2 (critical) | System | Linux Kernel | 1 | Error | the message |
| 8 | logind, new session (`MESSAGE_ID` new session) | Security | Security | 528 (success audit) | Audit Success | user, domain (the computer), logon id, logon type (2 interactive at the greeter, 10 for a remote seat, 3 for ssh) |
| 9 | logind, session removed | Security | Security | 538 | Audit Success | user, domain, logon id, logon type |
| 10 | PAM authentication failure (`SYSLOG_IDENTIFIER` of the greeter, `login`, `sshd`; `pam_unix` failure text) | Security | Security | 529 | Audit Failure | user, domain, logon type |
| 11 | systemd-coredump, a process dumped core | Application | Linux Kernel | 4 | Error | the program, the signal |
| 12 | Any unit listed in the table's `units=` set (wex-*.service, the display manager, NetworkManager), `PRIORITY` <= 3 | System | the unit's `SYSLOG_IDENTIFIER` | 1 | Error | the message |

- **Types:** `PRIORITY` 0-3 Error, 4 Warning, 5-6 Information; 7 (debug) is never mapped. Rule 12's set is where an
  administrator adds units; rows 1-3 apply to every system unit, since a failed unit is what Windows' Service Control
  Manager reports.
- **New sources** ("Linux Kernel", and rule 12's per-unit sources) are registered by the service under
  `HKLM\System\CurrentControlSet\Services\EventLog\System\<source>` the first time they log, with our message module
  (event 1: "%1"). XP's sources (Service Control Manager, Disk, Security) are registered by `wine.inf` with the same
  module.
- **Service names:** Windows shows a service's display name; the unit's `Description=` is the closest (read by the
  bridge from the entry's `UNIT_DESCRIPTION` or, failing that, the unit name).
- **Security events are gated by the audit policy**, as on Windows: rows 8-10 are stored only when the machine's
  "Audit logon events" policy (`secpol`, success and failure separately; GPO lab) enables them. XP's default is no
  auditing, so by default nothing is written to Security. The bridge sends them; the service decides, since it owns
  the Security log and reads the policy. This is the Security log's first writer (it was created empty in L1).
- **Logon ids:** logind's session id, mapped to a stable 64-bit value per boot (Windows shows a LUID).

## Rate limits and filters

- In the bridge, per rule and source: a token bucket of 10 events a second with a burst of 50. Over it, entries are
  counted, not sent; once a minute while any were dropped, one event says so (System, source "Linux Kernel", id 5,
  Warning: the rule, the source and the count).
- Overall: at most 100 events a second to the service; the socket's back-pressure does the rest (the bridge stops
  reading the journal while the service does not read; the journal keeps the entries, and the cursor resumes).
- `PRIORITY` filters per rule (above); debug and info entries are only mapped where a row asks for them (logons).
- **No debug output, ever (scope 3):** entries of Wine's own processes (`_COMM` `wine`, `wine64`, `wineserver`,
  `wine-preloader`, or `_EXE` under the Wine installation) are dropped before the table, and so is any `MESSAGE` that
  looks like a Wine debug line (`^[0-9a-f]{4}:(trace|warn|err|fixme):`), whatever unit sent it. The Event Log to
  journal direction writes event records only.

## Event Log to journal

Each record the service stores is sent back to the bridge on the same socket (a separate frame type), and the bridge
writes it with `sd_journal_send`: `SYSLOG_IDENTIFIER=wex-eventlog`, `PRIORITY` from the type (Error 3, Warning 4,
Information and audit 6), `MESSAGE` "<log>/<source> <event id>: <insertion strings joined by ' | '>" (the message is not
formatted: the message module lives in Windows), and the fields `WEX_EVENTLOG_LOG`, `WEX_EVENTLOG_SOURCE`,
`WEX_EVENT_ID`, `WEX_EVENT_TYPE`, `WEX_EVENT_RECORD`, `WEX_EVENT_USER_SID`. Records that came from the journal are not
sent back (they carry their cursor), and the bridge drops `wex-eventlog` entries before the table, so nothing loops.
Security records are written to the journal only when the bridge's config allows it (`security_to_journal=no` by
default: the journal is readable by the `adm` group, the Security log only with `SeSecurityPrivilege`).

## Who sees what

Application and System are readable by every interactive user (Windows' defaults, kept by L1), while the journal is
readable only by `adm` and `systemd-journal` on Debian. Mapped events therefore become visible to more people than the
journal entries they came from. The table limits this to coarse facts (a service failed, a disk error, a core dump's
program name). Rule 12's and rules 5-7's free-text messages are the exception, so they go to System only when the
table says so, and the default set is small. The alternative, a custom log "Linux" with a `CustomSD` that only
administrators read, is open for the review (question 1).

## Order of work

1. **J1: the bridge and System.** The bridge, its account, unit and socket; rows 1-7 and 11-12; the rate limits; the
   cursor; Event Log to journal. Tests (suite and an installed-image stage): a unit that fails gives 7000 and 7034 in
   System with the unit's description; a unit failing in a loop is rate-limited and reports the count; a Wine debug
   line in a unit's output is never stored; a session 0 restart resumes without a duplicate or a gap; the socket
   refuses any account but session 0's; an event reported by a Windows program appears in `journalctl
   SYSLOG_IDENTIFIER=wex-eventlog` with its fields.
2. **J2: Security.** Rows 8-10, gated by the audit policy, with the GPO lab's `secpol` test: no events by default, 528
   and 538 at a greeter logon and logoff once success auditing is on, 529 at a wrong password once failure auditing is
   on.
3. **J3: EventLog's own boot events** (6005 at the service's start, 6006 at a clean stop, 6009 with the version, 6008
   when the previous boot did not stop cleanly, found from the journal's boot list), and whatever the first users ask
   for.

## Open questions for the review

1. Free-text kernel and unit messages: into System (visible to every interactive user, as Windows' System is), or into
   an administrators-only custom log "Linux"?
2. Is the bridge's account, `wexjournal` in `systemd-journal`, the right boundary, or should it read only the system
   journal (`SD_JOURNAL_SYSTEM`)? The draft does the latter: users' own journals are never read.
3. The Security rows: is logind plus PAM the right source of truth, or should the greeter (our `logonui`/msgina path)
   report logons to the Event Log itself, with the journal used only for ssh and console logins?
