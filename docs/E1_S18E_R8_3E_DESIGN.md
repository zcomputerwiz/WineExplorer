# E1-S18e R8 Step 3e Design: Caller-Binding Authority

## 1. Retained Objects and Opaque Identifiers
The server retains authoritative kernel/server object references across the elevation lifecycle; opaque 128-bit IDs are unique within their generation (docs/E1_S1_CONTRACTS.md:25):
* **`prefix_id`**: Stands for the Wine prefix filesystem directory anchor (`config_dir_fd`, `wine/server/request.c:102,720`). Server generates random 128-bit ID `e1s_prefix_id` (`wine/server/registry.c:171,2862`) on server instance init and retains it in server global state.
* **`host_process_id`**: Stands for caller's native host process pinned identity, bound to `client_process->peer_pid` (`wine/server/process.h:100`). Server keeps it on `struct process` (`wine/server/process.h:61`).
* **`opener_id`**: Stands for retained opener process object `client->client_process` (`wine/server/named_pipe.c:77,1519`). Server retains object reference in `struct pipe_end`.
* **`writer_id`**: Stands for retained consumed-writer process object `server->consumed_writer.process` (`wine/server/named_pipe.c:82,963`). Server retains object reference in `struct pipe_end`.
* **`desktop_route`**: Stands for caller's desktop object `struct desktop *` (`wine/server/user.h:66`). Server must retain `client->client_desktop = grab_object(get_thread_desktop_quiet(current))` at pipe connect (`wine/server/named_pipe.c:1519`) and `writer->desktop` on write. Route ID is assigned and stored in `struct pipe_end` / `struct desktop`.

## 2. Generations: Ownership, Increments, and Broker Learning
Generations are authoritative system snapshots; the broker queries them from trusted endpoints, never from client assertions:
* **`server_gen` (u64)**: Owned by wineserver. Initialized from `/dev/urandom` on server boot / first endorse (`wine/server/sysrun.c:543-547`, `wine/server/registry.c:2860-2866`). Changes only on server restart. Broker learns it directly from wineserver reply.
* **`root_gen` (u64)**: Owned by machine-root authority. Initialized at service startup (`tools/e1s6/e1s6-lab.py:225`) [*unverified in code: dynamic root config reload increment*]. Broker learns it over authenticated S6 control channel (`src/dlls/wexhost/e1s6.c:465`).
* **`broker_gen` (u64)**: Owned by wineserver tracking broker process. Assigned fresh random u64 when broker registers (`wine/server/sysrun.c:552-553`). Broker learns it from server S6 endorsement reply (`wine/server/sysrun.c:568`, `src/dlls/wexhost/e1s6.c:468`).
* **`verifier_gen` (u64)**: Owned by native PAM verifier service. Initialized at verifier boot (`tools/e1s6/e1s6-lab.py:225`). Broker learns it via S6 registration (`src/dlls/wexhost/e1s6.c:466`).
* **`map_gen` (u64)**: Owned by S4 accounts map authority (`tools/wex-accounts/wex-accounts.c:344`). Incremented whenever `accounts.map` changes. Server parses `map->generation` via `parse_map` (`wine/server/accounts.c:344,566`). Broker learns it from server binding reply.
* **`policy_gen` (u64)**: Owned by machine-root admin policy authority (`/etc/wineexplorer/sysrun.conf`). [*unverified in code: dynamic increment not implemented; static 1 in tools/e1s6/e1s6-lab.py:471 and src/programs/wexelevate/e1s18_broker.c:1553*]. Broker learns it from server binding reply.

