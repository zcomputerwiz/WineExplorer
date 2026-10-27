<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Network-service bootstrap diagnostic — result (WO-20261027-CHROMIUM-NETWORK-BOOTSTRAP)

**Branch:** `codex/chromium-network-bootstrap` (HEAD `8ef02e48`, branched from resource branch `97922ec5`)
**Wine base (retained, unchanged):** `codex/g4-token-integration` @ `fffa7de` (combined candidate build, wine-11.19)
**Wine child branch:** none created — Option B required no instrumentation/fix to implement in this order
**Date:** 2026-10-27

## Summary

Executed the "otherwise" branch of the one-trial rule: **one** targeted fresh bootstrap
diagnostic with genuinely new status/IPC evidence, then report the smallest supported fix.
No code change was made; the retained combined Wine build and the full compatible Chromium
runtime were reused unchanged. The single counted trial launched `chrome.exe` directly
(baseline flags plus a `--vmodule` verbosity variable) against the original local HTTP fixture
over real loopback with actual VirGL, in a fresh named output dir on the owned R4 disk.

**Operative finding:** the out-of-process network service was spawned **exactly once**
(PID 700), immediately after the browser's `network_service_instance_impl.cc:652`
"restarting service" ERROR, and stayed alive and functional for the whole run (it serviced
background googleapis/google/edgedl requests, each failing with an air-gapped error).
**The fixture page's navigation request was never dispatched to that network service** — there
is no `NotifyBeforeURLRequest` for the fixture host `127.0.0.1:41821` anywhere in the Chromium
log, and the local fixture server received zero requests. ~17.7 s later the tab was killed with
a Sad Tab. The failure is therefore in the **renderer → out-of-process network-service request
handoff for the initial navigation during a startup network-service re-establishment**, not in
process creation, a policy kill, or native socket I/O.

## 1. Corrections to the prior resource-pressure report

Per the parent directive, these overstatements are corrected here with no extra memory test cycle:

- **3428 MiB is GREATER than 2 GiB.** The stated early-exit predicate ("available commit <
  2 GiB") is therefore **TRUE** (the probe would early-exit), not false as previously written.
- **3428 MiB is BELOW both bracketing `MemAvailable` samples (3600 / 3453 MiB), not between
  them.** Different sample times / Wine warmup can account for a small difference; the arithmetic
  does not prove an exact mapping at the same instant.
- Idle-state measurements **do not conclusively exclude load-dependent pressure**, but they
  provide no positive reason to pursue memory instead of actual bootstrap. The candidate API
  success + ~3428 MiB available-commit + 12% load is an idle-state measurement only; it is not a
  measurement of Chromium's evaluator during the failing run.
- The prior journal wait was **45000 ms, not 220 s** (220 was a Windows PID).

## 2. Pinned-source reading of the `:652` line (not a proven process death)

Per the parent's read of the exact pinned source (`7d98240`,
`content/browser/network_service_instance_impl.cc` lines 629–662): the re-establish block is
entered when the remote is not bound or not connected; it captures
`service_was_bound = remote().is_bound()`; in the out-of-process branch, **line 652** logs
`"Network service crashed or was terminated, restarting service."` immediately before
`ServiceProcessHost::Launch`. This line is **not** a captured crash signature and does **not** by
itself prove a prior process died — it indicates a bound-but-not-connected remote. No faulting
PC/status has been established. This result treats the line exactly that way and does not label
the earlier ~0.9 s message a proven process death.

## 3. Actual process / IPC evidence (directly observed)

Evidence files (D:-backed, private):
- `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-network-bootstrap-20261027\guest-evidence\chromium-network-bootstrap-evidence.tar.gz`
- extracted: `ac_build\nb_evidence\chromium-network-bootstrap-20261027\{chromium.log, wine-trace.log, run-summary.json, wexcomp.log, wayland-info.log, native-egl.stdout.log}`

Timeline (Chromium wall-clock unless noted; the Wine `+process` trace uses its own seconds clock):

