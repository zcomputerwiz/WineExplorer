# Chromium Termination-Trace Result — WO-20261027-CHROMIUM-TERMINATION-TRACE

Date: 2026-10-27
Work order: `docs/CHROMIUM_ECN_REVIEW_AND_TERMINATION_WORK_ORDER_20261027.md`
Classification: **Diagnostic only. No Wine fix is identified from this data alone.**

## Verdict (one paragraph)

A single fresh, normal Chromium comparison was run against the original local HTTP
fixture over loopback, with normal sandbox/security flags and actual VirGL, **without**
`+winsock`. Opt-in `WEX_TERM_TRACE` tracepoints produced genuinely new termination
evidence: the top-level browser (Windows PID 224 / wine `0xe0`) explicitly called
`NtTerminateProcess` on its page renderer (wine `0x268` / Windows PID 616) with
`exit_code=1, self=0`, immediately after that renderer recorded `violent_death=1` — i.e.
the renderer **crashed** and the browser reaped it. A second renderer (`0x260`/608) exited
cleanly and was likewise reaped by the browser; a third renderer (`0x2a0`/672) survived
until the final observer/prefix teardown. The top-level browser did **not** exit within the
45 s observation window (helper `timeout`, `wait_result=258`). Chromium's own log
corroborates: the network service "crashed or was terminated, restarting service" early on,
and the browser logged `Tab Killed: http://127.0.0.1:41441/` (sad tab) for the fixture URL.
The trace cleanly separates **browser-initiated child reaps** from **prefix-init teardown**
and from the final **observer/prefix/session teardown** that ran after the helper timed out.
No minimal Wine patch is supported by this evidence: the violent deaths are unhandled
signals (no `FATAL`/`CHECK` in `chromium.log`) and no minidump or faulting-address capture
was taken, so the exact crashing Wine code path is not pinned down by this tracepoint.

## Branches and commits

| Item | Value |
|---|---|
| Root branch | `codex/chromium-termination-trace` (worktree `D:\DeepThought\Worktrees\chromium-token-integration\WineExplorer`) |
| Root base (before this result commit) | `d16cbc86` — "Ingest parent termination trace work order and update continuation policy" |
| Root code change (this commit) | `scripts/check-g4-chromium-native-exit.py` — adds the fixed `termination-trace` mode |
| Wine base (retained unchanged) | `codex/g4-token-integration` @ `fffa7de` (combined candidate build) |
| Wine child branch | `codex/g4-termination-trace`, cut from `fffa7de`, **excludes** the ECN stub `d629841`; committed as `a91ceea`, pushed to `github.com/zcomputerwiz/wine.git` |

The prior ECN accept-and-ignore stub (`d629841`) is **rejected** per the parent decision and
is not present on the child branch. ECN remains a future TODO and is not treated as causal
to this fixture failure.

## What was implemented (WEX_TERM_TRACE)

Tiny, semantically inert, opt-in tracepoints guarded by the `WEX_TERM_TRACE=1` environment
variable. **Off by default in every other mode** — the only difference from the base build is
that these lines are printed to stderr when the variable is set. No behavioral change; no new
system calls; no retry logic; no option handling.

- `ntdll/unix/process.c` — `NtTerminateProcess` **entry** and **return**:
  - `WEXTERM: ntdll NtTerminateProcess ENTRY caller_unix_pid=.. handle=.. exit_code=..`
  - `WEXTERM: ntdll NtTerminateProcess RETURN caller_unix_pid=.. handle=.. exit_code=.. ret=.. self=..`
- `server/process.c` — the chokepoints where the server actually terminates processes:
  - `WEXTERM: server NT-terminate REQUESTER_wine_pid=.. TARGET_wine_pid=.. handle=.. exit_code=.. self=..` (the NT terminate handler; `self=1` when a process terminates itself)
  - `WEXTERM: server terminate_process TARGET_wine_pid=.. exit_code=.. skip_thread=..`
  - `WEXTERM: server kill_process TARGET_wine_pid=.. violent_death=..` (`violent_death=1` ⇒ the process died from an unhandled signal/exception)
  - `WEXTERM: server process_killed (last thread died) TARGET_wine_pid=.. final_exit_code=..`
  - `WEXTERM: server terminate_job exit_code=.. (killing member processes)` and `terminate_job -> member TARGET_wine_pid=.. exit_code=..`