## 3. Object-Level Comparisons (Never IDs Alone)
Endorsement (3d) and rechecks at attach, READY, RESULT, Confirm, and redemption compare in-memory object pointers:
* **Caller provenance**: Verifies `client->client_process == writer->process` and `client->client_process->token == client->client_primary` (`wine/server/named_pipe.c:1834-1836`).
* **Caller security versions**: Verifies `is_equal_luid(opener_id, client_token_id)` and `opener_modified == client_modified_id` (`wine/server/named_pipe.c:1837-1840`, `wine/server/token.c:2589-2600`).
* **Desktop comparison**: S resolves `desktop_route` to retained `struct desktop *` of caller and requires pointer equality with D's desktop object: `d_desktop == route_desktop`. Refuses if D is created on or switched to another desktop.
* **Liveness & Pin**: Checks caller `running_threads > 0`, D's native pin alive (`process->wex_pin_pidfd != -1`, `wine/server/request.c:426`), and D created suspended by B (`wine/server/process.c:1521`).

## 4. Lifetime and Release
* **Establishment**: Client connects to elevation pipe and sends request; `struct pipe_end` retains `client_process`, `client_primary`, `consumed_writer`, and `client_desktop`. Broker queries binding; server generates opaque IDs and snapshots current generations.
* **Release**: Disconnect, pipe close (`wine/server/named_pipe.c:448,951`), or caller exit drops retained objects. On dialog exit / handle close, `wex_dialog_unprotect` (`wine/server/process.c:80-94`) releases D's pipes and creator reference. Server invalidates authority context on consumption or cancellation; no state survives restart.

## 5. Broker Request Protocol (Op Pattern of Existing Private Request)
Following `load_e1s_profile` (`wine/server/registry.c:3445-3530`) and `create_e1s_account_token` (`wine/server/token.c:3010-3069`), extend `get_pipe_caller_binding` (`wine/server/protocol.def:2692-2709`, `wine/server/named_pipe.c:1784`) with an operation dispatch rather than adding a new top-level request:
* **Request layout**: `@REQ(get_pipe_caller_binding)` keeps `obj_handle_t pipe`, adds `unsigned int operation` and `VARARG(data,bytes)`.
  * `operation = 0 (WEX_BINDING_QUERY)`: Existing H2/S3 legacy query (preserves 88-byte fixed reply for `NtQueryWexPipeCallerBindingEx`).
  * `operation = 1 (WEX_BINDING_AUTHORITY)` [New 3e]: Validates caller objects; retains desktop; captures generations (`server_gen`, `broker_gen`, `map_gen`, `policy_gen`); issues opaque IDs (`prefix_id`, `host_process_id`, `opener_id`, `writer_id`, `desktop_route`); returns S1 Record 1 bytes / sealed tuple inputs via `VARARG(record,e1s_token)`.
  * `operation = 2 (WEX_BINDING_ENDORSE_DIALOG)` [Step 3d]: Passes D's process handle in `data` with 204-byte pre-endorsement tuple; verifies Section 3 rules; assigns `dialog_id`; sends ENDORSED to V; returns sealed tuple.
  * `operation = 3 (WEX_BINDING_RECHECK)` [Step 3d]: Atomically re-verifies D, caller objects, desktop, pin, and generations.

## 6. Specification Corrections for docs/E1_S18E_R8_SPEC.md
1. **Section 3, Line 67**:
   * *Existing*: `Endorsement request (new private server request, B -> S): D's process handle, looked up as a process handle in B's own table with PROCESS_QUERY_LIMITED_INFORMATION, and the pre-endorsement tuple.`
   * *Exact Correction*: `Endorsement request (op of private server request get_pipe_caller_binding, B -> S): the broker's elevation pipe handle, D's process handle with PROCESS_QUERY_LIMITED_INFORMATION, and the 204-byte pre-endorsement tuple.`
   * *Reason*: The server cannot verify "the caller's retained S3 objects and versions match" or "D's desktop is the object behind desktop_route" from D's handle alone without referencing the elevation pipe / caller authority context.

