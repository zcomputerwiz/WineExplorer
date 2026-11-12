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
| T2 | permissions stored, journal and checkpoint, refused writes, virtual store | |
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
  saved group is the system group and whose real group is not, and whose executable is the configured
  `wineserver`; development (`WEX_SESSION0_DEV=1` in session 0's environment) accepts only its own uid. A
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

Production packaging for ST-10 (written by Antigravity, reviewed) provides the `installer/systemd/wex-session0.service` unit and setup automation in `scripts/setup-machine-root.sh`. The service runs `wineserver --session0 -f` as the unprivileged system account `wexsys` (primary group `wexsys`, shell `/usr/sbin/nologin`, home `/var/lib/wineexplorer/machine`) with `WINEPREFIX` set to the machine root. It starts before `display-manager.service` and ensures `/run/wineexplorer` exists mode 0755 owned by `wexsys`. Production setup is opt-in via `WEX_SESSION0=1`, which creates the `wexsys` account if missing, installs and enables the systemd service, and records `session0_socket=/run/wineexplorer/session0` and `session0_user=wexsys` in `/etc/wineexplorer/sysrun.conf`. With `WEX_SESSION0` unset, setup preserves existing behavior and configuration byte for byte. The launcher
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

Open in T1:

* **ST-10 production test:** the unit and setup exist but have not run on a machine yet; with the sysrun
  fixture: session 0 as the `wexsys` account,
  and the production trust test with a launcher-started server (the sysrun fixture); with it, the checks that a
  standard user's machine write is refused by session 0 and an elevated administrator's is accepted.
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
