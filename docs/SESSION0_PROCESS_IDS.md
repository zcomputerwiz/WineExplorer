<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

# Process and thread ids agreed through session 0 (R6 design note)

Status: design, 2026-11-23. This is R6 of [SESSION0_SPLIT_REVIEW.md](SESSION0_SPLIT_REVIEW.md), as the owner decided
it (decision 4): session 0 manages the ids, because it always exists and already holds what needs agreement across
the machine. Steps 1 and 2 are built (2026-11-24, `claude/r6-id-blocks`, `claude/r6-bridge-pids`).

**Step 3 waits for a consumer (2026-11-24).** After step 2, nothing in the tree needs DESCRIBE_PROCESS:

- the R9 broker in session 0 (B0) identifies its caller by the pipe's session id and token, not by the id;
- `wexelevate`'s and the token broker's id checks open the process locally, so they fail closed for a foreign id,
  as they did for 0;
- lsass's pipe is not bridged, and DWM is per session.

The only known candidate is B76 (step 4): what Click-to-Run does with App-V's id once it has opened it. Step 3 is
built when the orchestrator's B76 lab VM shows that DESCRIBE_PROCESS (or a limited foreign handle on top of it) is
enough, or when another service needs to check a caller from a session.

## The problem

Each wineserver numbers its own processes and threads (`server/process.c`, `alloc_ptid`): id = index * 4, from
index 8, in one table per server. Session 0 and every user's server therefore hand out the same small numbers, and
an id is only meaningful inside the server that gave it out. Each place where an id crosses from one server to
another has its own workaround:

- **The pipe bridge reports PID 0 for a bridged client** (`named_pipe.c`, `client_pid = 0`). A service in session
  0 cannot tell who called. A real id would be worse today: it may name another, unrelated process in session 0.
- **Some pipes are not bridged** (`named_pipe.c`, `is_lrpc_pipe_name`): `LRPC<pid>.<n>`, COM's `\pipe\OLE_<oxid>`,
  and lsass's pipe, because a name made from a process id means another process in session 0.
- **Session 0's ids are hidden from sessions** (B85): `QueryServiceStatusEx` and the merged enumeration give a
  session's programs `dwProcessId = 0` for a service in session 0.
- **Office Click-to-Run runs one service per session** (B76). App-V's VIRTMAN interface passes the client's PID to
  the Click-to-Run service (d99d2add), so the service has to live in the caller's server.

## The design

### 1. Blocks of ids, leased by session 0

- Session 0 divides the id space into blocks of 4096 indices (16384 id values). It keeps block 0 for itself and
  hands out the rest.
- A user's server gets its first block in the answer to its SESSION_ID request, which every launcher-started server
  already makes at start, before it accepts a client. When a block is nearly used up, the server asks for another
  with a new BLOCK request on its session link.
- A server allocates its own process and thread ids inside its blocks, as `alloc_ptid` does today: the table keeps
  one chunk per block, indexed from the block's base. Creating a process or a thread therefore costs no round trip
  to session 0, and a server keeps working while session 0 restarts.
- Ids stay multiples of 4 and below 2^26 (what Windows hands out); the first block starts at Wine's index 8, so
  ids 0 and 4 stay reserved. 4096 blocks fit, far more than the sessions of one machine. Threads take ids from the
  same blocks, as on Windows.
- **Session 0 records each block** with the session id, the server's Unix pid and its start time, in a file next to
  the session ids (`record_id_owner` keeps those today). After a session 0 restart, a server that links again asks
  for its blocks back, as it asks for its session id back now. Session 0 frees a block only when the server that held
  it has ended, which it checks from the recorded Unix pid and start time, so a block is never handed out twice.
- Plain prefixes, the setup account and development mode keep stock allocation: they have no session 0.

Disjoint ranges alone already make a foreign id safe. It never names a local process, so `OpenProcess` on it fails
cleanly instead of opening the wrong one.

### 2. Exact questions to the owning server

Session 0 knows which server owns any id from its block table. When a program needs facts about a process in
another server, it asks, and the owning server answers at that moment:

