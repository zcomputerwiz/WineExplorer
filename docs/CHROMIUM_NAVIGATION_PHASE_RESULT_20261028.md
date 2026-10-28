# Result — WO-20261028-CHROMIUM-NAVIGATION-PHASE (Stalled NavigationRequest Phase Isolation)

- **Date:** 2026-10-28
- **Run name:** `chromium-navigation-phase-20261028`
- **Branch / base:** `codex/chromium-navigation-phase` @ f9ee4afd (matches origin)
- **Pinned Chromium:** 156.0.8078.0 (chrome-win zip, commit `7d9824099030f5c28c939ba08654562faaad7c09`)
- **Wine candidate build (reused, NOT rebuilt this WO):** `/home/debian/wex-g4/wine-control-channel-20261027/build/wow64`
- **VM:** owned R4 overlay `system-native-identity-r4-20261024.qcow2`, `-m 8192`, private loopback netns, hostfwd `127.0.0.1:2235→22`, VirGL (`virtio-vga-gl`), QMP in isolated socket dir
- **Spec:** `docs/CHROMIUM_NAVIGATION_PHASE_WORK_ORDER_20261028.md`

## Objective (restated)

Run ONE fresh normal 8 GiB control run with supported bounded startup tracing to identify the **exact unfinished NavigationRequest phase** and the **required callback** that stalls the loopback navigation before commit — without assuming the cause. If a small real Wine defect is proven, fix it; otherwise STOP and document the exact unfinished phase + required callback + the smallest targeted semantic check.

## Verdict (up front)

| Dimension | Result |
|---|---|
| **Diagnostic collection** | **SUCCESS** — all three bounded artifacts captured: NetLog (3219 events), startup trace (~10 MB, flushed at 35 s), scoped VLOG (481 lines). |
| **Page-load acceptance** | **NO** — the navigation stalled before commit; the browser process never exited within the 45 s wait and was reaped at teardown. `passed=false`, `observation_complete=false`. |
| **Exact unfinished phase identified** | **YES** — see below. The stall is in the **URL-loader handoff → awaiting network-service response** window, *after* throttle/beforeunload/start-checks cleared. |
| **Small real Wine defect proven?** | **NO** — a candidate cause (one-time network-service crash/restart) is observed but has **no captured exception/CHECK/stack**, so no single fixable function is established. Per the WO decision rule: **STOP and document.** |

> Collection success ≠ page acceptance, reported separately per the WO. The run's own classification string states this explicitly: *"Changed parentage: ordinary CreateProcess held-handle Chromium status ONLY; never rendering/security acceptance."*

## The exact unfinished NavigationRequest phase (the core answer)

The startup trace records TRACE_EVENT names as they fire. Probing the flushed protobuf for the navigation phase markers gives a clean, unambiguous boundary:

**Fired (present in trace):**
`NavigationRequest::Create → BeginNavigation → BeginNavigationImpl → StartNavigation → WillStartRequest → NavigationThrottleRunner::ProcessInternal → OnStartChecksComplete → NavigationURLLoaderImpl::Start → ThrottlingURLLoader::StartNow`

**Never fired (absent from trace):**
`OnReceiveResponse`, `WillProcessResponse`, `OnURLLoaderComplete`, `StartToCommit`/`StartCommit`, `CommitNavigation`, `RenderFrameHostImpl::CommitNavigation`, `DidFinishNavigation`, `OnCommitted`.

Interpretation:
- `NavigationThrottleRunner::ProcessInternal` + `OnStartChecksComplete` both fired ⇒ **the navigation throttle and any beforeunload/start checks completed and cleared.** The stall is *not* in the throttle, *not* in beforeunload.
- `NavigationURLLoaderImpl::Start` + `ThrottlingURLLoader::StartNow` fired ⇒ **the document URLLoader was started / handed off toward the network service.**
- `OnReceiveResponse` (and everything after) never fired ⇒ **the NavigationRequest is blocked waiting for the network service to deliver the document response/error, and that callback never arrived.**

**Exact unfinished phase:** between `ThrottlingURLLoader::StartNow` (URLLoader start / handoff into the network service) and `NavigationRequest::OnReceiveResponse`.
**Required callback that never fires:** the URLLoaderClient response delivery for the loopback document request — first `OnReceiveResponse`, which would then drive `WillProcessResponse → StartCommit → CommitNavigation`. No response *or* error was ever delivered, so no commit occurred and the process stayed alive until the 45 s helper timeout.

## Evidence

### NetLog (in-progress format, 3219 events) — the document URLRequest was never created

