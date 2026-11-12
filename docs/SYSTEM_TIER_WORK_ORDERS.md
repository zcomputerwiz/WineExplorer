<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
<!-- Copyright 2026 WineExplorer contributors -->
# System tier (session 0): plan and work orders

Design: [SYSTEM_TIER_DESIGN.md](SYSTEM_TIER_DESIGN.md). This file holds the decisions, the order of work and the
result of each work order. Stock Wine changes are listed in [STOCK_CHANGES.md](STOCK_CHANGES.md) when made.

## Decisions

The user approved the design note as a whole on 2026-11-11 ("go ahead and write up the plan then start
implementation"). Its six recommendations are therefore taken as decided; any of them can be reopened.

1. Shape: session 0 is an always-running machine server that holds the only copy of the registry; programs
   connect to it directly for registry calls.
2. Identity: a dedicated `wexsys` account, not root.
3. Limited machine writes: refused, with the virtual store for 32-bit interactive programs.
4. User hives are hosted in session 0 in the end; the machine trees go first.
5. The user's launcher-started server attests a program's token to session 0; anything else is a standard user
   by its Unix identity.
6. Order: T0, then T1 with the split of `services.exe` alongside.

## Order of work

| Phase | Work orders | State |
|---|---|---|
| T0 spike and measurements | ST-01 registry calls per program start; ST-02 the server's own registry use; ST-03 key handles that cross servers; ST-04 prototype of registry calls against a second server; ST-05 limited-write list from a traced limited session | ST-01 to ST-04 done, ST-05 open |
| T1 session 0 owns the machine trees | ST-10 account, unit, socket, peer test; ST-11 registry-only client in the server; ST-12 attested channel set-up by the user's server; ST-13 `ntdll` routing and handle mapping; ST-14 user servers stop loading and saving machine hives; ST-15 reconnect; ST-16 two-user tests | ST-11, ST-12, ST-13, ST-15 and development ST-10/ST-16 done; production ST-10, notification, ST-14 rest open |
| T2 | permissions stored, journal and checkpoint, refused writes, virtual store | permissions stored (T2a), refused writes, lazy flush (T2c) and the virtual store (T2b) done; journal open |
| T3 | tier-only trees; account policy, `net accounts`, verifier, Group Policy | |
| T4 | user hives in session 0 | |
| T5 | `services.exe` front and back (the in-place split can start during T1), session-0 services, pipe proxy | |
| T6 | broker decision in session 0, session ids | |

Rules for every work order: a switch keeps today's mode until T1 is accepted; every step is tested with a 32-bit
and a 64-bit program; the existing suites (sysrun server, lab S6, broker) pass with the switch off.

## ST-01 Registry calls per program start (2026-11-11)

Method: `WINEDEBUG=+server` in a plain prefix on the main build, parsed by `scripts/lab/registry-trace-stats.py`
(key handles are followed from open and create to close, so each request is counted under the tree of its key).

| Run | Server requests | Registry requests | Machine tree | User tree | Other |
|---|---|---|---|---|---|
| First program of a session (`cmd /c exit`, 64-bit; the server, `wineboot` and services start: 9 processes) | 14205 | 5503 (38.7%) | 4442 | 1024 | 37 |
| `cmd /c exit` in a running session (64-bit) | 1378 | 627 (45.5%) | 427 | 198 | 2 |
| `cmd /c ver` in a running session | 1382 | 627 (45.4%) | 427 | 198 | 2 |
| The 32-bit lockout probe in a running session | 745 | 221 (29.7%) | 118 | 103 | 0 |
| `reg query` of one `HKLM` key in a running session | 1206 | 484 (40.1%) | 328 | 155 | 1 |
| The desktop shell for 25 s under Xvfb (13 processes) | 25368 | 7162 (28.2%) | 5651 | 1474 | 37 |

By request, shell run: `get_key_value` 2417, `open_key` 1599, `enum_key_value` 1004, `enum_key` 755, `create_key`
744, `set_key_value` 593, `delete_key` 35, `delete_key_value` 14, `set_registry_notification` 1.

What it says:

* The registry is 30 to 45 percent of everything a program asks its server, and about two thirds to three
  quarters of that is the machine tree. Session 0 will carry roughly a quarter to a third of today's request
  load of every session; one program start is a few hundred machine-tree requests.
* The cost per call does not change (one round trip, as today), but the load moves to one single-threaded
  server for all users. That makes the later read-only shared mapping more than an optimisation for busy
  machines; it is measured again in T1 with two sessions.
* A plain session writes to the machine tree while it starts (shell run: 1136 create or set requests under
  `Machine`, counting `create_key` of keys that already exist). These are the per-session keys and the boot-time
  state; ST-05 lists them from a limited session, because in session 0 each needs a home: per-session overlay,
  grant, or refusal.

Not measured here: an installer, a browser, and a limited session (ST-05).

## ST-02 The server's own registry use (2026-11-11)

Read from `wine/server` (outside `registry.c`, which is the registry itself):

| Use | Where | Needed in a user's server once session 0 holds the registry |
|---|---|---|
| The prefix id of the binding (R8) | `named_pipe.c` 2247, `registry_get_prefix_id` (`registry.c` 176) | Asked from session 0 at start, or kept by session 0 and attested |
| Whether elevation is enforced (the helper setting), read when the first client connects | `process.c` 845, `elevation_helper_unset` (`registry.c` 538), `elevation_enforced` | A start-up value from session 0; the on-disk value decides today, so nothing live is lost |
| Access check of a key against a token and its descriptor | `token.c` 1625-1645, `handle.c` 727-738 | Moves to session 0 with the keys |
| Profile hive bookkeeping for E1 | `registry.c` 2601 | Moves with the user hives (T4) |
| Flush at exit and on signal | `request.c` 1045, `signal.c` 162 | User hive only until T4 |

So a user's server needs two start-up values from session 0 and no live registry reads of machine keys.

## ST-03 Key handles that cross servers (2026-11-11)

Counted in the same traces as ST-01.

| Case | Count in all runs above |
|---|---|
| Key handle duplicated (`dup_handle` of a key) | 0 |
| Key opened or created inheritable | 0 |
| Key watched (`set_registry_notification`) | 1, in the shell run, on a user-tree key |

The cases that need special handling when key handles come from another server are rare in these runs. They
still have to work (an installer or a service may use them); the prototype handles notification first, since
the shell uses it.

## ST-04 Prototype of registry calls against a second server (2026-11-11)

A spike to show the shape works, behind a development switch; not the product path.

* Switch: the environment variable `WEX_REGISTRY_SERVER` holds the socket path of a second Wine server. Unset,
  nothing changes.
* `dlls/ntdll/unix/server.c`: on the first machine-registry call the process makes the ordinary client handshake
  with that server on a connection of its own (`regsrv_connect`) and then sends registry requests there
  (`regsrv_call`, one connection per process under a mutex). Key handles from it carry a tag bit (0x40000000), so
  later calls and `NtClose` go to the right server.
* `dlls/ntdll/unix/registry.c`: `NtCreateKey` and `NtOpenKeyEx` choose the server by name (an absolute name under
  `\Registry\Machine`) or by the parent handle; delete, enumerate, query, set, delete value and flush follow
  the handle.
* The second server is unchanged: it sees an ordinary client.

Test (`scripts/lab/system-tier-st04.sh`, written for the wexbuild main build tree; two plain prefixes on the main build, one standing in for session 0; 64-bit `reg.exe`
and the 32-bit `reg.exe` with `/reg:64`), 16 checks pass:

* switch off: both programs read the session's own value;
* switch on: both read the value that exists only in the second server; values written and a subkey created
  from the session arrive there and are enumerated; a value is deleted; a child process started by `cmd` reads
  from the second server too;
* separation: `HKCU` written with the switch on stays in the session and is absent in the second server; the
  session's own `HKLM` copy is untouched by the writes.
* Cost: 20 runs of `reg.exe` take 1145 ms with the switch off and 1126 ms on, so no measurable difference.
* With the switch off the normal suite (325 checks) and the broker test pass on the same build.

What the spike leaves out, each a T1 work order: the second server treats the program as a full process (it has
its Unix pid and would signal it; ST-11 makes it a registry-only client); no token attestation (ST-12); change
notification, rename, save, load and security calls on a tagged handle are not routed; duplicating or inheriting
a tagged handle does not work (ST-13); a lost connection ends the thread (ST-15); the session's server still
loads and saves its own machine hive (ST-14); a name relative to a handle on `\Registry` itself is not routed.

First findings for T1:

* The 32-bit registry view needs no extra work: redirection happens before the request and travels with it.
* A second server that is not persistent exits a few seconds after its last client, and every registry call
  then fails. Session 0 must be a service, and `ntdll` needs the reconnect before anything is switched over.

## T1 first increment: ST-11, ST-12, ST-13, ST-15 and the development form of ST-10 (2026-11-11)

Stock Wine changes: "System tier: session 0 holds the machine registry" in `STOCK_CHANGES.md`.

How it works now:

* **Session 0** is a `wineserver --session0` for the machine root's prefix. It listens on a SEQPACKET socket
  (mode 0666): `session0_socket=` in `/etc/wineexplorer/sysrun.conf`, or `WEX_SESSION0_SOCKET` when there is no
  such line. It runs until stopped (no exit after the last client).
* **Channel.** A program's first machine-registry call asks its own server for a channel (`get_registry_channel`).
  That server describes the program's primary token (user, owner, primary group, groups, privileges,
  restricting SIDs, capabilities, AppContainer, integrity, elevation, session, default DACL), connects to session
  0, and sends the description with one end of a new socket pair; it hands the other end to the program.
  Session 0 rebuilds the token and serves its end as a **registry-only client**: a process object with that
  token whose requests outside 18 registry and handle requests fail with access denied, and which has no Unix
  pid (session 0 never signals it) and cannot change session 0's debug level.
