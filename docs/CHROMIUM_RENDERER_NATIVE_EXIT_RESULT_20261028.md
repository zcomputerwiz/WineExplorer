# Chromium Page Renderer Native Exit — Result

**Work order:** WO-20261028-CHROMIUM-RENDERER-NATIVE-EXIT
**Date:** 2026-10-28
**Root branch:** `codex/chromium-renderer-native-exit` (worktree `D:\DeepThought\Worktrees\chromium-token-integration\WineExplorer`)
**Wine child branch:** `codex/g4-renderer-native-exit` @ `aa79f01` (cut from `02d4ef2`; pushed to github.com/zcomputerwiz/wine.git)
**Status:** COMPLETE — report-and-stop. **No Wine source fix is warranted or imported.**

## 1. Verdict (initiating exit reason, isolated)

The initiating exit of the Chromium page renderer (**Wine PID `0x268`**) was an **external kernel OOM-killer SIGKILL** of its Linux process — **Linux PID 1183 (`CrRendererMain`)** — at guest-kernel time ≈ 87.9 s.

This is **not** a Wine defect:

- It is **not** a Wine `abort_thread()` / `abort_process()` self-termination (no such path ran for 0x268).
- It is **not** an unhandled SEH fault / exception (no debug event was delivered for 0x268).
- A kernel SIGKILL executes **no user-space code**, so the renderer could not close its wineserver client socket, run a handler, or report an exit. That fully explains the earlier "missing `client_socket_close`" observation as an external kill, not a missing Wine exit path.

**No production fix is accepted or imported from this delivery.**

## 2. Method (per parent directive)

- Started from **retained logs + source**; no new VM run was needed to repeat the same observation (parent step 1: *"No new VM just to repeat the same observation"*).
- Mapped Wine PID/TID → Linux identity from the **creation path + process count/identity/RSS** in the kernel OOM task table — **never assumed equal IDs**.
- Added opt-in tracepoints only as a **step-2 contingency capability** (committed, off by default); retained evidence was already sufficient, so they were **not exercised in a live trial this cycle**.

## 3. Evidence chain

### 3.1 Renderer creation and last activity (wineserver trace)

`chromium-native-evidence/helper.stderr.log`:

- `79.642:00dc:0188:trace:process:NtCreateUserProcess … chrome-win\chrome.exe pid 0268 tid 026c` — renderer **0x268** created as a child of the browser (**wine 0xdc = Linux 220**).
- Lines 1205–1206, `82.357:0268:02ac:fixme:process:GetProcessInformation Unsupported information class 4.` — **last** renderer activity before death.
- (For contrast, the `dxil.dll` / `dxcompiler.dll` load warnings at t=82.311/82.323 are on **Wine PID 0x200 = the GPU process**, not renderer 0x268 — a red herring; no graphics/DXIL change is selected from them.)

### 3.2 Abrupt-death fingerprint (the only violent death in the trace)

`helper.stderr.log` lines 1207–1217:

```
wex_term_trace: process_poll_event server_kill pid=0268 event=0x11 is_terminating=0 unix_fd=726   # 1207
WEXTERM: server kill_process TARGET_wine_pid=0x00000268 violent_death=1                          # 1208 (ONLY violent_death=1 in the log)
WEXTERM: server terminate_process TARGET_wine_pid=0x00000268 exit_code=1 skip_thread=(nil)       # 1209
WEXTERM: server process_killed (last thread died) TARGET_wine_pid=0x00000268 final_exit_code=1   # 1210
wex_term_trace: process_poll_event server_kill pid=0268 event=0x11 is_terminating=1 unix_fd=726   # 1211
WEXTERM: server kill_process TARGET_wine_pid=0x00000268 violent_death=0                          # 1212
WEXTERM: server terminate_job exit_code=0 (killing member processes)                             # 1213
WEXTERM: ntdll NtTerminateProcess ENTRY caller_unix_pid=1082 handle=0x93c exit_code=1            # 1214
WEXTERM: server NT-terminate REQUESTER_wine_pid=0x000000dc TARGET_wine_pid=0x00000268 handle=0x0000093c exit_code=1 self=0   # 1215
```

Interpretation:

- `event=0x11` = POLLIN|POLLHUP on the renderer's client socket; **`is_terminating=0`** at HUP time means wineserver did **not** expect the process to be shutting down — the client vanished abruptly. That is recorded as **`violent_death=1`** (line 1208).
- There is **no `client_socket_close`** and **no SEH/debug event** for 0x268. A graceful Wine exit or an unhandled exception would have produced one of those. Contrast: the gracefully-exiting processes in the same trace each logged `wex_term_trace: client_socket_close … reason=process_exit errno=0` (pids 1033, 1037); the renderer did not — consistent with a SIGKILL that could not run the client-side close.
- The subsequent `NtTerminateProcess` from **caller_unix_pid=1082** (the browser, wine 0xdc) at lines 1214–1215 is the browser **cleaning up after** the renderer was already gone — a consequence, not the initiating cause.

### 3.3 Kernel OOM-kill of the matching Linux process

`serial.log`:

- Line 769: `[84.055919] wine_threadpool invoked oom-killer: gfp_mask=0x140cca(GFP_HIGHUSER_MOVABLE|__GFP_COMP), order=0, oom_score_adj=0` (invoking thread PID 1088 `wine_threadpool`).
- Call trace (lines 772–790): `oom_kill_process.cold → out_of_memory → __alloc_pages_slowpath → … → __filemap_get_folio → filemap_fault → … → asm_exc_page_fault` — a **page fault** that could not be satisfied because the system was out of memory.
- Memory state: line 808 `free:22255` (~87 MB free); line 809 `all_unreclaimable? yes`; lines 823–824 `Free swap = 0kB / Total swap = 0kB` (**no swap**); line 825 `1048442 pages RAM` (~4 GB).
- Task table (values in 4 kB pages), the Chromium processes:

| Linux PID | name            | rss (pages) | rss ≈ MB   |
|-----------|-----------------|-------------|------------|
| 1082      | CrBrowserMain   | 107311      | ~423       |
| 1156      | CrGpuMain       | 105599      | ~410       |
| 1165      | storage.CrUtili | 94466       | ~368       |
| 1181      | CrRendererMain  | 186791      | ~728       |
| **1183**  | **CrRendererMain** | **187196** | **~730 (highest)** |
| 1194      | network.CrUtili | 97042       | ~378       |
| 1198      | CrRendererMain  | 112550      | ~439       |

- Line 871: `oom-kill:constraint=CONSTRAINT_NONE,nodemask=(null),cpuset=/,mems_allowed=0,global_oom,task_memcg=/user.slice/user-1000.slice/session-19.scope,task=CrRendererMain,pid=1183,uid=1000`
- Line 872: `Out of memory: Killed process 1183 (CrRendererMain) total-vm:1636770424kB, anon-rss:748648kB, file-rss:0kB, shmem-rss:136kB, UID:1000 pgtables:2068kB oom_score_adj:0`
- Line 873: `systemd-journald[331]: Under memory pressure, flushing caches.` (sustained pressure)

There is **exactly one** OOM kill in the log (PID 1183) and **exactly one** `violent_death=1` (Wine 0x268).

### 3.4 Identity mapping (Wine 0x268 → Linux 1183), by elimination + identity + RSS

- 0x268 is a **CrRendererMain**: it was created as a child of the browser running `chrome.exe`, and the OOM task table shows the renderer processes are named `CrRendererMain`.
- The single violent death (0x268) must be the single OOM-killed process (1183): both are the one externally-killed Chromium renderer.
- 1183 is a `CrRendererMain` with the **highest resident memory** (~730 MB; anon-rss = 748648 kB), consistent with being the page renderer under load at the moment of the fault.
- Conclusion: **Wine PID 0x268 = Linux PID 1183 (`CrRendererMain`)**. This is established by process count/identity/RSS, not by assuming Wine and Linux PIDs are equal (they are not — the browser is wine 0xdc / Linux 220; the renderer is wine 0x268 / Linux 1183).

## 4. What remains unknown

- **Exact wine-session ↔ kernel-boot clock offset.** The wineserver trace uses a session-relative clock (t≈79–87 s) and the kernel log uses boot time; they could not be aligned to sub-second precision from retained logs. This does **not** affect the identity proof (which rests on count/identity/RSS, not timestamps).
- **Whether more guest RAM would let the page complete.** Out of scope: this is a resource change, not a code fix.
- **The precise in-renderer memory-growth trigger** (V8 heap / GPU / shader compilation). Not needed to establish the initiating exit reason; the OOM task table already shows the renderer was the largest resident consumer.

## 5. Retained trial outcomes (control-channel, from `results.json` + `readiness.json`)

