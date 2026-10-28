# Chromium Control-Channel Isolation Result (WO-20261027-CHROMIUM-CONTROL-CHANNEL)

Date: 2026-10-27
Branch (root): `codex/chromium-control-channel`
Branch (wine child): `codex/g4-control-channel` @ `02d4ef2` (base `a91ceea`)
Attempt dir: `chromium-control-channel-20261027`

## Conclusion

The prior observation that renderer **wine-pid `0x268`** (decimal 616) recorded
`violent_death=1` is **not** an in-process CPU crash of the renderer, and it is
**not** a defect in the Wine control channel. It is wineserver *correctly* detecting
that the renderer's control socket hung up while the process was not in its terminating
state, which is exactly how Wine surfaces an abnormal client death.

The exact causal control-socket event was isolated:

| Field | Value |
|---|---|
| Causal path | `server/process.c` → `process_poll_event()` (the poll/hangup path) |
| **Not** the request path | `request_error` (recvmsg/sendmsg errno) count in trial = **0** |
| Wine pid | `0x268` (decimal 616) |
| Server-side control-socket fd | `726` |
| poll revents (`event`) | `0x11` = **`POLLIN \| POLLHUP`** |
| `is_terminating` at the event | `0` (renderer had not initiated a clean exit) |
| Resulting call | `kill_process(process, !process->is_terminating)` = `kill_process(process, 1)` |
| Outcome | `violent_death=1`, forced `exit_code=1` |

Per the work order's step 4: since **no small actual Wine defect is proven** (the control
channel behaved exactly as designed), no fix is implemented. This document reports the
exact causal socket event/error and stops, per the "else report and stop" branch.

## Method

1. **Instrumentation (wine child `codex/g4-control-channel`).** Three opt-in tracepoints
   guarded by `WEX_TERM_TRACE`, emitting a lowercase `wex_term_trace:` prefix (distinct from
   the older uppercase `WEXTERM:` termination-trace lines). No behavior changes:
   - `server/process.c` — `process_poll_event`:
     `wex_term_trace: process_poll_event server_kill pid=%04x event=0x%x is_terminating=%d unix_fd=%d`
   - `server/request.c` — recvmsg/sendmsg protocol-error path:
     `wex_term_trace: request_error pid=%04x op=%s res=%d errno=%d (%s)` (saved errno captured before the kill)
   - `dlls/ntdll/unix/server.c` — client-side control-socket close:
     `wex_term_trace: client_socket_close pid=%d fd=%d reason=%s errno=%d`
2. **One fresh normal Chromium control trial** in a new attempt dir
   (`chromium-control-channel-20261027`), loopback HTTP fixture, normal sandbox/security
   flags (no sandbox disabling, no `+winsock`, no debugger). `WEX_TERM_TRACE=1` set by the
   `control-channel` mode of `scripts/check-g4-chromium-native-exit.py`.
3. **Analysis** of the archived guest evidence
   (`RUN/chromium-control-channel-20261027/chromium-native-evidence/helper.stderr.log`).

## The causal event (renderer `0x268`)

Verbatim from `helper.stderr.log` (lines 1207–1216):

```
wex_term_trace: process_poll_event server_kill pid=0268 event=0x11 is_terminating=0 unix_fd=726
WEXTERM: server kill_process TARGET_wine_pid=0x00000268 violent_death=1
WEXTERM: server terminate_process TARGET_wine_pid=0x00000268 exit_code=1 skip_thread=(nil)
WEXTERM: server process_killed (last thread died) TARGET_wine_pid=0x00000268 final_exit_code=1
wex_term_trace: process_poll_event server_kill pid=0268 event=0x11 is_terminating=1 unix_fd=726
WEXTERM: server kill_process TARGET_wine_pid=0x00000268 violent_death=0
WEXTERM: server terminate_job exit_code=0 (killing member processes)
WEXTERM: ntdll NtTerminateProcess ENTRY caller_unix_pid=1082 handle=0x93c exit_code=1
WEXTERM: server NT-terminate REQUESTER_wine_pid=0x000000dc TARGET_wine_pid=0x00000268 handle=0x0000093c exit_code=1 self=0
WEXTERM: server terminate_process TARGET_wine_pid=0x00000268 exit_code=1 skip_thread=0x55aba4e266e0
```

Reading the sequence:

- The **first** `process_poll_event` for `0x268` fired with `is_terminating=0` →
  `kill_process(..., 1)` → `violent_death=1`. This is the causal event.
- The **second** `process_poll_event` fired after the first had set the terminating state
  (`is_terminating=1`) → `violent_death=0` (no re-mark).
- The only `NtTerminateProcess` targeting `0x268` comes from the **browser process**
  (wine pid `0xdc`, unix pid `1082`) *after* the violent kill, with `exit_code=1` — this is
  Chromium's standard crash-reap of a dead renderer, not the cause.

The last observable activity of wine-pid `0x268` before the hangup (t≈82.357s) was an
unrelated, benign syscall:

```
82.357:0268:02ac:trace:process:NtQueryInformationProcess (0xffffffffffffffff,0x12,...)
82.357:0268:02ac:fixme:process:GetProcessInformation Unsupported information class 4.
wex_term_trace: process_poll_event server_kill pid=0268 event=0x11 is_terminating=0 unix_fd=726   <- immediately after
```

## Decoding `event=0x11`

Wine's control-socket poll loop uses the epoll fast path. In `server/fd.c`:

```c
assert( POLLERR == EPOLLERR );   // line 540
assert( POLLHUP == EPOLLHUP );   // line 541
...
pollfd[user].revents = events[i].events;   // line 569 — revents copied verbatim from native epoll
if (pollfd[user].revents) fd_poll_event( poll_users[user], pollfd[user].revents );
```

Because of those asserts, Wine's `POLL*` constants are **epoll-aligned**: on Linux
`EPOLLHUP=0x10`, `EPOLLERR=0x8`. Therefore:

- `event=0x11` = `0x1 | 0x10` = **`POLLIN | POLLHUP`**.
- The kill condition in `server/process.c`:
  `if (event & (POLLERR | POLLHUP)) kill_process(process, !process->is_terminating);`
  evaluates to `0x11 & (0x8 | 0x10) = 0x11 & 0x18 = 0x10 != 0` → the kill fires.

There is **no bit remapping** between native epoll revents and the value passed to
`process_poll_event`; `0x11` is a genuine kernel-reported peer hangup on the control socket.
(POLLHUP here means the *peer* — the renderer's unix process — closed its end of the
socket, i.e., it terminated.)

## Ruling out the alternative hypotheses

The work order asked which of three mechanisms set `violent_death=1`:

1. **`process_poll_event` POLLHUP/POLLERR** — ✅ **This is the cause.** Revents `0x11`
   (`POLLIN|POLLHUP`) on server fd `726`, with `is_terminating=0`.
2. **A `request.c` recvmsg/sendmsg protocol error/errno** — ❌ Ruled out. The trial-wide
   count of `wex_term_trace: request_error` lines is **0**. No protocol parse error occurred.
3. **An ntdll client-side socket close** — ❌ Not for `0x268`. The only two
   `client_socket_close` lines in the trial are clean self-terminations
   (`pid=1037 fd=9 reason=process_exit errno=0`, `pid=1033 fd=6 reason=process_exit errno=0`).
   Renderer `0x268` performed **no** client-side close — its ntdll never ran the clean-exit
   path, so the hangup was detected *passively* by the server (the kernel closed the socket
   end when the renderer's unix process died).

## Why this is correct behavior, not a defect

- **Wineserver did not cause the death.** The `POLLHUP` was already present when
  `process_poll_event` ran; `kill_process` was invoked *as a reaction* to an established
  hangup. The renderer's unix process had already exited (abnormally) before wineserver acted.
- **`violent_death=1` is the intended signal.** Wine uses this flag to distinguish a clean
  self-termination (`is_terminating=1` → `violent_death=0`) from an abnormal death where the
  connection was lost without a clean exit (`is_terminating=0` → `violent_death=1`). It is how
  the rest of the system (job/crash notification) learns that a process crashed.
- **It is the sole violent death in the trial.** Across all 22 recorded process terminations,
  exactly one was `violent_death=1` (`0x268`); the other 21 were `violent_death=0`. The
  control channel reported every clean shutdown correctly and flagged only the genuinely
  abnormal one.

## Cross-corroboration from Chromium's own log

`chromium.log` independently shows the browser in crash-recovery, consistent with a renderer
dying abnormally:

```
[220:224:...] ERROR:content\browser\network_service_instance_impl.cc:652] Network service crashed or was terminated, restarting service.
[220:224:...] WARNING:chrome\browser\ui\sad_tab.cc:257] Tab Killed: http://127.0.0.1:42483/
```

Immediately before the renderer's hangup, the trace also shows graphics-module load failures
(`dxil.dll` load failed with `status=c0000135`; `dxcompiler.dll` builtin not found), placing
the renderer in a degraded-graphics state — but that is upstream of, and unrelated to, the
control channel.

## Scope note

The work order scoped this task to isolating the exact control-socket event/errno that sets
`violent_death=1`. That is done definitively above. **Why the Chromium renderer itself
terminated abnormally** (a Chromium-on-Wine rendering/DXIL issue, per the load failures) is a
separate investigation outside this work order and is not addressed here.

## Trial integrity

- The trial ran to completion: orchestrator `consumer rc=0`, `archive_exit=0`,
  `outside_modules_unchanged=true` (the overlaid traced `ntdll.so`/`wineserver` did not leak
  into any other module's hash).
- Graceful QMP powerdown verified: `graceful_powerdown=true`, `qemu_exit_status=0`.
- Prior consumed attempt dirs were preserved. To satisfy the run()'s 4 GiB pre-stage disk guard
  on the persistent guest disk (which was at 99% / 1.3 GiB free), only three **Wine build
  deploy caches** (`wine-control-channel-20261027`, `wine-termination-trace-20261027`,
  `wine-token-integration-20261027` — each a re-creatable `build/wow64` artifact, guarded by
  the presence of a `build/` subdir) were removed, freeing ~9.6 GiB. No protected
  `chromium-*` attempt OUTPUT dir was touched. (Note: two dirs named in the preserve list —
  `chromium-resource-pressure-20261027` and `chromium-winsock-unblock-20261027` — were already
  absent on this guest before any action; nothing of this task's removed them.)

## Deliverables & branch state

- **Root** (`codex/chromium-control-channel`): added a `control-channel` diagnostic mode to
  `scripts/check-g4-chromium-native-exit.py` (WEX_TERM_TRACE on; isolates the causal
  control-socket event) + this result document. The per-run orchestrator
  (`build/g4-virgl-20261022/chromium-control-channel-trial.py`) is a gitignored local build
  artifact, matching how prior diagnostic modes were delivered.
- **Wine child** (`codex/g4-control-channel` @ `02d4ef2`): the three opt-in
  `wex_term_trace:` control-channel tracepoints (no behavior changes). No fix was made because
  no defect was proven.