**Coverage note (honest limitation):** `ntdll/unix/process.c` is a Unix-side PE; its
`NtTerminateProcess` entry/return trace fires **only when a process explicitly calls the API**.
Many terminations happen entirely inside wineserver (job teardown, last-thread-died cleanup)
and never pass through the ntdll call site. The **server-side** tracepoints are therefore the
primary source of evidence here; the ntdll lines add the *caller* (the calling process's real
Linux PID and its own `self` flag) for the browser's explicit reaps.

## Execution

- One fresh normal Chromium comparison (`--app=http://127.0.0.1:41441/<nonce>/index.html#nonce=<nonce>`), original local HTTP fixture over loopback, normal sandbox/security flags, actual VirGL, **no** `+winsock`.
- Wine candidate = base `fffa7de` + the two traced files (`ntdll.so`, `wineserver`) from the child branch build; deployed as an overlay and verified (WEXTERM tracepoint count present in both modules). All other modules byte-identical to base (`outside_modules_unchanged: true`).
- `WEX_TERM_TRACE=1` was set only for this run. Wineserver + `ntdll.so` stderr propagates to the client's stderr, so the full trace landed in `chromium-native-evidence/helper.stderr.log` (2234 lines; the candidate build also emits verbose `trace:process`/`trace:seh`, which is why the file is large).
- **Helper recompile (build evidence):** the native-exit observer's compiled contract pins `WEX_EXIT_OUTPUT`; a helper built for another run path hard-rejects this run at runtime. The helper was recompiled for this run's fresh output path (`userenv-browser-termination-trace-20261027.exe`, machine 34404; UTF-16 path verified present, prior token path absent) with the canonical recipe and zero warnings.
- VM lifecycle: powered on via the VirGL launcher, waited for QMP identity, ran the one trial, then a graceful QMP `system_powerdown` — `graceful_powerdown=true`, `qemu_exit_status=0`, guest-shutdown observed, QEMU+Xvfb both stopped.

## Role map (wine PID = Windows PID)

| wine PID | Windows PID | Chromium role |
|---|---|---|
| `0xe0` | 224 | **browser(main)** — the helper's child (`native-exit.jsonl` `bound` `child_pid=224`) |
| `0xe8` | 232 | crashpad-handler |
| `0x1f8` | 504 | gpu-process |
| `0x218` | 536 | utility |
| `0x260` | 608 | **renderer** (exited cleanly, reaped by browser) |
| `0x268` | 616 | **renderer** (crashed: `violent_death=1`, reaped by browser with exit_code=1) |
| `0x270` | 624 | utility |
| `0x2a0` | 672 | **renderer** (survived to observer/prefix teardown) |
| `0x364` | 868 | utility |

Wine PIDs in the trace are hex and equal the Windows PIDs the app sees. `caller_unix_pid` is
the real Linux PID of the calling process.

## Termination evidence (caller / target / status / time)

Times are Wine-server ticks from adjacent timestamped `trace:process` lines; the WEXTERM lines
themselves carry no per-event timestamp. Wall-clock anchors come from `chromium.log`.

### Phase A — prefix-init teardown (before Chromium did anything)
- wine `0x30` (winemenubuilder, unix 1050): self-terminate `exit_code=-1073741515` (= `0xC0000135`, STATUS_DLL_NOT_FOUND), `violent_death=0`. Early prefix-init process exiting.
- wine `0x28` (wineboot --init, unix 1048): self-terminate `exit_code=0` (two `NtTerminateProcess`: handle `0x0` then `-1`). Prefix init complete, right before the browser launched.

### Phase B — browser reaps a crashed page renderer (the key finding)
Renderer `0x268`/616 last active at tick **155.790** (`NtQueryInformationProcess`, thread `0x26c`), then:
- `kill_process 0x268 violent_death=1` — the renderer **died violently** (unhandled signal).
- `terminate_process 0x268 exit_code=1 skip_thread=(nil)`; `process_killed 0x268 final_exit_code=1`.
- `terminate_job exit_code=0 (killing member processes)`.
- **Browser `0xe0` (unix 1094) → `NtTerminateProcess TARGET 0x268 handle=0x940 exit_code=1 self=0` → `ret=0`.** The browser explicitly reaped its crashed child.

### Phase C — second renderer exits cleanly, browser reaps it
- Renderer `0x260`/608 (unix 1192): self-terminate `exit_code=0`; `process_killed final_exit_code=0`.
- Browser `0xe0` → `NtTerminateProcess TARGET 0x260 handle=0x8dc exit_code=1 self=0` → `ret=0`, and again `handle=0x93c exit_code=0 self=0` → `ret=0` (the browser holds multiple handles to each child).

### Phase D — a utility exits cleanly
- Utility `0x364`/868 (unix 1257): self-terminate `exit_code=0`, `violent_death=0`.