| t | source | event |
|---|--------|-------|
| 14:06:42.786 | chromium.log:121 | fixture navigation registered — data_protection "URL to scan: http://127.0.0.1:41821/…/index.html#nonce=…" |
| 14:06:42.871 | chromium.log:131 (also wine-trace:655) | **the single** `network_service_instance_impl.cc:652` "Network service crashed or was terminated, restarting service." (~85 ms after nav) |
| 50.138→50.176 s | wine-trace:661/664/669-670 | **the single** `network.mojom.NetworkService` spawn: `CreateProcessInternalW … --type=utility --utility-sub-type=network.mojom.NetworkService` → `NtCreateUserProcess` → "started process pid 02bc tid 02c0" = **PID 700** |
| ~14:06:43.3 onward | chromium.log (PID 700) | network service connected; services background `update.googleapis.com`, `www.google.com/async/folae`, `accounts.google.com`, `edgedl.me.gvt1.com` requests — each failing with `net_errors_win.cc:121 "Unknown error 10042 mapped to net::ERR_FAILED"` (air-gapped) |
| 14:07:00.563 | chromium.log:371 | `sad_tab.cc:257 Tab Killed: http://127.0.0.1:41821/` — ~17.7 s after nav |

**Key direct observations:**

1. **Exactly one network-service process.** The Wine `+process` trace begins at
   `wineboot.exe --init` (wine-trace:1, t=46.081 s) and captured every child spawn for the whole
   session. It records exactly one `network.mojom.NetworkService` spawn (→ PID 700), immediately
   after the `:652` line. Full child inventory: crashpad-handler ×2 (~47.17 s), gpu-process
   (~49.0 s), storage.mojom.StorageService (~49.085 s), renderer ×2 (~49.86 s),
   **network.mojom.NetworkService (50.138 s → PID 700)**, extension renderer (~50.67 s),
   chrome.mojom.ProcessorMetrics (~51.19 s), repeated unzip.mojom.Unzipper (component updates).
2. **The network service did not crash or get killed.** PID 700 is still emitting log lines at
   the end of the run (14:07:58) and returned real (failing) results for its background requests —
   a live, connected, functional process.
3. **The fixture request was never dispatched to the network service.** Every
   `NotifyBeforeURLRequest` in chromium.log (lines 255–534) is from PID 700 and targets only the
   background googleapis/google/edgedl hosts. There is **no** `NotifyBeforeURLRequest` for
   `127.0.0.1:41821`, and the fixture server's request record is empty (`fixture_requests=[]`).
4. **The tab ended in a Sad Tab** (chromium.log:371), ~17.7 s after the navigation was registered —
   i.e., the page load timed out rather than failing with a specific net error for the fixture host.

## 4. Mapping to the four candidate failure modes

- **(c) child process termination / policy kill — ruled out.** The sole network-service process
  (PID 700) stayed alive and served traffic to the end; no policy kill is observed.
- **(d) live process whose channel never connects — ruled out for the running instance.** PID 700's
  Mojo channel connected: it processed background requests and produced real error responses.
- **(a) first network service disconnected before launch / (b) renderer failed to reach the
  network service over Mojo — consistent with the evidence.** The `:652` line indicates a
  bound-but-not-connected remote at re-entry, and the fixture request never reached the connected
  service.

## 5. Specific remaining failure

The initial fixture navigation's network request was **not delivered to the out-of-process network
service** and was not retried once PID 700 connected; the renderer waited ~17.7 s and the tab was
killed with a Sad Tab. This occurred in the context of an early startup network-service
re-establishment (`:652`). The failure sits **above native socket I/O** (the network service's
Winsock path is active, returning real errors) and is not a process-creation or policy-kill problem.

## 6. Residual uncertainty (honest limitations)

- **`--vmodule` did not take effect.** There are **zero `VERBOSE2` lines** in chromium.log despite
  `--vmodule=*service_process_host*=2,*network_service_instance_impl*=2,*mojo/core/channel*=1,*mojo/edk/port*=1,*navigation_request*=2,*render_frame_host_impl*=1`; only the global `--v=1` was
  active (VERBOSE1 lines such as `NotifyBeforeURLRequest` appear). Either the vmodule value was not
  honored through the wine→chrome command-line reconstruction, or those modules have no level-2
  statements on the specific paths exercised here (the re-establishment path logs via
  `LOG(ERROR)`, which did appear). **Either way, level-2 mojo/navigation handshake detail is absent
  from this evidence**, so I cannot state whether the initial navigation's URLRequest was dropped at
  dispatch vs. hung waiting on an unconnected remote.