Decoding `constants.logEventTypes`/`logSourceType` (they are stored as `{NAME: code}`):
- The **only** event in the entire capture that references the loopback host is a single **speculative `HOST_RESOLVER_MANAGER_REQUEST`** for `http://127.0.0.1:36631` (source type `NETWORK_SERVICE_HOST_RESOLVER`), with params `{"is_speculative": true, "allow_cached_response": true, "cached": true, ...}` — a DNS warm-up that completed instantly from cache. **There is no `URLRequest`, no socket connect, no HTTP transaction for the document.**
- The 9 real `URL_REQUEST_START_JOB` sources are all external and all failing offline: `google.com/async/folae`, `accounts.google.com/ListAccounts` (×5), `update.googleapis.com/service/update2/json` (×3). None is `127.0.0.1`.
- **All 3219 events fall in a ~36 ms window** (`time` 171667→208156 µs) and then the network service logs nothing for the rest of the ~40 s capture — a burst-then-silence signature consistent with the navigation stalling on a request that never completes.

### chromium.log (scoped VLOG, 481 lines) — one-time network-service crash/restart at nav start

- `143959.641` data-protection observer sees the URL `http://127.0.0.1:36631/<nonce>/index.html#nonce=…` (navigation starting).
- `143959.929` **`ERROR content\browser\network_service_instance_impl.cc:652 — Network service crashed or was terminated, restarting service.`** (~288 ms after nav start.) This is the **only** network-service line in the log; there is no second crash and no explicit re-init/ready line.
- **No FATAL, no CHECK failure, no access violation, no exception code, no stack dump anywhere in the log.** The only `0xC0000…` codes present are `0xC0000BB9` (ERROR_NOT_FOUND) from a Hyper-V CPU perf-counter probe (`PdhAddEnglishCounter`) — unrelated noise.
- Persistent offline/Wine environment noise (not loopback-blocking): repeated `Failed to read DnsConfig`, `WSALookupServiceBegin failed with: 8`, and `connect failed: 10051` / `ERR_ADDRESS_UNREACHABLE` for `accounts.google.com`. Loopback (`127.0.0.1`) needs no DNS, so these do not explain a loopback stall by themselves.

### native-exit journal + results verdict — the stall signature

- `native-exit.jsonl`: `bound` (full 15-arg command line; nonce `28bc000f…`; port 36631) then `timeout` (`wait_result=258` = WAIT_TIMEOUT, `wait_ms=45000`). **No `exit` event** — the child (chrome.exe pid 216) never exited and was reaped.
- `chromium-native-evidence/results.json`: `passed=false`, `diagnostic_exit=1`, `observation_complete=false`; `journal_outcome.outcome="native_timeout"`, `accepted_observation=false`, `manifest_marker_verified=false`.
- The `error` string `"ValueError('Linux helper failed/exit mismatch')"` is the **expected** consequence of a stall in this diagnostic mode, not a collection failure: the check-script's contract expects the helper to observe a *natural child exit* (a successful load-and-exit); because the navigation stalled, the child never exited within 45 s, `WaitForSingleObject` returned WAIT_TIMEOUT, and the observed terminal event (timeout) does not match the expected natural-exit outcome. All three diagnostic artifacts were nonetheless captured successfully.

### Front capture
- `front_exit=0` — the front screenshot was captured (`terminal-front.png`, 9,825 bytes). **Note:** image rendering is unavailable to this session's model, so its pixel content was not visually inspected here; given the pre-commit stall (no `OnReceiveResponse`/commit), the frame is *expected* to show an unloaded/blank app window rather than fixture document content. This is stated as inference from the navigation state, not a visual assertion.

## Proximate-cause assessment (association, not a proven single-function root cause)

The evidence most consistently indicates: **the loopback document request's handoff into the network service coincided with / was interrupted by a one-time network-service crash+restart (~288 ms after nav start). The restarted/absent service never delivered a response or error for that request (NetLog shows no `URLRequest` for 127.0.0.1 at all), so the NavigationRequest blocked in the pre-commit "awaiting response" state until the 45 s timeout.**

Why this is *not* yet a proven small Wine defect:
- The crash has **no captured exception/CHECK/stack**, so the specific failing Wine syscall/API is unknown. Writing a targeted fix against an unexplained process crash would be speculative.
- The alternative (a Wine loopback socket/connect fault) is not supported by NetLog, which shows the request never even reached a socket for 127.0.0.1 — pointing at the service-lifecycle/handoff layer rather than a low-level connect path.

Per the WO's decision rule ("If no defect proven → STOP and document the exact unfinished phase/required callback + smallest targeted semantic check"), this WO **stops here** rather than implementing an unproven fix.