* **Trust** (session 0's side, per connection): production accepts a server whose uids are all the peer's, whose
  saved group is the system group and whose real group is not, and whose process name is `wineserver` (only
  the setgid launcher gives a non-member that saved group, and it starts only the configured server and the
  machine root's wineboot, both non-dumpable; the executable itself cannot be checked, since `/proc/<pid>/exe`
  of a non-dumpable process of another uid is not readable, which the first production run showed);
  development (`WEX_SESSION0_DEV=1` in session 0's environment) accepts only its own uid. A
  launcher-started user server reads the socket from the configuration only, never from its environment.
* **Routing** (`ntdll`): create and open of an absolute path under `\Registry\Machine` or `\Registry\User\.Default`
  go to session 0, except the per-session keys of `wine/wex_session0.h` (the 20 the merge-on-save work already
  kept per session, plus `Hardware` and the service manager's `ServiceCurrent`). Opens relative to a session 0
  key are sent relative to it (so 32-bit redirection is decided by session 0 as before), unless the target is a
  per-session key, which is opened in the process's own server by its absolute path. Every other call follows
  the handle.
* **Handles.** Session 0 key handles are entries of a table in `ntdll` (0x40000000 + index * 4) with the path,
  access and attributes. A lost channel (session 0 restarted) is replaced at the next call, and each key is
  reopened by its path before its first use on the new channel.
* **User servers** in this mode still load the machine hives as a snapshot (for the per-session keys and the
  two start-up values of ST-02) but never save them.

Test: `scripts/lab/system-tier-t1.sh` (development mode, all as root except the last case; probe
`scripts/system-tier-probe.c`, 32-bit and 64-bit), 32 checks pass:

* two user sessions A and B at the same time: A reads session 0's value (64-bit and 32-bit `reg.exe`); a value
  written in A is read in B at once and is in session 0; a subkey created by B's 32-bit `reg.exe` is enumerated
  in A; a delete in A is seen in B;
* change notification: a 64-bit and a 32-bit program in B waiting on a machine key are woken by a write in
  A; without a change the wait times out;
* per-session keys and `HKCU` stay in the session: a value under `HKLM\Software\Wine\Drives` written in A
  (`reg.exe` opens it relative to its `HKLM` handle) is read in A and absent in B and in session 0;
* a duplicated key handle reads (64-bit and 32-bit); `NtQueryObject` gives the key's name; the security
  descriptor is returned;
* a program holding a key while session 0 is stopped and started again reads the new value through the same
  handle and creates a subkey under it; session 0 had saved B's earlier write to its `system.reg`;
* a server without the setting keeps its own machine registry;
* a session of another uid: its server is refused by session 0, and its `reg query` of a machine key fails.

### Session 0 service

Production packaging for ST-10 (written by Antigravity, reviewed) provides the `installer/systemd/wex-session0.service` unit and setup automation in `scripts/setup-machine-root.sh`. The service runs `wineserver --session0 -f` as the unprivileged system account `wexsys` (primary group `wexsys`, shell `/usr/sbin/nologin`, home `/var/lib/wineexplorer/machine`) with `WINEPREFIX` set to the machine root. It starts before `display-manager.service` and gets its own runtime directory `/run/wineexplorer-session0` (mode 0755, owned by `wexsys`); `/run/wineexplorer` stays root's, since the boot-mode records of Safe Mode live there and are trusted only in a root-owned directory. Production setup is opt-in via `WEX_SESSION0=1`, which creates the `wexsys` account if missing, installs and enables the systemd service, and records `session0_socket=/run/wineexplorer-session0/session0` and `session0_user=wexsys` in `/etc/wineexplorer/sysrun.conf`. With `WEX_SESSION0` unset, setup preserves existing behavior and configuration byte for byte. The launcher
(`tools/wex-sysrun`) accepts the two new configuration keys, and `installer/package-wine.sh` stages the unit.
The machine root stays root's (the launcher trusts only root-owned paths): session 0 writes it through the
system group, the server accepts a root-owned prefix with `--session0`, and a user's server accepts machine
hives owned by the `session0_user` account.

### Change notification on a session 0 key

A program registers its event with its own server (`relay_registry_notification`, which returns a cookie) and
passes the cookie to session 0 with `set_registry_notification`. Session 0 writes the cookie to the channel's
wait pipe when the key changes; the read end of that pipe is the program's relay in its own server
(`set_registry_relay`, sent when the channel is set up), which sets the event. When the channel is lost, the
relay sets every pending event: a spurious change report rather than a lost one.

### Production test with the launcher (2026-11-12)

`scripts/test_sysrun_server.sh`, section "system tier: session 0" (skipped with `WEX_SKIP_SESSION0=1`): the
suite's fixture (machine root, setgid launcher, administrator `wexadm` in sudo, standard user `wexstd`, elevation
through the test broker), with session 0 started as a throwaway account of the system group and
`session0_socket`/`session0_user` added to the configuration. 7 checks pass, normal suite 332 in all:

* session 0 runs as its own account; its socket is mode 0666;
* a standard user's program reads `HKLM` from session 0 (a value that differs from the hive file the user's
  server loaded), with the 32-bit and the 64-bit probe;
* a standard user's write and an administrator's limited (not elevated) write to `HKLM` are refused;
* an administrator's elevated `reg add` (through the broker) is accepted, and the standard user reads it at once;
  the refused writes are absent;
* a server the user starts without the launcher (it reads the same configuration) is refused by session 0, and
  its programs' machine-registry calls fail;
* session 0 saves the hive as its account; the users' servers accept that file, and the write is there after a
  restart of session 0.

The first run of this section found the trust bug above (session 0 refused every launcher-started server).

### T2a: key permissions are stored (2026-11-12)

Written by Qwen, reviewed. Session 0 saves a key's own security descriptor as an `#sd=` line (hex) in its hive;
every server loads such a line (malformed lines are reported and ignored); keys whose only change is their
descriptor are saved too. `scripts/lab/system-tier-t1.sh` adds three checks (37 in all): a DACL set from a user
session reaches session 0's `system.reg` and is the same after a restart of session 0. Open: the merge-on-save
path of user servers does not carry descriptors (not used with session 0).

Open in T1:

* **ST-10 on an installed machine:** the unit and `setup-machine-root.sh` with `WEX_SESSION0=1` have not run on
  an installed system yet (ISO build and VM test).
* **ST-14** rest: the user server's snapshot could be dropped once the two start-up values come from session 0.
* **ST-05:** which machine writes a limited session makes at start-up, now that session 0 decides them. The tool
  is ready: `scripts/lab/registry-trace-stats.py --writes` lists them by key, classified per-session or
  session 0 from the header's lists (written by Qwen, reviewed; tests in
  `scripts/lab/test_registry_trace_stats.py`).