- **New link order DESCRIBE_PROCESS (id, start time):** the owner answers whether that process still exists, and
  gives its image path, session, user SID, integrity and start time. Session 0 sends it to a user's server over the
  session link, which already carries orders (PING).
- **The other direction:** a user's server asks session 0 about one of session 0's processes with a request on its
  link.
- **For programs:** a new server request that a program uses to ask about a foreign id. It waits asynchronously for
  the answer, like other waits, because the answer comes from another server.
- **Id plus start time is the key.** A process that exited and whose id was reused can never be taken for the old
  one. A caller that checks "is my client still process X" passes both.

This is the path for anything that decides access: a service checking its caller, or Click-to-Run checking App-V's
client. Windows works the same way: listings are snapshots, and access is decided against the live process.

Not in scope: a handle to a process in another server (`OpenProcess` for more than these facts). If something needs
that, it is option (b) of the review (references naming the owning server), kept in reserve.

### 3. What then goes away, step by step

1. **The bridge reports the real client id** instead of 0. Services that only need identity (who, which session,
   which user) use DESCRIBE_PROCESS. Callers that test for PID 0 as "bridged" are found and changed first (the DWM
   and token brokers refuse 0, which stays right for a pipe that is never bridged).
2. **B85's zeroing is removed:** a session 0 service's id is unique, so a session may see it.
3. **`LRPC<pid>` and `OLE_<oxid>` pipes can be bridged** where that is wanted, since the name no longer collides.
   Each is decided separately with the ALPC and COM owners. lsass stays local: it reads its callers' memory.
4. **B76: Click-to-Run back to one service in session 0.** Ask the orchestrator before changing ClickToRunSvc's
   placement: the B76 lab VM (c2r-split) depends on it.
   - What is known (the orchestrator, 2026-11-23): before the split, the service answered App-V's d99d2add call with
     E_INVALIDARG when the PID belonged to another server's process. The error went away once the service ran in the
     client's server. So it validates the PID against a real process, at least by opening it.
   - Open: whether it also reads the token or image, or uses the handle for more. The orchestrator's B76 lab VM
     answers that black-box when R6 reaches this step.
   - If it only opens and queries, a limited handle to a foreign process is enough. The owning server would answer
     `PROCESS_QUERY_LIMITED_INFORMATION` queries through DESCRIBE_PROCESS. Anything more needs option (b), and the
     split stays.

### 4. A list of every session's processes: on demand, not pushed

On Windows, Task Manager and `NtQuerySystemInformation(SystemProcessInformation)` list every session's processes,
with limited details about other users'.

- **Here, on demand:** when a program asks for the whole machine's list, its server asks session 0. Session 0 asks
  each linked server for its current list and merges them. It answers with what the caller may see: a standard user
  gets limited details about other users' processes, as on Windows.
- **This step is optional and comes last.** Programs listing processes today see their own server's processes,
  which keeps working.

**Not built: a list pushed to session 0.** Each server could send process start and exit events to session 0, rate
limited and batched, so session 0 always has a nearly current list. Nothing needs that today:

- access checks must use the exact path above;
- listings are rare enough to ask for;
- a pushed list is another copy to keep right after restarts and lost events.

If a measured need appears (listings that must be fast, or a component that reads the list often), the rules for it
are:

- events for process start and exit only (id, start time, image, session, user), batched every 100 to 250 ms;
- exits never dropped;
- sequence numbers per server, with a full snapshot after a gap or a re-link;
- used for display only, never to decide access.

## Order of work

1. Blocks: the SESSION_ID answer with the first block, the BLOCK request, the per-block id table, the block record
   and its restart rules, and tests:
   - ids of two sessions and session 0 never overlap;
   - a server keeps its blocks across a session 0 restart;
   - a block is freed only after its server has ended.

   Protocol version 6.
2. The bridge's real client id, and B85's zeroing removed.
3. DESCRIBE_PROCESS and the program-side request, with tests from a session 0 service against a client in a session.
4. Case by case: bridging `LRPC<pid>` pipes, B76's placement (with the orchestrator), the machine-wide list (if
   wanted).
