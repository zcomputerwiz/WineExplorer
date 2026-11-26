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
- **Wine's own named objects.** Making `\BaseNamedObjects` machine-wide also makes machine-wide the objects Wine
  creates there directly, per server today (which under WineExplorer means per user): `WineWaylandZones` (`dlls/winewayland.drv/wayland_zones.c`),
  `WineAppContainerRegistrationLock` (`dlls/kernelbase/security.c`) and `__WINE_FONT_MUTEX__`
  (`dlls/win32u/font.c`), all mutexes. One user could then deny or hold another user's. It also makes reachable, as
  `Global\Name`, every name Wine creates unprefixed in a process of session 0, since session 0's local namespace is
  `\BaseNamedObjects`. The audit ("Audit: Wine's own names in `\BaseNamedObjects`" below) lists both kinds and
  proposes where each goes (small stock changes, listed in `STOCK_CHANGES.md`). userenv's `Global\` machine policy
  objects (`dlls/userenv/gpolicy.c`) are rightly global, and are X1's in-tree test with the GPO lab.
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
- **Only `\BaseNamedObjects` is forwarded.** Every other directory (`\Sessions\N\...` with `WineObjects`,
  `\KernelObjects`, `\NLS`, `\KnownDlls*`, `\Device`, `\??`, window stations) is never forwarded and stays each
  server's own. Wine's internal locks live in `\Sessions\<session>\WineObjects` for that reason
  (`wine/internal_lock.h`; see the audit below).
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
2. **The moves of Wine's internal locks** to `\Sessions\<session>\WineObjects` (the audit below; Wine
   `claude/internal-locks-kernelobjects`), with their Wine tests. Independent of M1.