- **Readiness:** `ready=true`, boot_id `951591cc-cfd8-4982-a217-cdf4d382f513`, cloud-init done, systemd running, logind power handler active/running.
- **Graphics probe (outer + inner eglinfo):** `passed=true`, driver `virgl`, renderer `virgl (D3D12 (Intel(R) UHD Graphics 750))` — **graphics/VirGL works**; the failure is not graphics-related.
- **Native run:** `passed=false`, `diagnostic_exit=1`, `observation_complete=false`; `journal_outcome.outcome = native_timeout` with `wait_ms=45000`, `wait_result=258` on the held browser handle (`child_pid=220`); helper error `"Linux helper failed/exit mismatch"`; `manifest_marker_verified=false`, `accepted_observation=false`. The browser (220) stayed alive through the 45 s wait — what died was the renderer.
- **HTTP fixture:** `requests=[]`, `errors=[]`; classification *"HTTP writes only; browser consumption NOT_PROVEN"* — the page was never consumed because the renderer died before load completed.
- **Front (screenshot):** `front_exit=0`; the captured PNG is Chromium's crash page **"Aw, Snap" / RESULT_CODE_KILLED** (a renderer was killed).
- **JS:** no JS success signal — consistent with the page never reaching a rendered/ready state (see HTTP `requests=[]` and the crash-page screenshot).
- **Shutdown / cleanup:** `cleanup_complete=true`, `remaining_servers=[]`, `remaining_prefix_processes={}`, private loopback namespace unchanged (inode `4026532238`); orchestrator consumer `0` / archive `0`; launcher recorded matching QMP ACKs, guest SHUTDOWN, QEMU exit 0, no fallback.
- **Collection success is separate from Chromium pass/fail:** all evidence was collected successfully (consumer/archive 0) even though the native run `passed=false`.

## 6. Why no fresh trial was run

Parent step 1 found the retained kernel OOM evidence and directed *"No new VM just to repeat the same observation."* An identical-condition rerun would reproduce the same guest OOM with **no genuinely new exit evidence**. Per parent step 4 (a small real Wine defect is not proven), the correct action is to **stop with this precise observation + the smallest justified next action**, not to repeat unchanged runs.

## 7. Committed tracepoints (step-2 contingency, NOT exercised this cycle)

On wine child `codex/g4-renderer-native-exit` @ `aa79f01` (2 files, +56; compiles cleanly; **off by default**, log-only):

- `dlls/ntdll/unix/thread.c` — `abort_thread()` / `abort_process()`: log caller unix pid/tid/status before `_exit()`. Runs in ordinary (non-signal) context, so `getenv()` / `fprintf()` are safe.
- `dlls/ntdll/unix/signal_x86_64.c` — `setup_raise_exception()`: log the unhandled fault's `ExceptionCode` / `ExceptionAddress` just before `send_debug_event()`. Runs in signal context → **async-signal-safe** (`write(2)` on a stack buffer, gated by a `sig_atomic_t` set once at init). No stdio, no allocation, no backtrace; control flow and process outcome unchanged.

All are opt-in via `WEX_TERM_TRACE`, preserve status/errno/thread accounting/signal behavior, and add no signal interception, global observer, or all-syscall trace. They were committed as the contingency capability for a future trial where retained evidence is insufficient — **not** as a fix, and **no production fix was imported**.

## 8. Smallest justified next action

This is a **guest memory-capacity problem, not a Wine source defect.** The smallest next actions are **environment/resource changes**, all out of scope for this WO:

- Increase the guest's RAM above ~4 GB and/or add swap so the renderer's page fault can be satisfied; and/or
- Cap renderer + GPU memory (Chromium flags / process limits) so a single renderer cannot exhaust the guest.

**Explicitly do not** select a DXIL download, graphics patch, or sandbox change from the 0x200 GPU-process warnings — those are unrelated to the renderer's initiating exit (parent directive).

## 9. Deliverables & git

- **Root:** this document on `codex/chromium-renderer-native-exit` (pushed to origin).
- **Wine child:** `codex/g4-renderer-native-exit` @ `aa79f01` on github.com/zcomputerwiz/wine.git (committed + pushed; 2 files, +56; whitespace check clean; Co-Authored-By present).

## 10. Constraints honored

No new VM run; no debugger attach/suspend, bypass flags, new image, reinstall/download, or master merge; no harness redesign or exhaustive conformance matrix; no speculative DXIL/graphics/sandbox fix; explicit-filename staging only (no blanket `git add .`); no cache/candidate deletion to pass a space guard.