* Known limits, by design for now: the token is the process's primary token (impersonating threads are checked
  as the process); one channel per process shared by its threads; a key opened relative to a session 0 key and
  reopened after a restart is reopened by its textual path (a 32-bit-redirected parent could then resolve
  differently); `NtLoadKey`, `NtSaveKey`, `NtUnloadKey` and handle inheritance by a child process do not work
  with session 0 keys; a session 0 that does not answer within 2 s is treated as absent, and while it is absent
  machine-registry calls fail (no fallback to the snapshot).

## ST-17 Load measurement script

Measurement script `scripts/lab/system-tier-perf.sh` benchmarks the system tier under concurrent two-user load. Run as root in the wexbuild WSL environment (`BUILD=/root/WE-prefetch/build/wow64 bash scripts/lab/system-tier-perf.sh [N]`, default N=40 via `PERF_N`). It measures wall-clock times with session 0 disabled versus enabled across two workloads running in two user sessions simultaneously: `wine cmd /c exit` (half sequential, then half in parallel per session) and sequential `wine reg query "HKLM\Software\Microsoft\Windows NT\CurrentVersion"`, reporting `perf <name> off_ms=<n> on_ms=<n> ratio=<ratio>` and session 0 versus user servers CPU consumption in milliseconds (`perf session0_cpu_ms=<n> user_servers_cpu_ms=<n>`).