## Smallest targeted semantic check (recommended follow-up, NOT executed in this one-run WO)

Isolate network-service stability from the full held-handle navigation stack:
1. On the same owned VM/prefix, run a loopback HTTP server and issue a **single** document request to `127.0.0.1` via a minimal Chromium invocation (e.g. `--headless=new --dump-dom http://127.0.0.1:<port>/`) with **only** `--log-net-log` (no startup tracing, no VLOG).
2. Assert two things:
   - **(a)** the network service process does **not** crash/restart during the request window (watch for "Network service crashed or was terminated"); and
   - **(b)** a `URLRequest` for `127.0.0.1` appears in NetLog and reaches a terminal phase with HTTP 200.
- If **(a) fails** even for a trivial loopback request → a Wine network-service stability defect is isolated; enable a crash dump / `WINEDEBUG=seh` to capture the missing exception, then chase it.
- If **(a) holds** but the full held-handle navigation still stalls → the stall is higher in the stack (renderer/mojo/commit), not the network service.

This would be a **new, separate bounded probe** (its own work order); it is deliberately *not* part of this WO's single run.

## Quality gates

- **Zero whitespace:** `git diff --check HEAD~1` to be run on commit (see delivery section).
- **Clean graceful QMP shutdown:** CONFIRMED — `stop_reason=signal`, `graceful_powerdown=true`, QMP `capabilities_ack=true`, `powerdown_ack=true`, `guest_shutdown_observed=true` (event `SHUTDOWN {guest:true, reason:"guest-shutdown"}`), `protocol_complete=true`, `qemu_exit_status=0`, `fallback_used=false`, cleanup `qemu_stopped=true` + `xvfb_stopped=true`.
- **No OOM:** SwapTotal=0; browser peak ~480 MiB (well under the 8 GiB allocation).
- **Cleanup complete:** guest-side `cleanup_complete=true`, `remaining_servers=[]`, `remaining_prefix_processes={}`; host-side QEMU + Xvfb stopped.
- **Collection success vs page acceptance:** reported separately above (collection = success; page acceptance = no).

## Constraint compliance (this WO)

- ONE fresh run only (`chromium-navigation-phase-20261028`); no identical second diagnostic, no unrelated API-completeness matrix.
- 8192 MiB RAM kept; candidate Wine build reused — **no Wine rebuild**, **no Wine/compositor pin change**.
- Sandbox/security flags, loopback HTTP fixture, and VirGL kept identical to the prior runs.
- No all-category/system/global process sampler, no remote debugger/attachment/suspension, no custom tracing framework, no full Chromium build. Default NetLog capture only (no sensitive/TLS payload). Trace data that flushed was in protobuf form and is reported as such; nothing was silently dropped.
- All private storage on D:-backed Debian; no other VMs touched; no shared loaded builds mutated; no master merge; no download/reinstall.

## Artifacts (run dir `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-navigation-phase-20261028`)

| Artifact | Size (B) | Note |
|---|---|---|
| `chromium-native-evidence/netlog.json` | 577,749 | in-progress format; 3219 events; ~36 ms burst then silence |
| `chromium-native-evidence/trace.json` | 9,994,232 | startup trace, **protobuf** (not JSON); flushed at 35 s |
| `chromium-native-evidence/chromium.log` | 76,366 | scoped VLOG; 481 lines; network-service crash line present |
| `chromium-native-evidence/native-exit.jsonl` | 1,571 | bound + timeout (WAIT_TIMEOUT @45 s), no exit |
| `chromium-native-evidence/results.json` | ~361 KB | verdict + file inventory; `observation_complete=false` |
| `chromium-native-evidence/terminal-front.png` | 9,825 | front capture (content not visually inspected this session) |
| `results.json` (top-level) | — | QEMU/QMP orchestration summary; graceful shutdown confirmed |
| `execution.json` | ~789 KB | full orchestration record; `orchestration_complete=true`, `consumer_ssh_exit=1` |

## Bottom line

The loopback navigation stalls at a **precisely identified phase**: past throttle/beforeunload/start-checks and past URLLoader start (`ThrottlingURLLoader::StartNow`), it blocks waiting for the network service to deliver the document response — `NavigationRequest::OnReceiveResponse` (and all commit phases) never fire. NetLog shows the document was never even issued as a `URLRequest` (only a speculative, cached host-resolver job for 127.0.0.1), and chromium.log shows a **one-time network-service crash/restart ~288 ms after nav start with no captured cause**. No small real Wine defect is proven by the available evidence, so per the WO this run **stops and documents** the exact unfinished phase, the required callback, and the smallest targeted semantic check to isolate a network-service-stability defect in a follow-up.
