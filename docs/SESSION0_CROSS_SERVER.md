<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Objects across Wine servers (R6 step 3 design note)

Status: design draft 3, 2026-11-24, by session 0 (the orchestrator). Draft 1 was reviewed by Codex, draft 2 by the
Session 0 reviewer (both static, read-only, against development's Wine); their findings are folded in and listed at
the end. **X0 has a go** under the reviewer's five conditions, which this draft writes in (M0, M2, M3, tests). It
continues [SESSION0_PROCESS_IDS.md](SESSION0_PROCESS_IDS.md) (R6 steps 1 and 2: ids leased by session 0, real ids
across the pipe bridge) and takes up option (b) of [SESSION0_SPLIT_REVIEW.md](SESSION0_SPLIT_REVIEW.md), kept in
reserve until now.

**The owner's rule for this design (2026-11-24):** choose the best pattern for each problem; several mechanisms side by
side are fine. The only limit is licence terms: ideas and patterns may be taken from anywhere, code only under a
licence compatible with ours (see "Prior art").

## The problems

With session 0, every user's logon has its own Wine server, and session 0 has one. Objects live in exactly one
server. Today a program can reach another server's objects in only three ways: the machine registry (held by session
0), named pipes (bridged by session 0), and process and thread ids (unique since R6 step 1, but only as numbers).