Script written by Antigravity, reviewed.

Results (2026-11-12, wexbuild, main build, N=40, development mode, two sessions at the same time):

| Workload | Without session 0 | With session 0 | Ratio |
|---|---|---|---|
| `cmd /c exit`, 20 sequential starts per session | 1678 ms | 1563 ms | 0.93 |
| `cmd /c exit`, 20 parallel starts per session | 454 ms | 449 ms | 0.99 |
| `reg query` of one `HKLM` key, 40 sequential | 2696 ms | 2583 ms | 0.96 |

CPU during the session 0 run: session 0 540 ms, the two users' servers 1390 ms together. So at this load session
0 adds no measurable time; it carries about a quarter of the servers' work, as ST-01 predicted. A single run on
an idle machine: the differences are within noise. Not measured: many sessions, a heavy installer.

### T2c: lazy flush (2026-11-12)

Session 0 saves changed hives every 5 s (unchanged hives are not written), as the lazy flush of Windows does,
instead of the 30 s of other servers. Its saves are atomic (temporary file, then rename: its prefix is the
machine root, so it takes the stock save path). A journal is still open.

### Requests from the Safe Mode work (2026-11-12, Integration session)

* Session 0 has its own runtime directory, `/run/wineexplorer-session0`; `/run/wineexplorer` stays root's (the
  boot-mode records live there).