### Phase E — observer/prefix/session teardown (after the helper timed out)
The native-exit helper observed a **timeout** (`wait_result=258`, 45 000 ms; tick 195.7 s) and
exited `exit_code=1` without killing its child (by contract, it leaves the tree alive for the
harness to reap). Then the harness tore down the whole job/prefix:
- helper wine `0x20`/32 (unix 1044): self-terminate `exit_code=1`.
- wineserver `terminate_process exit_code=1` for `0x38` (services), `0x44`/`0x98` (winedevice), `0x68` (svchost), `0x7c` (plugplay), and **`0xe0` (browser main)**.
- `terminate_job` → members: `0x270` (utility), `0xe8` (crashpad-handler), `0xfc` (explorer), `0x128` (rpcss), `0x158` (conhost), `0x1f8` (gpu-process), `0x218` (utility), **`0x2a0` (renderer — the surviving third renderer)**, `0x1a8` (lsass).

**Distinction required by the work order:** Phase B/C are *browser-initiated* reaps of the
browser's own children during the trial; Phase A is *prefix-init* teardown before Chromium ran;
Phase E is the *observer/prefix/session* teardown triggered after the helper timeout — it is
what finally killed the still-alive browser (`0xe0`) and surviving renderer (`0x2a0`), not
Chromium.

## Chromium-log corroboration (`chromium.log`, `[pid:tid:...]`)

- `22:06:20.468` — browser (224) navigates to the fixture URL `http://127.0.0.1:41441/<nonce>/index.html#nonce=...`.
- `22:06:20.523` — **`Network service crashed or was terminated, restarting service.`** (a utility process died early too).
- `22:06:21.052/.057` — renderers 616 (`0x268`) and 608 (`0x260`) log only startup lines ("Available number of cores: 4"); 616 then vanishes (the violent death above).
- `22:06:35.154` — **`Tab Killed: http://127.0.0.1:41441/`** (`sad_tab.cc`) — the browser declared the fixture tab dead (sad tab), confirming the page failed to render.
- `22:06:35.412/.784` — renderer 672 (`0x2a0`) spawns and creates a script context; it survives until Phase E.
- External `update.googleapis.com` / `google.com` / `accounts.google.com` requests fail with `net::ERR_ADDRESS_UNREACHABLE` — expected (no internet in the isolated VM) and unrelated to the loopback fixture.

## HTTP / readiness / front image

- **Readiness:** `readiness.json` `ready=true`; cloud-init done, systemd running, logind active; two validated SSH probes within deadline.
- **HTTP/navigation:** the browser initiated the fixture navigation (above). This trace does **not** independently confirm whether the loopback request reached the fixture server — consistent with the work order's instruction not to claim a confirmed IPC failure from absent fixture requests. What *is* established is the renderer crash and the resulting `Tab Killed`, not an IPC layer fault.
- **Front image:** `terminal-front.png` was captured and is retained in the run directory and in `chromium-native-evidence.tar.gz`.

## Honest limitations

1. No per-event timestamp on the WEXTERM lines; times are bracketed by adjacent trace lines and cross-referenced to `chromium.log` wall-clock (the two clocks are not perfectly aligned).
2. The ntdll entry/return trace only fires for explicit `NtTerminateProcess` calls; server-side events are the primary evidence.
3. **No minidump was captured**, and its absence is *not* evidence of no crash — to the contrary, `violent_death=1` confirms a crash occurred. The exact faulting instruction/API in Wine that kills the renderer (and the network service) is not identified by this tracepoint.

## Smallest supported actual Wine fix

**None identified.** This is a diagnostic work order and the evidence localizes the failure to
early **violent deaths (unhandled signals)** of Chromium child processes — first the network
service, then the page renderer `0x268`/616 — which the browser reaps with `exit_code=1`,
culminating in a sad-tab. There is no `FATAL`/`CHECK` failure in `chromium.log` and no
faulting-address capture, so no specific minimal Wine patch is supported by this data. A
follow-up that pins the root cause would need a renderer/network-service minidump or a
faulting-address (RIP) capture at the violent death — which this opt-in termination
tracepoint deliberately does not attempt (it records *who killed whom, with what status and
when*, not *where* the crash faulted).

## Tests / build evidence

- Host Python suite: **33/33 passed** (WSL Debian), including the new `termination-trace` mode wiring.
- Wine suites match the base `fffa7de` baseline (the two traced files are semantically inert when `WEX_TERM_TRACE` is unset).
- `git diff --check`: clean (zero whitespace errors) on the committed change.