| # | Problem | Who needs it | Today |
|---|---|---|---|
| P1 | **`Global\` named events, mutexes and semaphores.** A program in a session and a service in session 0 open the same `Global\Name` and expect one object. | Installers and their update services (Edge's updater is the current suspect: Edge fails to install when WebView2 is absent, i.e. when the updater service is set up in the same run), any service that coordinates with a per-user helper | Each server has its own `\BaseNamedObjects`, so the two sides get two different objects and wait forever or race |
| P2 | **`Global\` named sections** (shared memory between a service and a session program) | Same kind of software; agents with a mapped status block | Two unrelated sections |
| P3 | **Waiting on another server's process** and reading its exit code | B76: Click-to-Run waits on Word; any service that tracks its clients | `OpenProcess` on a foreign id fails cleanly (R6 step 1), so the service must run in the caller's server |
| P4 | **Querying and reading another server's process**: basic information, image name, PEB and command line (`VM_READ`), its primary token | B76 (24 opens for query, 6 PEB and command-line reads, the token for query and duplicate, per Word start) | Same as P3; Click-to-Run runs one service per session |
| P5 | **Which server owns an id** | Routing of P3/P4 | Session 0 knows the block table |
| P6 | **A server or session 0 goes away** while another holds references into it | All of the above | Not needed yet |

Out of scope: window messages across sessions (Windows does not allow them either), the registry (already in session
0), named pipes (bridged since T5.2), ALPC and COM's `OLE_<oxid>` pipes (R6 step 4, case by case).

## Trust and constraints

- **Who is trusted** (unchanged since T1, `server/session0.c`'s header and `peer_trusted`): session 0 believes a user's
  server only when the launcher started it: its uids are the user's, its saved group is the system group and its real
  group is not (`wex_sysrun_peer_ok`, with a pidfd pin), and it is not dumpable (`server/sysrun.c`), so the user
  cannot attach to it or change its memory. Its token descriptions (user, groups, privileges, elevation) are believed
  for its own user and its session's system role; another user's account is refused (`described_token_bound`).
  **Programs are not trusted**, in any server. A bug in a user's server that lets a program steer it is therefore a
  machine-wide problem, as it has been since T1; this design adds requests to that surface and keeps each one narrow.
- **Defence in depth where it is cheap.** Draft 1's review pointed out that `described_token_bound` accepts a
  description of SYSTEM, LocalService or NetworkService from any trusted server (its session's system role). That is
  by design, but new paths in this document must not widen it. Three places where draft 2 would have:
  - **The session id.** A description carries its own session id (`wex_token_desc.session_id`), which
    `create_bound_token` does not check. Every request of this design turns a description into a token only through
    one new function, `link_bound_token()`: the account binding of `described_token_bound`, plus a session id that
    must be the link's (a description naming another session is refused). M1's "the session is the link's" rests on
    it.
  - **The impersonation level.** `token_create_described` always builds a primary token, so a thread impersonating at
    identification or anonymous level would be described as if it could act. The caller's server refuses those levels
    with `STATUS_BAD_IMPERSONATION_LEVEL` before it describes the effective token, as Windows does for opens with such
    a token.
  - **A user's server acting for another user's server.** Not in this design: M3 serves only callers in session 0
    (below).
- **Wine's own named objects.** Making `\BaseNamedObjects` machine-wide also makes machine-wide the per-user objects
  Wine creates there directly: `WineWaylandZones` (`dlls/winewayland.drv/wayland_zones.c`) and
  `WineAppContainerRegistrationLock` (`dlls/kernelbase/security.c`), both mutexes. One user could then deny or hold
  another user's. Before X1, every name Wine creates in `\BaseNamedObjects` is audited, and the per-user ones move to
  `\Sessions\N\BaseNamedObjects` (small stock changes, listed in `STOCK_CHANGES.md`). userenv's `Global\` machine
  policy events (`dlls/userenv/gpolicy.c`) are rightly global, and are X1's in-tree test with the GPO lab.
- **Access is decided where the object lives**, against the caller's token, as on Windows. A check in the caller's
  server is at most a shortcut. Access masks sent back with an answer are information, never authority: the owner
  keeps the granted rights in its own table (M0).
- **Plain prefixes, the setup account and development mode keep stock Wine behaviour**: they have no session 0.
- **Session 0 restarts** (updates, crashes) must not hang a session: every reference into another server carries
  that server's epoch and fails cleanly when the server is gone.

## Prior art

| Source | What it does | What we take | Licence and use |
|---|---|---|---|
| Windows' object manager | Per-session `\Sessions\N\BaseNamedObjects`; `Global\` resolves to `\BaseNamedObjects`; server silos give containers their own namespace | The namespace layout (M1) | Behaviour only (documented API, black box) |
| Android Binder | Proxies keyed by a per-process handle the kernel translates; counted references across the boundary; `linkToDeath` tells holders when the owner died | Owner-side reference table, counted references, death notices (M0, M3, M6) | Apache-2.0: pattern only, no code (Apache-2.0 code would make the module GPL-3.0) |
| Chromium's Windows sandbox broker | Targets forward calls to a broker that checks policy and performs them; client-side checks are only a speed-up; brokers serve each other's targets | Forwarding with the decision at the owner (M1-M4) | BSD-3-Clause: pattern; no code planned |
| Wine's own server | Waits are evaluated whole in one place before any object is consumed (`server/thread.c`) | All of a wait's objects in one coordinator (M2) | LGPL, the code we change |
| Linux `ntsync` (6.14) and `server/inproc_sync.c` | NT sync objects as kernel objects behind descriptors | Not used in this draft (see "Deferred"): objects in one wait must share one device instance, and a descriptor lets its holder pass any mutex owner id | Kernel uapi (GPL-2.0 WITH syscall note), Wine (LGPL) |
| Wine's session shared memory (`win32u`) | Server-written section, read-only to clients, seqlock | Not needed now: session 0 routes by its own table (M5 deferred) | LGPL |
| DCOM's object exporter | Proxies kept alive by pings | Not needed while links report closure; kept in mind | Behaviour only |

## M0. The foundation: one capability and lifetime protocol

Everything below uses one protocol on the session link, built first.

- **Server epochs.** Each server has an identity that survives link resets and changes on restart: its Unix pid,
  its start time (as `id_block_owner_alive` already uses) and a random 64-bit boot nonce. The link serial is not an
  epoch (it numbers orders on one connection). Each link also gets a connection number.
- **References are owner-side table entries.** When an owner grants access to one of its objects for a remote
  caller, it stores an entry: the object (pinned with a reference), the rights granted, the caller's account and
  token (for auditing and re-checks), and the link it was issued to. It returns an opaque 64-bit reference id. Every
  later operation names that id and is checked against the entry: right link, right epoch, right granted rights. A
  reference cannot be used on another link or after the owner's restart.
- **Counted, released, cancelled.** The holder releases when its handle closes; release and cancel are idempotent.
  When a link closes for good (the peer's epoch has ended), the owner drops all entries issued to it.
- **Asynchronous dispatch.** Requests that depend on the peer (waits, forwarded queries) are asynchronous on both
  sides: the caller's thread waits as for any server wait, and nothing blocks the server's main loop. The existing
  synchronous `link_request` (2 s) stays for session 0's own short requests only.
- **Tokens.** Every request that carries the caller's identity carries a description, which the receiver turns into
  a token only with `link_bound_token()` (see "Trust and constraints": account, session and impersonation level).
- **Caps on what is in flight, not rate limits** (rate limits fail at random; caps give back-pressure). Per account,
  starting values: 16384 references, 4096 registered waits and subscriptions, 256 requests in flight per link, 4 MiB
  queued. Over a cap: `STATUS_INSUFFICIENT_RESOURCES`. Session 0 logs an account at half of any cap. Frames stay
  within the link's 64 KiB message limit, headers included (`include/wine/wex_session0.h`).
- **Subscriptions are level-triggered.** A subscription to an object's readiness (M2's mixed waits) fires whenever the
  object is ready, not once per change, so no wake-up is lost between a notice and the next request.
- **Try-acquire is not cancellable by the holder.** Once a try is sent, the holder waits for its answer; a success
  wins over a timeout or an APC that arrived meanwhile (the object was taken, so the wait reports it). A failed try
  re-arms the subscription in the same message.

## M1. One global namespace, held by session 0 (P1, P2)

- In a server linked to session 0, `\BaseNamedObjects` (what `Global\` names and the
  `\Sessions\N\BaseNamedObjects\Global` link resolve to, `server/directory.c`) becomes a **remote directory**: opens
  and creations in it go to session 0 over the link. Session 0's own `\BaseNamedObjects` is the one real directory.
  Every alias that resolves into it is rebound, not only `Global`. `\Sessions\N\BaseNamedObjects` stays in the
  session's server, so `Local\` and unprefixed names (`BaseGetNamedObjectDirectory`, the PEB's session) stay local, as
  on Windows.
- Types: event, mutex, semaphore (M2) and anonymous sections (M4). Other types named in the global directory
  (waitable timers, jobs, symbolic links, anything else) **stay local to the caller's server, as today**, with a
  FIXME: refusing them would break what works within one session now.
- Results pass through faithfully: `CreateMutex(Global\X)` on an existing object succeeds with
  `ERROR_ALREADY_EXISTS` (single-instance checks rely on it), a name of another type gives
  `STATUS_OBJECT_TYPE_MISMATCH`, a denied open `STATUS_ACCESS_DENIED`.
- Not covered, documented: `Session\N\` names (`\Sessions\BNOLINKS`), with which a service in session 0 opens a
  session's object. They fail as path-not-found today and keep doing so.
- **Rights after the open.** The owner's reference records the rights granted. `DuplicateHandle` of a remote object
  asking for **more** than that is refused at the holder (`STATUS_ACCESS_DENIED`; a new open is the way to more
  rights); asking for less is enforced by the holder's server, as for any handle. So "masks are information" holds at
  the owner, and the holder's server enforces narrower handles itself.
- AppContainer callers: the description carries the package SID and capabilities, so session 0's check sees them
  (tested).
- Session 0 decides with the caller's token as its server describes it: the object's security descriptor for opens,
  the directory's descriptor for creations, and `SeCreateGlobalPrivilege` for **creating sections and symbolic
  links** from a session other than 0, which is what Windows documents; opens and event/mutex/semaphore creation do
  not need it. The session is the link's, not a claim in the request.
- The answer is a reference (M0). The caller's server makes a local handle to a **remote object** holding it.

## M2. Synchronization objects: one coordinator per wait (P1)

Global events, mutexes and semaphores live only in session 0. A program's handle to one is a remote object in its own
server; every operation goes to session 0, which checks the granted rights in its table and performs it there:
set, reset, pulse, release (with the caller's thread id, which session 0 checks against the lease of the link's
server: a thread can only release a mutex it owns), query.

Waits follow one rule from Wine's own server: **all of a wait's objects are evaluated in one place before any is
consumed** (`server/thread.c`). So:

- **A wait on global objects only** (one object, or several with wait-any or wait-all) is sent whole to session 0 and
  performed there, exactly as a local wait: atomic wait-all, one winner for wait-any, auto-reset events consumed once,
  `PulseEvent` releasing only the waits present at that moment, abandoned mutexes reported as `WAIT_ABANDONED`.
  Session 0 records the waiting thread under the caller's server's lease, so mutex ownership and abandonment (the
  thread or its server ends) are kept at the owner. Timeouts and alerts (APCs) cancel the remote wait; a cancel that
  crosses a grant gives the grant back by the owner's own rules (it is still one coordinator).
- **Wait-any mixing local and global objects: a loop in ntdll, with hints.** The caller's server cannot always look
  at its local objects itself: with `/dev/ntsync`, Wine's events, mutexes and semaphores are in-process objects that
  ntdll waits on in the kernel and the server never sees (`server/inproc_sync.c`, `event.c`, `mutex.c`). So:
  - Each remote object in the caller's server has a **hint**: an internal sync object (`create_internal_sync`), set
    by the caller's server when session 0's level-triggered subscription (M0) reports the remote object ready.
  - ntdll waits on the local objects plus the hints, by whatever means it uses for locals (the kernel, or the
    server's select). Local objects are therefore only ever consumed by an ordinary, atomic wait.
  - When a hint's index wins, ntdll sends one **try-acquire** for that remote object (a zero-timeout wait at session
    0, atomic there). Success returns that index. Failure resets the hint and re-arms the subscription in the same
    message, and ntdll waits again with the remaining timeout. At most one try is in flight per wait, and it is not
    cancelled (M0).
  - Index order comes with it: the wait returns the lowest ready index, as `WaitForMultipleObjects({stop, work})`
    patterns expect.
  - The remote object's sync must never hand the hint out as the object itself (`get_inproc_sync` would): for a
    remote object it answers "not in-process", so a wait on global objects only goes to the server's select and,
    whole, to session 0.
  - Documented limits: a `PulseEvent` on a global event can be missed by a mixed wait (Microsoft itself calls
    `PulseEvent` unreliable); under contention for a global mutex, mixed waiters can be overtaken by waits made
    entirely at session 0, since they only see hints; `SignalObjectAndWait` with a remote side is not atomic (it
    signals, then waits).
- **Session 0's own server does not use `ntsync`** (`get_inproc_device_fd()` gives -1 there), so it stays a classic
  coordinator: its main loop can hold and satisfy waits on global objects, which it could not do on `ntsync`
  descriptors (they cannot be polled). User servers may use `ntsync` or not; the hint loop works either way. Debian
  13's kernel (6.12) has no `ntsync`, so nothing changes today, but a 6.14 kernel must not change behaviour silently:
  the tests run in both modes where `/dev/ntsync` exists.
- **Wait-all mixing local and global objects:** refused with `STATUS_NOT_SUPPORTED` in this design. It cannot be made
  atomic without moving the local objects to session 0 too. If real software needs it, the answer is to make those
  local objects global for that process, not a rollback scheme.
- The registry relay (`registry_relay`) is notification plumbing; M2 uses its idea (a cookie on the link turns into a
  local wake-up) only for the notify-only half of mixed waits.

## M3. Process objects across servers (P3, P4)

- **Callers in session 0 only.** B76 needs a service in session 0 to reach a session's process; nothing needs one
  user's server to act on another's. A user's server acting on a token from another user's server, including that
  server's system role, is a new trust edge and gets its own review if it is ever wanted. Until then `OpenProcess`
  from a session on another session's or session 0's process keeps failing cleanly, as since R6 step 1.
- `OpenProcess` in session 0 on an id in a session's block (the lease table says whose) is sent to that session's
  server. The owner checks the requested rights against the caller's token and the target's descriptor and issues a
  reference (M0) to the pinned process.
- **Open versus use.** The open is granted with the rights the descriptor allows, `PROCESS_VM_WRITE` and
  `PROCESS_VM_OPERATION` included (B76 asks for them at open but never uses them). The operations implemented in this
  design are the ones listed below; any other operation on a remote process fails with `STATUS_NOT_SUPPORTED`.
- **Waits and exit code:** the owner sends a death notice with the exit code to every holder when the process ends.
  The remote process object sets an internal sync, as a local process does, so waits work with or without `ntsync`.
  Once signaled, it answers `ProcessBasicInformation` itself (exit status from the notice, id and parent cached at
  open), so `GetExitCodeProcess` after the wait works without the owner.
- **Queries, class by class**, each with Windows' access rule, answered by the owner from the owner's own data:
  `ProcessBasicInformation` (with `PROCESS_QUERY_LIMITED_INFORMATION` for the limited fields as Windows does),
  `ProcessImageFileName` and `ProcessImageFileNameWin32`, `ProcessSessionInformation`, `ProcessTimes` (from the owner:
  Wine's ntdll today computes times from the querying Unix process, which is wrong for a remote one), `ProcessWow64Information`,
  `ProcessPriorityClass`. Other classes: `STATUS_NOT_SUPPORTED`, with a FIXME once per class at the owner, so one
  c2r-split lab run shows which further classes Click-to-Run uses.
- **Memory reads** (`NtReadVirtualMemory`, `PROCESS_VM_READ` in the reference): the owner reads through its own pinned
  process object, the same path a local read with a `VM_READ` handle takes (`server/process.c`). At most 64 KiB of
  data per request minus the frame header; larger reads are split. Never a Unix pid from the caller, never a
  process-memory descriptor handed out. The caller treats the bytes as data.
- **The process's token** (`OpenProcessToken`) only towards session 0: a service in session 0 opening a session
  process's token. The owner checks the token's descriptor (R7) and sends a description; session 0 builds the token
  with `create_bound_token`, so it is bound to the link's account under the existing trust rule. A user's server never
  gets a token of session 0 or of another user (`STATUS_ACCESS_DENIED`). Restrictions, integrity and elevation are kept
  as described.
- Threads of another server, handles inside a remote process (`DuplicateHandle`), and memory writes: not in this
  design.

## M4. Global sections: anonymous, exact-sized, sealed (P2)

Only **anonymous (pagefile-backed) sections** are shared across servers. Session 0 creates the backing itself for a
global section: a `memfd` of exactly the section's size, with mode 0600 for session 0's account (so another account
cannot reopen it through `/proc/self/fd`), sealed against growing and shrinking and with its seals locked
(`F_SEAL_GROW | F_SEAL_SHRINK | F_SEAL_SEAL`). For a caller granted only `SECTION_MAP_READ`, session 0 sends a
read-only descriptor opened from it; the permissions keep the receiver from reopening it writable. Execute rights:
not granted across servers (a global section mapped executable is refused). Wine's own anonymous backing today
(`memfd_create(..., MFD_EXEC)` without seals, `server/mapping.c`) is not exported as it is.

File-backed global sections (a named section over a file) are refused: a descriptor would give the whole file, beyond
the section's bounds. Image sections are never shared.

## M5. A directory of id owners: deferred

Session 0 already routes every M3 request with its own block table; nothing else needs the table now. If listings
need it later, the plan stays: session 0 writes a fixed-size read-only section with the seqlock layout of Wine's
`win32u` session memory, sized for all 4096 blocks so it never grows.

## M6. Lifetime and restarts (P6)

- A reference whose owner's epoch has ended fails every operation with `STATUS_CONNECTION_DISCONNECTED`, and a wait on
  it ends with that failure (`WAIT_FAILED`). Never `WAIT_ABANDONED`: that means the caller now owns a surviving mutex.
  Never "signaled": a lost link is not a process exit.
- **Link loss is not server death.** When a link drops and the peer's epoch is still alive (same pid, start time and
  nonce), references wait for the link to come back (the existing relink) for a bounded time, then fail as above.
- **A server's death does end its processes** (a Wine server takes its clients with it). For remote process objects
  whose owner died, the death notice cannot come; once the owner's epoch is confirmed ended (not on link loss alone),
  the holder marks them signaled, since a Wine client really dies with its server (ntdll aborts on a lost server
  socket). Their exit status is `STATUS_CONNECTION_DISCONNECTED` (a fixed non-zero code), and
  `ProcessBasicInformation` keeps working from the cached values.
- **Session 0 restart:** global objects lived in session 0, so they are gone. Holders' references fail as above, and
  a new open of the name creates a new object. Programs that keep a global handle across a session 0 restart see
  their operations fail instead of silently splitting one name into two objects. A "keeper" process that holds
  global objects across session 0 restarts was considered and is not planned: session 0 restarts are rare, and
  services restart with it.
- When a user's server ends, session 0 drops its references, ends its registered waits, and releases (as abandoned)
  global mutexes held by its threads.

## Order of work

Each step is its own branch, with tests in the system tier suites and the reviewer's review; prefixes without
session 0 keep stock behaviour.

1. **X0: M0**, the protocol: epochs, the owner-side reference table, `link_bound_token()`, asynchronous dispatch,
   caps, release and cancel, level-triggered subscriptions and the uncancellable try; session 0's server without
   `ntsync`. Tests:
   - forged and stale references are refused, and a reference cannot be used over another link;
   - a description naming another session, and an identification-level impersonation, are refused;
   - a holder server killed with live references: the owner drops them all, nothing leaks;
   - link loss while the peer's epoch lives: references survive the relink;
   - a release sent twice; a frame at the 64 KiB limit; each cap;
   - a session 0 restart.
   Before X1: the audit of Wine's own names in `\BaseNamedObjects` (see "Trust and constraints").
2. **X1: M1 and M2 for events**, global-only waits. The likely fix for the Edge installer, if its trace shows a
   `Global\` wait (to be confirmed from the other session's updater log before building). Tests: a service in session
   0 and a program in a session share `Global\` events both ways; `Local\` stays per session; access checks with a
   standard user and a denied descriptor; `PulseEvent` and auto-reset across servers.
3. **X2: mutexes and semaphores**, ownership and abandonment across servers; then mixed wait-any; mixed wait-all
   refused.
4. **X3: M3 waits and queries.** B76's lab then checks Click-to-Run in session 0 (it also needs X4).
5. **X4: M3 memory reads and the process token towards session 0.**
6. **X5: M4 sections.**

## Deferred, with the reason

- **Sharing `ntsync` objects between servers.** Objects in one wait must belong to the same `/dev/ntsync` instance,
  and each server opens its own (`server/inproc_sync.c`), so an imported object cannot join a wait. A shared device
  instance for all servers would fix that, but every holder of an `ntsync` mutex descriptor can pass any owner id to
  the kernel, which does not check it: a program could release or abandon another session's mutex. Revisit only if
  coordinator waits (M2) prove too slow, and then for events and semaphores only.
- **The id-owner directory (M5)**, until a listing needs it.
- **File-backed global sections**, waitable timers and jobs in the global namespace, mixed wait-all.

## Questions answered in review (draft 2)

1. Lowest index among ready objects matters (`{stop, work}` patterns): kept by M2's hint loop.
2. A remote process whose owner server died reports signaled, once the epoch is confirmed ended (M6).
3. B76's query classes cannot be derived statically: a FIXME per unsupported class and one lab run (M3).
4. Quotas: in-flight caps with the starting values in M0.
5. Windows rules: refused types stay local (M1); `Session\N\` names documented as not covered; `ERROR_ALREADY_EXISTS`
   and type mismatches pass through; `DuplicateHandle` above the reference's rights refused; AppContainer tested.

## Open questions

1. X1's Edge check: does Edge's updater wait on a `Global\` object when the update service is new? From the other
   session's updater log, before X1 is built.

## Review record

Draft 1 (2026-11-24) was reviewed by Codex, static and read-only, against development's Wine. Its findings and what
this draft does with them:

| Finding | Severity | Draft 2 |
|---|---|---|
| `create_bound_token` believes described groups, privileges and elevation, and accepts the system role | Critical as stated | By design under T1's trust rule (launcher-started, non-dumpable server with the system group); draft 1 wrongly said the user can attach to their server. Stated in "Trust and constraints"; new paths do not widen it |
| M3's returned pid, start time and access mask are forgeable | High | M0: owner-side reference table, bound to the link and epoch; returned masks are information only |
| M2a: an `ntsync` mutex descriptor lets its holder pass any owner id; modify rights do not cover what a descriptor allows | High | `ntsync` sharing deferred |
| M2a: objects from separate `/dev/ntsync` opens cannot be waited on together | High (blocker) | Same |
| M4: Wine's memfd backing is mode 0777, unsealed, and file-backed descriptors expose the whole file | High | M4: own exact-sized memfd, 0600, sealed; file-backed refused |
| M2b's rollback is wrong for events and semaphores too; wait-any needs one winner | High | M2: one coordinator per wait; mixed wait-any with try-acquire; mixed wait-all refused |
| M6 conflated disconnection, death and abandonment; the link serial is not an epoch | High | M6 rewritten; M0's epochs |
| No bounds on references, waits and queued work; synchronous `link_request` must not carry peer-controlled work | High (availability) | M0: quotas, asynchronous dispatch |
| Query classes need explicit support; `ProcessTimes` is computed from the caller; open versus use of VM_WRITE | Medium | M3: class list, open versus use |
| `SeCreateGlobalPrivilege` covers creating sections and symbolic links only; rebind every alias | Medium | M1 |

Draft 2 (2026-11-24) was reviewed by the Session 0 reviewer, static and read-only: **go on X0** with five conditions,
all written in.

| Finding | Draft 3 |
|---|---|
| T1 trust reading confirmed; Codex's "critical" restates T1 | Unchanged |
| The description's session id is not checked; the session must be the link's | `link_bound_token()` (Trust, M0); X0 condition 1 |
| A described token is always primary: identification-level impersonation would pass | Refused before describing (Trust, M0); X0 condition 1 |
| M3's relay between user servers is a new trust edge | M3 limited to session 0 callers; X0 condition 4 |
| Wine's own per-user names in `\BaseNamedObjects` become machine-wide | Audit before X1, per-user names move to the session directory |
| Under `ntsync` the server cannot see local sync objects; session 0 cannot coordinate on `ntsync` descriptors | Session 0 without `ntsync`; mixed waits as an ntdll hint loop (M2); X0 condition 3 |
| Mixed wait-any needs one try in flight, success winning over timeouts, level-triggered re-armed subscriptions, index order | M0 and M2; X0 condition 2 |
| `GetExitCodeProcess` after the owner's death goes through `ProcessBasicInformation` | Answered locally once signaled (M3, M6) |
| Refusing other types would regress today's per-session behaviour | They stay local, with a FIXME (M1) |
| X0 tests: killed holder, relink, double release, 64 KiB frame | X0's test list; X0 condition 5 |
| M4's memfd, permissions and seals | Confirmed as designed |