## 7. Files Changed, Division of Work, and Test Matrix
* **Files Changed**:
  * `wine/server/named_pipe.c`: Retain `client_desktop`; implement `WEX_BINDING_AUTHORITY` op in `get_pipe_caller_binding`.
  * `wine/server/protocol.def`: Add `operation` and `VARARG` to `get_pipe_caller_binding`.
  * `wine/server/sysrun.c` & `wine/server/sysrun.h`: Expose server/broker generation helpers outside transport test define.
  * `wine/server/accounts.c`: Expose `accounts_get_map_generation(void)`.
  * `wine/dlls/ntdll/sec.c`: Add `NtQueryWexCallerBindingAuthority` wrapper for broker.
  * `src/programs/wexelevate/e1s18_broker.c`: Ingest server binding into `s_r8_context` to enable `e1s18_build_r8_tuple`.
* **Mechanical vs Authority**:
  * *Mechanical*: Struct packing, byte offsets in `e1r8_codec.h` / `e1s_encode.h`, `/dev/urandom` reads, handle translation.
  * *Authority*: Desktop object comparison, opener/writer provenance and version checks, S6 registration generation binding.
* **Tests**:
  * Unit tests in `src/programs/wexelevate/test_broker_e1s18_main.c` (verify `e1s18_r8_context_complete` with live authority).
  * Server tests in `scripts/test_sysrun_server.sh`:
    * Probe positive: valid caller binding returns matching opaque IDs and current generations.
    * Probe negative: unprivileged or non-system caller denied (`STATUS_ACCESS_DENIED`).
    * Probe negative: opener != writer or modified token ID denied.
    * Probe negative: cross-desktop or modified desktop route denied on endorsement.
    * Probe negative: pipe disconnect drops retained objects and invalidates rechecks.

## 8. Claude review of this draft (2026-11-09; for Codex)

Antigravity drafted sections 1-7 from the code; references spot-checked (registry.c:171 `e1s_prefix_id`,
sysrun.c generation code, protocol.def:2692). Open points, to settle with Codex before 3e is built:

1. **host_process_id is not a pin yet.** `peer_pid` (SO_PEERCRED of the process socket) names the process
   that created the socketpair, normally the parent, not the caller (the R8 v2 spec review made the same
   point). Proposal: reuse step 3b for callers too: when the system role is enabled, set SO_PASSCRED and
   SO_PASSPIDFD on every new process socket and pin the loader's first fd transfer (as `wex_pin_*`), so
   host_process_id stands for that kernel pin. Cost: one pidfd per fd transfer, closed at once. Question:
   acceptable, or pin only processes that open the elevation pipe (too late: their first transfer is gone)?
2. **root_gen and policy_gen have no owner.** root_gen today is the verifier's (e1s6-lab.py:225), not a
   machine-root event counter; policy_gen is a constant 1. Proposal: policy_gen = a server-held counter
   bumped when the protected sysrun.conf or accounts policy reread differs; root_gen = the verifier's
   service-start generation learnt over S6 (V is a root service), renamed in the spec if that is what it is.
3. **Desktop retention point.** Retain the caller thread's desktop at the elevation write (the consumed
   writer), not at connect, and compare D's desktop with it; a desktop switch between them denies.
4. **prefix_id** exists only under the S13 lab define; 3e needs it in the system-role build.
5. **Request shape**: extending `get_pipe_caller_binding` with an operation is fine, but the endorsement
   (3d) then needs the pipe handle too (section 6 correction): agree.
6. Mechanical later (WO): protocol/ntdll wrapper plumbing, broker ingestion into `s_r8_context`, codec
   of the authority reply, tests 5 and 6 of section 7.

## 9. Decisions (Claude, 2026-11-09; Codex is not used for reviews for now)

Claude decides and reviews; Antigravity and Qwen implement from work orders. These decisions replace
the open points of section 8 and correct sections 1-7 where they differ.

1. **Lab-gated, like S6.** All 3e and 3d server code is compiled only with `WEX_E1S6_TRANSPORT_TEST`,
   the gate the S6 transport, its generations and `e1s_prefix_id` already use. The standard-user chain
   stays disabled; moving S6, 3e and 3d into the production build is a separate enablement step with
   its own review. Shipping behaviour does not change.