3. **X3: M3 waits and queries.** B76's `c2r-split` lab then checks Click-to-Run in session 0 (it also needs X4).
4. **X4: M3 memory reads and the process token towards session 0.** B108 (Edge run by the user) is retested after it.
5. **X1: M1 and M2 for events**, global-only waits. **Gate: step 2 has landed** (the moves land before or with M1,
   never after). Not the Edge installer's fix (open question 1 is answered: B108 is process visibility, not a
   `Global\` wait). Tests: a service in session 0 and a program in a session share `Global\` events both ways;
   `Local\` stays per session; access checks with a standard user and a denied descriptor; `PulseEvent` and auto-reset
   across servers; session 0's policy writer creates userenv's five `Global\` objects before any session links (a
   session that created one first, with its own descriptor, is refused or the object recreated: the test fails
   otherwise).
6. **X2: mutexes and semaphores**, ownership and abandonment across servers; then mixed wait-any; mixed wait-all
   refused. userenv's machine policy mutex is its in-tree test.
7. **X5: M4 sections.**

The order after X0 was decided on 2026-11-26 by the reviewer and the orchestrator ("Order: a proposal" below): X3 and
X4 have a failing case (B76), X1 has none today.

## Audit: Wine's own names in `\BaseNamedObjects` (before X1)

2026-11-26, against Wine development as pinned by master c0ae4ba8, static (named creates and opens in `dlls/`,
`programs/`, `server/`; WineExplorer's modules outside Wine create none). For the reviewer before X1 code.

**Two ways a Wine name lands in what M1 makes machine-wide:**

- **An absolute `\BaseNamedObjects\...` path** (table A). Today every server has its own root directory, so these are
  per server, which under WineExplorer means per user. After M1 they would be one object for the whole machine.
- **Any name created in a process of session 0's server** (table C). Session 0's programs are in session 0
  (`server/session0.c`, T5.3), and for session 0 `\Sessions\0\BaseNamedObjects` and `Local\` both resolve to
  `\BaseNamedObjects` (`server/directory.c`, `create_session`), as on Windows. So once M1 forwards `Global\` from
  the sessions, every unprefixed, `Local\` or `\Sessions\%u\` name a session 0 process creates can be opened, created
  first or held from a session as `Global\Name`. Windows has the same exposure for its services; Wine's own
  once-per-server locks were written assuming nobody else can reach them.

Not affected: `\KernelObjects\...` (`__wineboot_event`, `__wine_user_shared_data`, `__wine_session`,
`Low/HighMemoryCondition`), `\NLS`, `\KnownDlls*`, `\Device`, `\??`, window stations: M1 forwards only
`\BaseNamedObjects`. Names that callers pass through the Win32 and NT APIs are the program's business.

**A. Absolute `\BaseNamedObjects\` names, created by every user's processes**

| Name | Type | Where | Guards | Proposal |
|---|---|---|---|---|
| `WineAppContainerRegistrationLock` | mutex | `dlls/kernelbase/security.c`, `ac_lock_acquire` | the user's AppContainer mappings in HKCU | private (rule below) |
| `__WINE_FONT_MUTEX__` | mutex | `dlls/win32u/font.c`, `font_init` | the volatile `HKCU\Software\Wine\Fonts\Cache` (the first process fills it); taken with an infinite wait at every GUI process's start | private |
| `WineWaylandZones` | mutex | `dlls/winewayland.drv/wayland_zones.c` | `HKCU\Software\Wine\Wayland` zone values, per Wayland socket | private |

All three guard per-user state. Moving them to `\Sessions\%u\BaseNamedObjects` (the PEB's session, as
`display_device_init` does) fixes the sessions but not session 0, where that path is `\BaseNamedObjects` again (table
C), hence the rule below.

**B. `Global\` names: rightly machine-wide**

| Name | Type | Where | Notes |
|---|---|---|---|
| `Global\userenv: Machine Group Policy has been applied` | event | `dlls/userenv/gpolicy.c` | descriptor `D:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GXRC;;;WD)` for all five |
| `Global\userenv: machine policy mutex` | mutex | `EnterCriticalPolicySection(TRUE)` | Everyone has `SYNCHRONIZE`, so any user can hold it and stall machine policy (the same grant as Windows' and ReactOS'); a mutex, so X2 |
| `Global\userenv: machine policy refresh event`, `... force refresh event` | event | `RefreshPolicy(Ex)` opens them | nothing in Wine or WineExplorer creates them, so `RefreshPolicy` is a no-op in every server today |
| `Global\userenv: Machine Policy Foreground Done Event` | event | opened with `SYNCHRONIZE` | |

Keep. A session that creates one of them before session 0 does owns it with its own descriptor, so session 0's
policy writer must create them at its start, before sessions link (an X1 test that fails if a session got there
first). WineExplorer's policy notification
does not depend on them: it crosses servers through the registry serial (`gp_watch`, the machine registry is session
0's).

**C. Names that become machine-wide when the creator runs in session 0**

| Name | Type | Where | Runs in session 0 | If a session holds or pre-creates it | Proposal |
|---|---|---|---|---|---|
| `__wine_SvcctlInstance` | mutex | `programs/services/services.c` | yes, `services.exe` | a services.exe restarted while session 0's server lives exits as "already running" | private |
| `display_device_init` (`\Sessions\%u\...`) | mutex | `dlls/win32u/sysparams.c` | every GUI process | display initialisation of session 0's GUI processes stalls | private |
| table A's three | mutex | | font: every GUI process | infinite waits at start (font), AppContainer registration fails | private |
| `__WINE_SXS_CACHE_MUTEX__`, `__WINE_FUSION_CACHE_MUTEX__` | mutex | `dlls/sxs/cache.c`, `dlls/fusion/asmcache.c` | installers (msi in session 0) | machine installs that touch the assembly caches stall | private |
| `__WINE_WINSPOOL_MUTEX__` | mutex, created owned | `dlls/winspool.drv/info.c` | services that print or list printers | printer initialisation stalls | private |
| `crypt32_root_semaphore` | semaphore, count 0 | `dlls/crypt32/rootstore.c` | anything that checks a certificate (msi, wexappx, TLS) | a session that creates it first and never releases it stalls every certificate check in session 0 (infinite wait); without M1 too, a first creator that dies before it releases hangs every later one | private, and a mutex (abandoned, so the waiters wake) |
| `PowerProfileRegistrySemaphore` | semaphore | `dlls/powrprof/powrprof.c` | rare | power profile reads stall | private |
| `__wine_dinput_reg_mutex` | mutex | `dlls/dinput/joystick_hid.c` | unlikely | | private |
| `winemenubuilder_semaphore` | semaphore | `programs/winemenubuilder` | when an install in session 0 makes shortcuts | | private |
| `__wine_clipboard_<winstation>`, `__wine_display_settings_restorer_mutex` | mutex, held | `programs/explorer/desktop.c` | if explorer runs there | session 0's clipboard or display restorer never starts | private |
| `DBWinMutex`, `DBWIN_BUFFER` (section), `DBWIN_BUFFER_READY`, `DBWIN_DATA_READY` | | `dlls/kernelbase/debug.c`, `dlls/kernel32/debugger.c` | every `OutputDebugString` | a session's debug monitor sees session 0's debug output, as on Windows (the Windows 11 host has `DBWinMutex` in `\BaseNamedObjects`); creating the section from a session needs `SeCreateGlobalPrivilege` (M1). A session can create or hold `Global\DBWinMutex` (a mutex needs no privilege), and `OutputDebugString` waits on it without a timeout (`kernelbase/debug.c`), but only once `DBWIN_BUFFER` and both events exist, so only while a session 0 debug monitor runs, as on Windows | keep |
| wininet's cache mutexes and `index.dat` mappings, mshtml's storage mutex | | `dlls/wininet/urlcache.c`, `dlls/mshtml/htmlstorage.c` | services using wininet | names come from the profile path (SYSTEM's or a service account's); a held mutex stalls that cache; the mappings are sections, so `SeCreateGlobalPrivilege` again | keep (Windows' exposure) |
| D3DKMT shared names (`\Sessions\%u\...`), DWM's `Local\__wex_dwm_*` | | `dlls/win32u/vulkan.c`, `dlls/d3d11`, `dlls/dxgi`, `include/wine/dwm_protocol.h` | only if graphics or the compositor run there | shared between the session's own processes by design | keep |

**The rule for "private" (decided 2026-11-26, the reviewer's choice):** Wine's internal locks are created in
`\Sessions\<the PEB's session>\WineObjects`, a directory `create_session` makes in every session next to
`BaseNamedObjects` (NULL descriptor, like its siblings; for session 0 too, where it is a real directory and not
linked into `\BaseNamedObjects`), and that M1 never forwards. Each lock keeps the scope it had: the unprefixed names
and `display_device_init` were per session; table A's three were per server, the same while a server holds one user
session (`create_session` runs for 0 and `default_session_id` only, `server/directory.c`). `\KernelObjects` was
considered and not taken: user processes can create there only because Wine gives it a NULL descriptor (users cannot
on Windows), and it is per server, which would merge per-session locks in a server with two user sessions. Every site
goes through one definition, `include/wine/internal_lock.h` (a format for the Unix side, helpers for PE code); about
thirteen call sites, each listed in `STOCK_CHANGES.md`.