- **Wine `NtTerminateProcess` lacks TRACE.** Per the directive ("absence from a trace without the
  corresponding tracepoint does not exclude a call"), the absence of a terminate line does not by
  itself exclude a termination. However, because the `+process` trace captured every *spawn* for the
  whole session and shows only one network-service spawn, an earlier first-launch network-service
  **process** (which would have produced a captured `CreateProcessInternalW`) is not present — so the
  bound-but-not-connected remote that triggered `:652` did not correspond to a separate, still-traced
  utility process. The most consistent reading: the initial launch had bound its Mojo pipe but the
  process spawn/connection had not completed when `GetNetworkService()` was re-entered (prompted by
  the fixture navigation), and the re-establishment produced the single observed PID 700. This does
  not change the operative finding in §5.

## 7. Native / HTTP HTML+JS / readiness / front outcome, and shutdown

- **Native:** The fixture is served over real in-guest loopback (`127.0.0.1:41821`); the network
  service's background requests also used the native Winsock path (hence real Windows error 10042).
  Native socket I/O is exercised and functional; the failure is above it.
- **HTTP HTML+JS:** Original fixture = `index.html` + `scene.js` served by `FixtureServer` on a
  per-run nonce path (`/898d…/index.html#nonce=898d…`).
- **Readiness:** Fixture server `start()` returned the URL before chrome launch (verified in
  preflight and re-gated in run); VirGL GL verified (`gl_verify` exit 0); owned labwc compositor up
  (pid 855). Chrome was not launched until all gates passed.
- **Front outcome:** Sad Tab — the page never rendered; tab killed at 14:07:00.563.
- **Shutdown:** Owned shutdown retained — SIGTERM the whole process group, escalate to SIGKILL on
  timeout, then `wineserver -k` (scoped to our prefix), stop owned compositor, close fixture.
  `chrome_exit_code=-15` (SIGTERM by driver), `chrome_ran_seconds=92.1`. VM stopped via QMP graceful
  powerdown (owned launcher).

## 8. Smallest supported fix / next step

The evidence localizes the failure to the renderer→network-service request handoff for the initial
navigation during a startup re-establishment, but does **not** identify a single small Wine function
defect to change, and no fix can be verified without a second run (this order's one-trial budget is
consumed). The smallest **supported** next step is a follow-up diagnostic that captures the missing
level-2 mojo/navigation handshake:

1. Make `--vmodule` effective through the wine→chrome command-line path (confirm `VERBOSE2` lines
   actually appear before trusting it), and/or
2. Add a narrow, semantically-inert Wine tracepoint on the network-service Mojo connect +
   `NtTerminateProcess` exit status (on an OWN child branch + incremental OWN build; no shared/loaded
   build modified).

That will determine whether the minimal fix belongs in **Wine** (reliable/fast utility spawn + Mojo
connect so the initial navigation is not issued against an unconnected remote) or is a **Chromium
navigation-retry gap**. This is a second diagnostic and requires parent sign-off. No code change, no
Wine child branch, and no modification to the retained build were made in this order.

## 9. Artifacts & reproducibility

- Guest output dir (R4 disk): `/home/debian/wex-g4/chromium-network-bootstrap-20261027/`
- In-guest driver: `ac_build\nb_bootstrap_run.py` (drives chrome.exe directly; reuses
  OwnedWexcomp/VirGL/FixtureServer + `install.environment` logic)
- VM run dir (transient boot logs only):
  `D:\VirtualBox_VMs\WEX-G4-VirGL-20261022\chromium-network-bootstrap-20261027-b2\`
- All runtime/private logs remain D:/D-backed WSL; nothing secret/vendor/legal is committed.