2. **No protocol change.** `get_pipe_caller_binding` already takes a request payload (named_pipe.c
   1825, the S6 endorsement path). 3e and 3d are new operations selected by a fixed header at the start
   of that payload (magic `E1R8`, version 1, op u16: 1 BINDING, 2 ENDORSE_DIALOG, 3 RECHECK), lab only;
   a payload without the header keeps today's S6 meaning. The section 5 field additions are dropped.
3. **host_process_id is the retained opener process plus its kernel pin.** In lab builds with the
   system role enabled, every new process socket gets SO_PASSCRED and SO_PASSPIDFD, and `receive_fd`
   records the first transfer's pid, uid and pidfd (the 3b mechanism; recording only, the kill rules
   stay for protected dialogs). BINDING requires the opener (equal to the consumed writer, existing
   checks) to have a live pin whose pid equals its unix pid. The ID is server-issued random; the
   comparison is the process object and the pin.
4. **desktop_route is the consumed writer thread's desktop object**, retained when the request message
   is consumed (not at connect). A desktop switch between write and BINDING denies. ENDORSE requires
   pointer equality with D's desktop object.
5. **Generations.** `server_gen` and `broker_gen`: the existing S6 values (sysrun.c), retained per
   broker registration. `verifier_gen`: carried by the verifier's S6 registration and retained by the
   server with it (new: today only the broker learns it). `map_gen`: the S4 map header, read fresh at
   BINDING and at each RECHECK. `root_gen` and `policy_gen`: server-issued random tags bound to a stat
   snapshot (device, inode, size, mtime, ctime) of the machine root directory and of the protected
   `sysrun.conf` respectively; a recheck restats and denies on any difference. No constant 1 anywhere.
6. **prefix_id** is the existing lab `e1s_prefix_id` (registry.c).
7. **IDs never stand alone.** Every opaque ID maps to a retained object in a per-request binding record
   owned by the server (released on pipe disconnect, caller death, cancellation or broker
   re-registration); ENDORSE and RECHECK compare the objects, pins and versions.

Build order (each a work order with tests; Claude reviews each result before commit):
3e-1 lab pin for every process (receive_fd recording, lab gate); 3e-2 desktop retention at consume;
3e-3 BINDING op with the binding record, IDs and generations; 3d-1 ENDORSE_DIALOG op using 3a-3c and
3e; 3d-2 RECHECK op; then the broker ingests BINDING into its R8 context (replaces the zero context).

## 10. Amendments (Claude, 2026-11-09, while writing WO-23)

* **Decision 2 amended:** `get_pipe_caller_binding` has no reply payload, and the S6 path returns its
  data to the verifier, not the broker. BINDING therefore needs one protocol addition: a reply
  `VARARG(binding,bytes)`, empty unless the lab-only op 1 is used. Shipping callers see no change.
* **Decision 4 simplified:** the desktop retained with the consumed writer is authoritative; no separate
  switch check at BINDING (the writing thread may be gone). ENDORSE compares D's desktop with it.
* **Decision 4 corrected (2026-11-10, found by the first end-to-end run, WO-27):** pointer equality of
  D's desktop with the caller's cannot hold in secure mode, where the broker puts D on the secure
  desktop, nor for a caller outside the interactive window station. The rule now, in ENDORSE and every
  RECHECK (`dialog_desktop_ok`, wine/server/named_pipe.c): like the consent prompt on Windows, D runs
  on the session's interactive window station (Wine's one visible window station, WinSta0, flagged
  `WSF_VISIBLE` by user32), whatever window station the caller uses; for `desktop_mode` 1 on its secure
  desktop (`desktop->secure`), for mode 2 on the caller's own desktop (so mode 2 needs a caller on
  WinSta0). The session is checked separately. RECHECK uses the mode sealed at ENDORSE.
* **Decision 5 amended:** `verifier_gen` is checked by the verifier itself (it owns the value and sees it
  in ENDORSED); the server does not retain it. The BINDING reply carries the five server-owned
  generations; the broker adds the verifier's from its S6 registration.