* `System\{ControlSet001,CurrentControlSet}\Control\SafeBoot\Option` is a per-session key: each session's
  wineboot writes it from the boot mode, and `SM_CLEANBOOT` reads it in the session.
* `wex-lastgood` takes `flock -s` on `<machine root>/.reg.lock` while it copies the control set.
* T5: the Safe Mode service filter (auto-start of `SafeBoot\Minimal`/`Network` services only, `StartService`
  refusing others with 1084) stays in the half of `services.exe` that moves to session 0.
* `setup-machine-root.sh` writes `builtin_administrator=administrator` by default (Safe Mode C4).

## T4-0: each user's own SID with session 0 (2026-11-12)

Found while planning T4: Wine gives every user's logon token the same SID, S-1-5-21-0-0-0-1000. With one
registry for all sessions that is wrong twice: two users' hives would have the same path under `\Registry\User`,
and session 0's access checks cannot tell users apart (a key whose descriptor names that SID, for example through
CREATOR OWNER, is open to every user).

Change (server): a server whose programs use session 0 takes its user's SID from the E1 accounts map
(`accounts_own_sid`, the active record of its uid; the account bound to the legacy SID keeps
S-1-5-21-0-0-0-1000), at start, before the registry is loaded. It is the token user of the logon token, the SID of
files owned by the user (`security_unix_uid_to_sid`), the name of `HKEY_CURRENT_USER` and the SID the user's key
default descriptor grants. A server without session 0, or a user missing from the map, keeps Wine's fixed SID.
No migration of `user.reg` is needed: its keys are stored relative to the hive root.