AppContainer processes: Wine has no `AppContainerNamedObjects` redirect today, so an AppContainer process reaches
these locks as it reached them in `BaseNamedObjects` (no change). When a redirect comes, `WineObjects` is denied to
AppContainers with the rest of the session's namespace. Locks that guard machine state (assembly caches, printers, the root certificate
import) are per server today too, so a session and session 0 do not exclude each other there; making them global
would need session 0 to create them first and is not proposed now.

The moves are independent of M1 and land before X1 or with it, never after (X1's gate in "Order of work").

**Rejected:** forwarding all of `\BaseNamedObjects` except names that start with `__wine` (guesswork, and misses
`display_device_init` and the others); giving session 0 a local namespace apart from `\BaseNamedObjects` (breaks
services that create `Name` unprefixed for clients that open `Global\Name`, a common pattern on Windows).

## Order: a proposal

Agreed by the reviewer and the orchestrator on 2026-11-26 and written into "Order of work" above; the reasons:

- **X1 has no failing case today.** WineExplorer's policy notification already crosses servers (registry serial),
  nothing creates the refresh events, and the one userenv object that is split per server today is a mutex (X2). P1
  stays a known class (installers with update services) without a current case; the GPO objects remain X1's and X2's
  in-tree tests.
- **X3 and X4 have B76** (Click-to-Run's service in session 0 waiting on and reading Word in the session), with its
  `c2r-split` lab. Whether they also fix B108's user-run Edge is not known: the remaining difference there is identity
  (the chain runs as the elevated user, where Windows runs it as SYSTEM), and Omaha's `CanUninstallGoogleUpdate` may
  need the identity, not only visibility.
- **B108 has a candidate outside this design:** run msi's deferred custom actions without impersonation as SYSTEM in
  session 0, the way B108 measured to work (the components' way). That is an msi and system tier item, written up in
  `MSI_NOIMPERSONATE_SYSTEM.md`.
- **Proposal:** the audit's moves now; then X3 and X4 (B76's lab, B108 retested after X4); then X1 and X2 with the
  GPO objects as their tests; X5 last. Keeping X1 first costs nothing in design (M1 and M3 are independent) but
  delays the step with a failing case.
- Evidence from the Windows 11 host (black box, 2026-11-26): Edge Update was not running; its global directory held
  no Edge Update object, and one event named after Edge's install path
  (`!device!harddiskvolume4!program files (x86)!microsoft!edge!application`, creator not identified). Nothing there
  shows a `Global\` wait between Omaha's service and an install.

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

None open. Answered since draft 3:

1. **X1's Edge check: answered by B108 (`KNOWN_BUGS.md`): process visibility, not a `Global\` wait.** Omaha's
   session 0 service (`/svc`, then `/ua /installsource core`) finds no registered app, does not see the install that
   runs in the user's server, and uninstalls itself with the running `setup.exe`. On Windows 11 the MSI's whole
   `DoInstall` chain runs as SYSTEM (in the user's session) and the service as SYSTEM in session 0: one identity, one
   kernel. With `ProcessCommandLineInformation` implemented (Wine 0ec8719014e), the MSI installs when the whole chain
   runs as SYSTEM in session 0 (the components' way); run by the elevated user it still fails ("CanUninstallGoogleUpdate
   returned 1"). Edge's case therefore concerns M3 and M4 (X3, X4), or msi's identity rule, not M1.

## Appendix: X0 wire protocol and how its tests reach it

Written by the X0 session (2026-11-24) before the code, for the reviewer. Names are those of
`include/wine/wex_session0.h` and `server/session0.c`.

### Frames on the session link

The session link (a SEQPACKET connection, T6.2) carries two kinds of message from version 8 on (`WEX_SESSION0_VERSION`
8; a server of another version is refused at `SESSION_ID`, as today):

- session 0's orders and their answers (`struct wex_session0_order`, `_order_reply`, 16 bytes, magic `WXS0`),
  unchanged;
- **X frames**, in both directions: a 32-byte header (`struct wex_x_header`: magic `WX0X`, `size` of the whole frame
  with the header, `type`, `call`, `status`, `desc_size`, a 64-bit `ref`), then a body by type, then for `OPEN` the
  caller's token description and the name. `size` is at most `WEX_SESSION0_MAX_MSG` (65536) and equals the number of
  bytes received; a frame that is cut (`MSG_TRUNC`), carries a descriptor, or whose sizes do not add up is answered
  `STATUS_INVALID_PARAMETER` when its header could be read (the link stays: the peer is a trusted server; it is
  logged), and ignored otherwise.

A request carries a `call` number chosen by its sender; the reply has the same `call` and `type | WEX_X_REPLY`.
Replies come in any order (asynchronous dispatch): the sender keeps a table of calls in flight, each with a
completion. Nothing on the link uses the synchronous `link_request` (2 s), which stays for session 0's short requests
on their own connections. All sends are non-blocking; what the socket does not take is queued per link and sent on
`POLLOUT`.

| Type | Direction | Body | Reply |
|---|---|---|---|
| `HELLO` | each side once, session 0 first | epoch, connection number, session | none |
| `OPEN` | holder to owner | kind, desired access, name size; description; name | `ref`, granted access |
| `QUERY` | holder to owner | (`ref`) | granted access, ready |
| `RELEASE` | holder to owner | (`ref`) | always `STATUS_SUCCESS` (idempotent) |
| `SUBSCRIBE` | holder to owner | (`ref`) | success; arms the subscription |
| `CANCEL` | holder to owner | (`ref`) | always success (idempotent): ends the subscription |
| `TRY` | holder to owner | (`ref`) | success (acquired) or `STATUS_TIMEOUT`; re-arms the subscription in the same message |
| `NOTICE` | owner to holder | (`ref`) | none |
| `PING` | either | up to the frame limit | the same bytes |

`OPEN` names a **kind**; X0 defines none for production (every kind is `STATUS_NOT_SUPPORTED`), X1 adds global
names, X3 process ids. The lab build has one kind, a lab object (below). `QUERY`, `RELEASE`, `SUBSCRIBE`, `CANCEL`,
`TRY` and `NOTICE` are generic over a small interface an object kind implements (`ready`, `try_acquire`, and a call
to `x_object_changed` when it may have become ready).

### Epochs and links

- A server's **epoch** is its Unix pid, its start time (`/proc/<pid>/stat`, as `id_block_owner_alive`) and a 64-bit
  nonce from `getrandom` at start. Each side sends its epoch in `HELLO`; session 0 also numbers each connection.
- The owner keeps a **peer** per epoch: account (uid), session, the current connection (or none while lost), its
  references, subscriptions, calls in flight and queued bytes. A reference is bound to the peer (the epoch), not to the
  connection: the same epoch linking again (the existing relink, `SESSION_ID` with its own id) gets its peer back
  with every reference. Another server is another peer, even of the same account and session.
- **Identity comes from the kernel.** A `HELLO` is accepted only when its pid is the connected process's
  (`SO_PEERCRED`) and its start time is that process's (`/proc`); the nonce only tells a restarted server from its
  predecessor. A `HELLO` that names another pid is refused, logged, and its connection gets no peer. (Liveness while a
  link is lost is a `/proc` check each second; a pidfd from `SO_PEERPIDFD` would avoid the timer and the reuse window,
  and is left for later.)
- **Link loss** (end of file, error) while the peer's process is alive (pid, start time, not a zombie): the peer is
  kept for at most 120 s; the owner checks the process each second.
- **Recovery on the relink** (review of this appendix, MUST FIX 1): a SEQPACKET socket drops what its closing end had
  not read, so replies, notices and requests can vanish with a link. Calls therefore belong to the peer, not to the
  connection: while the epoch lives they stay pending (a new call during the loss fails at once with
  `STATUS_CONNECTION_DISCONNECTED`), and on the relink each is sent again with its call number. `RELEASE`, `CANCEL`,
  `QUERY`, `SUBSCRIBE` and `PING` are idempotent. For `TRY` and `OPEN` the owner keeps each success in a cache per peer
  and replays it to a request that comes again, so nothing is acquired or opened twice. Each request carries the
  holder's low-water mark (its lowest pending call, in the header's `status`), and the owner drops cached replies
  below it (at most 1024 per peer; the oldest counts as delivered). A `TRY` success still cached when the peer's
  epoch ends is given back by the object's rules. On the relink the owner also arms every subscription of the peer
  again, so a notice lost in the socket comes again.
- **A relink after the other side dropped the peer** (its 120 s ran out while the epoch lived; second review, 1b):
  each `HELLO` carries the nonce of the epoch the sender still keeps for the connecting server (`peer_nonce`, 0 for
  none). When it is not the receiver's own nonce, the sender started fresh, and the receiver ends its side of that
  peer as well: what it holds there is dead, its pending calls fail with `STATUS_CONNECTION_DISCONNECTED`, the
  references it issued are dropped, and a new peer starts. So no waiter keeps believing in a subscription the owner has
  forgotten. Test: the link held down past a shortened bound (lab `lostbound`, `droplink hold`, `relink`).
- Call numbers are compared with serial arithmetic (`(int)(a - b) < 0`), so they work when 32 bits wrap.
- **Before X1 (second review, 1a):** a low-water mark is pinned by the oldest pending call. In X0 every call is
  answered at once, but X1's global waits can stay pending for hours, and the owner would keep every later cached
  success meanwhile. X1 therefore acknowledges explicitly: long calls (waits) are left out of the mark, and the holder
  acknowledges the cached replies it received, batched in the next request (a list after the body) or in an `ACK`
  frame when it has none to send. The cache's bound (1024 per peer) stays as the backstop.
- **Epoch ended** (the process is gone, a zombie, or another start time; a `HELLO` with another epoch; or the 120 s
  ran out): the owner drops every reference and subscription of that peer (releasing the pinned objects); the holder
  marks every reference into that epoch dead: each later operation fails at once with
  `STATUS_CONNECTION_DISCONNECTED`, and a raw use of the old id at the new epoch finds nothing.
- **A user's server has one peer, session 0.** It accepts X frames only on its session link, which it opened itself
  to the configured socket after checking session 0's account (`connect_session0`). Session 0 never forwards a frame
  from one link to another: no user server to user server relay (condition 4). A user's server as owner believes
  session 0 for any account, but only for session 0 callers: a description whose session is not 0 is refused there.

### References

- An owner-side entry: a random 64-bit id (never 0, unique among live entries, never reused within an epoch), the
  peer, the pinned object, the granted rights, the caller's token built by `link_bound_token()` (kept for re-checks
  and the audit), the account. Every operation looks the id up **in the peer's own table**: an id of another peer,
  a released id, a forged id and an id of an earlier epoch all answer `STATUS_INVALID_HANDLE` (one status, so the
  answer says nothing about other peers' ids).
- Rights: an operation needs its right in the entry (`QUERY`: none beyond the reference; `SUBSCRIBE`, `TRY`:
  `SYNCHRONIZE`). The granted mask in the reply is information only.
- **Counting is the holder's**: every `OPEN` gives a new entry; the holder counts its own handles to it (duplicates
  share the id) and sends one `RELEASE` when its last handle closes. `RELEASE` and `CANCEL` of an id the peer does not
  hold are no-ops that answer success, so a release sent twice (or crossing a link loss) never drops another entry.
  A subscription ends with its reference.
- Released entries are freed at once; a `TRY` in flight on an entry being released still gets its answer.

### `link_bound_token()` (condition 1)

The only function that turns a description received in an X frame into a token: `token_create_described`, then the
binding of `described_token_bound` for the peer's uid (session 0 as owner; a user's server as owner binds session 0's
descriptions to session 0's account rule: any account), then the description's `session_id` must be the peer's
session (session 0 as owner: the session of the peer's link; a user's server as owner: 0). Refused:
`STATUS_ACCESS_DENIED`; malformed: `STATUS_INVALID_PARAMETER`. On the caller's side, `x_describe_caller()` takes the
thread's effective token (its impersonation token, else the process's) and refuses an impersonation token below
`SecurityImpersonation` with `STATUS_BAD_IMPERSONATION_LEVEL` before describing anything.

### Subscriptions and tries (condition 2)

- **Level-triggered with one notice outstanding.** A subscription is armed by `SUBSCRIBE` and by every `TRY`. An
  armed subscription whose object is ready gets one `NOTICE` and is disarmed: at once if the object is ready when it
  is armed, else at the next `x_object_changed` that finds it ready. The holder keeps the notice as a set hint until
  it tries; so a readiness that comes between a notice and the next request is never lost, and the number of queued
  notices is bounded by the subscriptions.
- **A failed try re-arms in the same message**: the owner answers `STATUS_TIMEOUT` and arms the subscription in one
  step of its loop, so an object that becomes ready right after the failed try gets a new notice.
- **A try is not cancellable by the holder.** There is no frame to cancel a `TRY`; the holder keeps its call until
  the answer comes, across a relink of the same epoch, and a success is reported as acquired even when the holder's
  wait timed out or an APC came meanwhile (X2 makes the wait report it). The owner keeps the success until the holder
  acknowledges it (low-water mark) and replays it to the repeated request after a relink; when the peer's epoch ends
  first, it gives the acquisition back by the object's own rules (for the lab object: the count is restored).
- **Never answered:** a reply, a `NOTICE`, a `HELLO`, malformed or not, and any frame before the `HELLO` (two servers
  would otherwise bounce invalid frames for ever). They are counted (`stray` in the lab's view) and dropped. An
  unknown request type gets `STATUS_NOT_SUPPORTED`. A frame with a descriptor is refused in one place (`bad` in
  `x_conn_frame`), which X5 will relax for sections.

### Caps (in flight, per account)

| What | Cap | Counted at | Over the cap |
|---|---|---|---|
| References | 16384 | the owner, per account of the holder (session 0: the peer's uid over all its servers) | `OPEN` answers `STATUS_INSUFFICIENT_RESOURCES` |
| Subscriptions and waits | 4096 | the owner, per account | `SUBSCRIBE` answers `STATUS_INSUFFICIENT_RESOURCES` |
| Requests in flight | 256 per link | both: the holder before sending, the owner for requests it has not answered yet | the holder fails the call locally, the owner answers `STATUS_INSUFFICIENT_RESOURCES` |
| Queued bytes | 4 MiB per account | both, bytes waiting for the socket | the holder fails new calls locally; the owner keeps reading the link but serves none of that peer's requests: they wait, counted in flight (so at most 256), until its queue is under half. Nothing is dropped. A peer whose queue at the owner passes 8 MiB anyway loses its link (a link loss: its references wait for the relink) |

Each owner logs an account once when a count reaches half of its cap (`x0: uid U at half of the C cap`), and again
after it fell under a quarter. (Draft of this appendix: the owner stopped reading the link over the queued cap. The
code keeps reading instead, because session 0 and a user's server can each own objects of the other: two owners that
both stopped reading would wait on each other forever.)

### Session 0 without `ntsync` (condition 3)

`get_inproc_device_fd()` returns -1 when `session0_server`, checked at each call (not only cached), so session 0's
events, mutexes, semaphores and internal syncs are always the server's own objects.

### How the tests reach the protocol: lab hooks

A user's server links only when the launcher started it (`peer_trusted`), so a harness cannot pose as one, and no
production kind exists in X0. The smallest real consumer (M3's death notice for a service in session 0) needs X3's
routing of `OpenProcess`, and could only show the paths a well-behaved server takes, not forged frames. So X0's tests
use **lab hooks** in the existing pattern:

- `server/wex_x0lab.c`, compiled only with `-DWEX_X0_LAB_TEST` (added to `SERVER_FLAGS` and `SERVER_OBJECTS` in
  `scripts/lab/r8-lab-build.sh`); `wex_e1lab.h` declares its hooks, each listed in `STOCK_CHANGES.md`. A normal build
  contains none of it.
- Every lab server (session 0 and each user's server) listens on `wex-x0-lab` in its server directory (its working
  directory, reached by root as `/proc/<pid>/cwd/wex-x0-lab`), mode 0600, and serves only a peer of uid 0
  (`SO_PEERCRED`). One text command per message, one text answer.
- The lab object kind (`OPEN` kind `WEX_X_KIND_LAB`, only in lab builds): a named counter in the owner; `ready` when
  the count is above 0, `try_acquire` takes one.
- Commands: `epoch`, `peers` (owner's view: per peer, epoch, account, link state, references, subscriptions, calls
  in flight, queued bytes), `set NAME COUNT` (owner), `open NAME [ACCESS] [desc=self|session=N|level=ident|anon]`
  (holder; a lab token: the first interactive token of the server's session, SYSTEM in session 0, duplicated at the
  level asked, the description's session id patched for `session=N`), `query|release|sub|cancel|try ID|all [raw]`
  (`raw`: sent as given, past the holder's own table), `ping [n=N] [size=BYTES] [raw]`, `nowait` on any of these
  (answers once sent; `batch N` reports later), `defer on|off` (owner: holds `PING` answers, to fill the owner's
  in-flight count), `pause|resume` (stops reading the link to a peer, to fill the other side's queue), `droplink` (a
  user's server shuts its session link down; the existing relink brings it back), `notices ID` (holder). On session 0
  the holder commands take `peer=S` to choose the peer of session S.
- `scripts/test_sysrun_server.sh` gets a lab section `s0_x0` (suite `l`, group `lab`, needs `s0_base`) with one
  check per test of X0 (condition 5) and a small Python client for the control sockets (`scripts/x0lab.py`). The
  targeted run already sends `wine:server/*` to the normal, T1 and lab suites (`scripts/lab/test-map.txt`).

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

The X0 appendix (2026-11-24, 07595238) was reviewed by the Session 0 reviewer, static: **go for the code**, with these
fixes, all in the X0 branch:

| Finding | X0 |
|---|---|
| A relink loses what was in the socket: a `TRY` success, a `NOTICE`, a `RELEASE`/`CANCEL` | Calls belong to the peer and are sent again on the relink; the owner caches `TRY`/`OPEN` successes until the holder's low-water mark passes them and replays them; every subscription is armed again ("Recovery on the relink"). Tests: droplink with each in flight |
| Back-pressure by not reading deadlocks two owners; 256 x 64 KiB does not fit 4 MiB | The link is always read; over the queued cap requests wait (at most 256), beyond that 32-byte refusals, a cut at 8 MiB. Replies are served only under the cap, so the queue stays near 4 MiB. Test: both directions busy at once |
| Never answer replies, `NOTICE`, `HELLO`; drop frames before `HELLO` | Done ("Never answered"); test with a stray reply, notice and cut `HELLO` |
| Peer identity from the kernel | Already so (pid = `SO_PEERCRED`, start time from `/proc`); test with a forged `HELLO` pid. Liveness by pidfd: later |
| Lab code out of shipped builds; the socket in a user-owned directory | `installer/package-wine.sh` refuses a staged wineserver with the X0 or E1 lab markers; `scripts/x0lab.py` checks that the socket belongs to the server's account |
| Reference ids by counter | Kept random: a counter restarting at 1 in a new session 0 would give a stale id to a new reference of the same peer |
| The user's server reads its link into 17 bytes | Already the 64 KiB `link_receive` path, with cut and descriptor checks |
| Descriptors on X frames, for X5 | Refused in one place (`bad` in `x_conn_frame`) |

Second round (the reviewer on X0's answers): accepted #2, #3, #4, #6, #7; #1's design right, with two holes. 1b, a
relink after the owner dropped the peer, is fixed in X0 (`peer_nonce` in `HELLO`, test
`x0_relink_after_the_owner_dropped_the_peer_starts_fresh`). 1a, the low-water mark pinned by long calls, is written
down for X1 (explicit acknowledgements; serial arithmetic already).
