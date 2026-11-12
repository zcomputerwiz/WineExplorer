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
| T0 spike and measurements | ST-01 registry calls per program start; ST-02 the server's own registry use; ST-03 key handles that cross servers; ST-04 prototype of registry calls against a second server; ST-05 limited-write list from a traced limited session | ST-01 to ST-03 done, ST-04 next |
| T1 session 0 owns the machine trees | ST-10 account, unit, socket, peer test; ST-11 registry-only client in the server; ST-12 attested channel set-up by the user's server; ST-13 `ntdll` routing and handle mapping; ST-14 user servers stop loading and saving machine hives; ST-15 reconnect; ST-16 two-user tests | after T0 |
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

## ST-04 Prototype of registry calls against a second server

Next. Scope: a second server instance that accepts a registry-only connection; `ntdll` sends open, query, enumerate
and close for `\Registry\Machine` to it behind a development switch; a 32-bit and a 64-bit program read a
machine value that exists only in the second server.