Test: the sysrun suite's session 0 section runs `wex-accounts sync`, then checks with the probe's `whoami` mode that
the standard user's and the administrator's tokens carry their own mapped SIDs and differ; the elevated write
through the broker still works (normal suite 333, broker test, T1 test).

## T4a: the user's own hive in session 0 (2026-11-12)

* **Load.** At the first channel request of a program whose token user is the server's user, the user's server
  opens its `user.reg` (or none for a new prefix) and sends it to session 0 with the token description on a
  link that stays open (message `USER_HIVE`). Session 0 loads it under `\Registry\User\<SID>`, creates
  `<SID>_Classes` as a link to its `Software\Classes`, and gives the hive's keys a default descriptor for that
  SID (full access for it, SYSTEM and Administrators). A second load of the same SID (another server of the same
  user) is refused; that server keeps its own copy.
* **Save.** Session 0 cannot write in the user's home, so it sends the hive (a memory file) back over the link
  whenever it changed (every 5 s), and the user's server writes `user.reg` through a temporary file and a rename.
  The user's server no longer saves its own copy. At its exit it sends `HIVE_UNLOAD`; session 0 sends the last
  changes and drops the hive. If the server dies, session 0 drops the hive without a last save.
* **Routing.** The channel reply says that session 0 holds the user hive; `ntdll` then sends
  `\Registry\User\<own SID>` and `<own SID>_Classes` there (the process token's user), and every other user path
  (another user's SID, E1 managed profiles) stays local.
* **Session 0's own HKCU** is SYSTEM's (`S-1-5-18`), never a user's, so no user's hive can collide with it.

Test (sysrun suite, session 0 section): a standard user's `HKCU` write reads back, is not in the user's own
`user.reg` while the session runs (session 0 holds it), is in `user.reg` (the user's own file) after the user's
server exits, and is read again in the next session. Normal suite 334, broker test, T1 test pass.

Open: E1 managed profiles (T4b); other users' hives are not visible in `HKEY_USERS` of a session (Windows shows
every loaded hive, subject to its descriptor); a server that dies loses up to 5 s of its user's changes; the
user's server still loads its own copy at start (unused once session 0 holds the hive).

## T2b: the virtual store (2026-11-12)

Codex was given the brief and returned without an implementation (it asked one question about routing, then
listed open points); Claude implemented it.

* **Who.** A 32-bit (WoW64) program whose token is not elevated (`TokenElevationType` not Full) and is not SYSTEM,
  LocalService or NetworkService. A manifest's `requestedExecutionLevel` is not known to `ntdll` and is treated
  as not stated.
* **Where.** Keys under `HKLM\Software` (with or without `Wow6432Node`) except `Classes`, `Microsoft\Windows` and
  `Microsoft\Windows NT`, opened through session 0.
* **Store.** `HKCU\Software\Classes\VirtualStore\Machine\Software\Wow6432Node\<rest>` (the user's classes, as
  Windows keeps it in `UsrClass.dat`; the `Wow6432Node` level as Windows shows it for 32-bit programs). It is
  reached with ordinary calls, so it is in session 0 when T4a holds the user's hive.
* **Behaviour.** A create, or an open for writing, that session 0 refuses returns a virtual key: a handle of the
  real key (read only, when it exists) and of the store key (created at the first write, with its missing
  parents). An open of a key whose store exists returns a virtual key too, also for reading, and an open of a key
  that exists only in the store finds it. Values and subkeys: writes and deletes go to the store; reads take the
  store first; enumerations and `NtQueryKey` counts are merged, the store's entries first. Object, security and
  notification calls use the real key (the store when there is none). A virtual key cannot be duplicated or
  renamed.
* Code: `dlls/ntdll/unix/registry.c` (`regvirt_*`), hooks in `server.c` (`NtClose`, `NtDuplicateObject`),
  `file.c` (`NtQueryObject`), `security.c`.

**Found on the way: 32-bit programs saw the 64-bit machine registry through session 0.** The server decides a
request's view from the calling process (`is_wow64_process`), and session 0's registry-only client is a 64-bit
process; the channel is set up before the program's server knows it is 32-bit, so passing the machine at set-up
did not help. Fixed by stating the view in every request: `ntdll` adds `KEY_WOW64_32KEY` (WoW64) or
`KEY_WOW64_64KEY` when the caller chose none, and session 0 keeps that flag for registry-only clients. The T1 test
had read only with `/reg:64` and missed it; it now checks a 32-bit program's own view, and three expectations that
relied on the wrong view were corrected.

Test (sysrun suite, session 0 section, standard user, 32-bit and 64-bit `reg.exe`): a 32-bit write under
`HKLM\Software` succeeds, reads back, is not in the machine key or the machine hive, and is in the user's
`VirtualStore`; a 64-bit write and a write under an excluded key are refused; a 32-bit query of a machine key with
a virtualized value shows both. Normal suite 335, broker test, T1 test pass.

## T6.1: a session id per logon (2026-11-12)

Every server gave its logon session id 1 (`default_session_id`, a constant). A server that uses session 0 now asks
session 0 at its start (message `SESSION_ID`, before the object directories and the first token exist); session 0
gives each trusted server the next id (1, 2, ...; it is 0 itself). Without an answer within 2 s the server keeps 1.
`default_session_id` is now a variable; only Wine's own `win32u` tests hard-code session 1.

Test: the T1 test (development mode) checks that the programs of sessions A and B report different session ids,
both at least 1 (probe `whoami` prints `session=`). T1 test, lab suite 398, normal suite 335, broker test pass.

## T1 close-out and the plan for T2b to T6 (2026-11-12)

T1 is done except the run on an installed machine (ISO build with `WEX_SESSION0=1`, VM test), which waits for
the merge of the Safe Mode and Vintner Boot branches. Decisions taken while closing it:

* **ST-14 (drop the user server's snapshot of the machine hives): not done, on purpose.** The per-session keys
  start from the snapshot's content (drive and port mappings, computer name state, display and device keys
  between boot steps), and the server's own start-up reads use it. It costs memory only; it is never saved
  and never served for any other key. Revisit with T4, when the user server holds nothing else either.
* **ST-05 (machine writes of a limited session): no separate work.** With session 0 a limited session's machine
  writes are refused exactly as the access checks under elevation enforcement already refuse them in its own
  server (the sysrun suite's enforced cases pass unchanged); `registry-trace-stats.py --writes` lists them when
  a regression needs it.

Plan:

| Item | Content | Who |
|---|---|---|
| T2b | Virtual store: a qualifying 32-bit limited program's refused writes under `HKLM\Software` go to its own `VirtualStore`, merged back on reads, in `ntdll` (session 0 unchanged). Needed before session 0 can be on by default | Codex (from 2026-11-12 02:00), Claude reviews and tests |
| T2d | A journal for session 0's hives (today: atomic saves every 5 s) | later, low priority |
| T3 | `HKLM\SAM` and `HKLM\SECURITY` readable by SYSTEM only in session 0; account policy there | postponed (2026-11-12): nothing reads these trees yet, and Wine gives a new key no inherited descriptor, so protecting the trees needs inheritance of key descriptors first; the lockout policy stays in `HKLM\Software\WineExplorer\Elevation`, where `net accounts` and the verifier use it |
| T4a | The user's own hive in session 0: at the first program of a session, the user's server sends session 0 its `user.reg` (descriptor) and the token; session 0 loads it under `\Registry\User\<SID>` (and `_Classes`) and sends the hive back to that server to save, since session 0's account cannot write in the user's home; at logoff it unloads it after a last save. `ntdll` routes `\Registry\User` to session 0. HKCU of SYSTEM and service accounts in session 0 | Claude |
| T4b (postponed: the E1 managed-profile code is lab-only until E1 leaves the lab) | E1's managed profile hives (`NtLoadWexProfileEx`, leases, the root-owned profile storage) move from the user's server to session 0, so an elevated administrator's profile is one hive for every session | Claude, with Codex review of the trust changes |
| T5 | `services.exe` front and back, the back in session 0 (Safe Mode filter in the back); pipe relay with the caller's token; installer and updater services. Waits for the Safe Mode merge (it changes `programs/services`) | design: `SYSTEM_TIER_T5_SERVICES.md` |
| T6 | Broker decision in session 0, session ids per logon | design: `SYSTEM_TIER_T6_BROKER.md` |
