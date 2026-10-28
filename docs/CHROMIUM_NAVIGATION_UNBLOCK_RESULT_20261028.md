<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium navigation-to-network boundary — result (bounded NetLog diagnostic)

One fresh headful Chromium run at **8192 MiB** guest RAM, identical to the 8 GiB
control-channel baseline except for a single bounded, logging-only addition:
`--log-net-log` (Default capture, 8 MiB cap, 40 s duration) on the browser child.
The page **still does not load**: the private loopback fixture server received
**zero** requests for its entire ~46 s lifetime, and the complete captured NetLog
contains exactly **one** event tied to the target host — a *speculative*
host-resolution request that never escalated to a document fetch. The navigation
reached the network layer's host resolution but its document URLRequest was never
dispatched, coincident with a network-service termination at navigation time.

Per the work order this is a **STOP-and-document** result: the evidence does not
prove a specific small Wine defect, so no Wine change (including the P0-flagged
`ProcessHandleCount` stub) was made and no feature switch / proxy bypass / memory
cap was added. The exact last-successful / first-failing boundary and the smallest
justified next step are recorded below.

## Single-variable change (logging only)

The browser child's command line gained three bounded NetLog flags over the 8 GiB
baseline (recorded verbatim in `results.json` `journal_outcome.bound.command_line`):

```text
--log-net-log=Z:\...\chromium-navigation-unblock-20261028\netlog.json
--net-log-max-size-mb=8
--net-log-duration=40s
```

This is **Default** capture only — no `--net-log-capacity`, no sensitive/TLS-key
include, no remote debug port, no exposed UI. Everything else (candidate, fixture,
private loopback, VirGL runtime, 8192 MiB RAM, security) is unchanged from the
8 GiB control-channel baseline. Fresh run name `chromium-navigation-unblock-20261028`;
fresh stage/output/profile/helper paths; no evidence or candidate deletion, no
consumed-path reuse.

## Candidate reuse (no rebuild)

Reused the last actually deployed matched full combined Wine candidate from the
control-channel run — **02d4ef2 diagnostic lineage over fffa7de** — at
`/home/debian/wex-g4/wine-control-channel-20261027/build/wow64`. The consumer's
actual argv confirms it: `helper_argv[0]` is that exact `wine` binary. No Wine
rebuild and no new trace deployment occurred.

## Actual QEMU argv (recorded in execution.json)

```text
qemu-system-x86_64 -accel kvm -machine q35 -cpu host -smp 4 -m 8192
  -device virtio-vga-gl -display gtk,gl=on
  -drive file=.../system-native-identity-r4-20261024.qcow2,format=qcow2,if=virtio
  -drive file=.../seed.iso,format=raw,media=cdrom,readonly=on
  -nic user,model=virtio-net-pci,hostfwd=tcp:127.0.0.1:2235-:22
  -serial file:.../chromium-navigation-unblock-20261028/serial.log
  -qmp unix:.../isolated-sockets/virgl-runtime-dkrsxppz/qmp.sock,server=on,wait=off
  -no-reboot
```

`-m 8192` is present. Full argv retained in the private run dir.

## Guest memory (before and after the consumer)

Compact meminfo sample at start/end of the browser interval (`execution.json`):

| field | before consumer | after consumer |
|---|---|---|
| MemTotal | 8138032 kB (~7.76 GiB) | 8138032 kB |
| MemFree | 6994184 kB | 6523960 kB |
| MemAvailable | 7772052 kB | 7757476 kB |
| SwapTotal | 0 kB | 0 kB |
| SwapFree | 0 kB | 0 kB |

`free_before_stage = 7827316736` bytes (~7.3 GiB). MemFree fell by ~459 MiB
(470,224 kB) across the whole consumer interval — modest, no exhaustion, no swap.

## Kernel OOM: none

The retained `serial.log` contains **no** oom-killer invocation, no "Killed
process", and no "out of memory". Same as the 8 GiB baseline — the renderer OOM
that killed CrRendererMain at 4 GiB does not reproduce here. This is not a memory
failure; it is a navigation/network failure.

## Chromium log: network-service lifecycle at navigation

`chromium-native-evidence/chromium.log`, wall-clock timestamps (browser PID 220):

- **12:20:49.953** — `data_protection_navigation_observer.cc:261`:
  `URL to scan: http://127.0.0.1:40583/79e4.../index.html#nonce=79e4...`
  (lines 121–122). The browser process committed the navigation to the target.
- **12:20:50.059** — `network_service_instance_impl.cc:652`:
  `Network service crashed or was terminated, restarting service.` (line 132).
  ~106 ms after the navigation observer fired.
- **12:20:50.592** — restarted network service begins logging (lines 255–257).

No FATAL, CHECK failure, SIGSEGV, or stack trace for the target path appears in
the log. The only signal is the network-service termination/restart at navigation
time.

## NetLog evidence (the core of this work order)

Capture: a single browser-owned NetLog directory `scoped_dir720_972991671`
(constants + event file, 3187 events) was written to the guest temp dir. The
browser child was killed at the 45 s deadline **before** NetLog stitched on stop,
so the run-dir `netlog.json` is a **383-byte un-stitched placeholder** ("Logging
is in progress writing data to: ...scoped_dir720_972991671"). The raw scoped-dir
files were retrieved from the guest while QEMU was still up and parsed; they hold
all 3187 events spanning NetLog t=48998→86657 ms (~37.6 s of active capture).

The logger was **active** throughout this window: it recorded 18 `URL_REQUEST_START_JOB`,
36 `HTTP_STREAM_JOB`, 136 `SOCKET_CONNECT`, and 16 `HOST_RESOLVER_MANAGER_REQUEST`
events for *other* hosts (background component/IPv6-reachability traffic). Within
that active window, exactly **one** event references the target host/port:

```json
{"params":{"allow_cached_response":true,"dns_query_type":"UNSPECIFIED",
  "host":"http://127.0.0.1:40583","is_speculative":true,
  "network_anonymization_key":"http://127.0.0.1 same_site","secure_dns_policy":0},
 "phase":1,"source":{"id":15,"start_time":"49020","type":43},"time":"49020","type":5}
```

- `host` = the target (`http://127.0.0.1:40583`)
- **`is_speculative: true`** — a *speculative* (pre-fetch) host resolution, not one
  driven by an actual URLRequest
- type 5 = `HOST_RESOLVER_MANAGER_REQUEST`, source type 43 = network-service host resolver

There is **no** `URL_REQUEST_START_JOB`, `HTTP_STREAM_JOB`, or `SOCKET_CONNECT`
anywhere in the capture that references `127.0.0.1:40583`. Because the same active
logger recorded those event types for other hosts in the same window, their absence
for the target is meaningful (not an inactive-logger gap): the target's document
request was never dispatched to the network stack during the captured interval.

**Independent ground truth:** the private loopback fixture server's own request log
is empty for its whole life — `http_fixture.requests = []`, `errors = []`
(`results.json`). The listener (127.0.0.1:40583) ran from monotonic 44.9 s to 91.3 s,
spanning the entire browser lifetime; it received **zero** HTTP requests. This holds
regardless of NetLog stitching.

## The exact boundary

- **Last successful step:** the browser committed the navigation (nav observer at
  12:20:49.953) and the network layer issued a *speculative* host resolution for the
  target host `127.0.0.1:40583` (NetLog t=49020). Loopback resolution is trivially
  satisfiable, so this is not where it stalls.
- **First failing step:** the actual document URLRequest for the target was never
  dispatched — no URLRequest job, HTTP stream job, or socket connect to the loopback
  server — and this coincides with the network service terminating at navigation time
  (12:20:50.059) and not completing any fetch for that host before or after its restart
  within the 37.6 s capture window. The fixture server confirms no connection ever
  completed.

Caveat per the work order: a missing event in an *unflushed/inactive* logger would be
"unknown, not proof." Here the logger was active and captured analogous events for other
hosts, and the server-side request log independently shows zero requests — so the
boundary (speculative resolution reached; document fetch never dispatched) is well
supported. What is **not** established is *why* the dispatch did not happen: no FATAL /
stack trace / specific failing syscall is tied to it in this capture.

## Consumer outcome — page did NOT load

`results.json`:

- `journal_outcome.outcome`: **native_timeout**
- bound: helper_pid 32 → child_pid **220**, image `chrome.exe`
  (`chromium-headful-r1-20261023-stage/vendor/extracted/chrome-win`),
  `--app=<fixture>`, tick_ms 45943
- terminal: event **timeout**, child_pid 220, wait_result **258** (WAIT_TIMEOUT),
  wait_ms 45000 — the browser child was still running at the 45 s mark
- `accepted_observation` false; `manifest_marker_verified` false
- `http_fixture.requests`: **[]** — the page was never fetched
- fixture nonce `79e407bcd7ae23fbb06f0b9c0999b5ec`, listener 127.0.0.1:40583, URL
  `http://127.0.0.1:40583/79e4.../index.html#nonce=79e4...`
- renderer string `virgl (D3D12 (Intel(R) UHD Graphics 750))` — GPU/renderer path is
  fine, same as the 8 GiB baseline; this is not a rendering failure
- consumer-side `error`: `ValueError('Linux helper failed/exit mismatch')` — a
  non-fatal journaling artifact of the held-handle timeout (the Linux wine helper PID
  1000 exited code 1 at the deadline), matching the accepted pattern from the 8 GiB run.

## Acceptance mapping — collection success vs page-load outcome (reported separately)

**Collection / instrumentation success: YES.**

- NetLog capture produced a complete, parseable active window (3187 events, ~37.6 s);
  the single target event was isolated and its type names resolved from constants.json.
- Fixture index.html (637 B) + scene.js (3750 B) present with recorded sha256; private
  loopback listener up for the whole interval.
- Guest readiness fired (`readiness.ready = true`, cloud-init done, systemd running).
- Viewed front captured (`chromium-native-evidence/terminal-front.png`), `front_exit 0`.
- Clean owned shutdown: `cleanup_kill 0`, `cleanup_wait 0`, `remaining_servers []`,
  `remaining_prefix_processes {}`; QEMU powered down cleanly (state=exited, no orphan).

**Page-load outcome: NO.**

- The normal page did not load. The fixture server received zero requests; the target's
  only network trace is a speculative host resolution; its document URLRequest was never
  dispatched. This is a navigation/network-service boundary failure, not an OOM, GPU, or
  rendering failure.

Per the work order these are distinct: the diagnostic instrumentation worked and the
evidence was collected successfully; the page it set out to load did not.

## Wine-level signals observed (not attributable to a single defect)

Observed in the logs but **not** tied to a specific failing call on the target path, so
none is treated as a proven small defect:

- `fixme:process:NtQueryInformationProcess ProcessHandleCount (...) stub` — a real
  compatibility gap (the parent review flagged it P0), but `CloseHandles` allocates
  count+1000, so a returned 0 is not automatically fatal; no crash on this path is shown.
- `WSALookupServiceBegin failed with: 8` — Winsock network-change notification LUID
  probe, non-fatal, present in both the browser and the restarted network service.
- `RPC_S_SERVER_UNAVAILABLE` structured exceptions (code 6ba), a handful total, in
  threads not mappable to the network service.
- `OutputDebugString` SEH — benign.

None of these produces a FATAL / stack trace / specific failing syscall on the target
navigation path, so none is promoted to a fix in this work order.

## Decision: STOP-and-document (no Wine change)

The evidence does not prove a specific small Wine defect responsible for the missing
document fetch. Per the work order, therefore:

- **No** `ProcessHandleCount` patch was made (not fatal on its own; no crash shown).
- **No** speculative API / sandbox / network-service feature switch, proxy bypass, or
  browser memory cap was added.
- No Wine/compositor pin update; the reused candidate is unchanged
  (`outside_modules_unchanged = true`, `candidate_reuse_valid = true`).

## Smallest justified next step (documented, not executed)

Convert "Network service crashed or was terminated" into a concrete crash signature for
the **first** network service: re-run with child-process stderr logging enabled
(`--enable-logging=stderr`) and/or scope `WINEDEBUG` around the network-service PID so
the termination at navigation time yields a specific FATAL line, SEH code, or last
syscall. Only once that signature is captured can a Wine-level cause (or its exclusion)
be established. This stays within the same bounded diagnostic budget; it adds no feature
switches and changes no candidate.

## Cleanup and shutdown

- `front_exit 0`; `cleanup_kill 0`; `cleanup_wait 0`
- `remaining_servers []`; `remaining_prefix_processes {}` — no leftover owned processes
- `outside_modules_unchanged = true` (reused candidate byte-identical)
- `archive_exit 0`; `chromium-native-evidence.tar.gz` written
- QEMU exited cleanly via its bounded launcher session-timeout powerdown
  (launched 12:19:48Z, exited 12:34:58Z; state=exited, no orphan process). The guest
  was unreachable by SSH afterwards only because the VM had already powered down — not a
  fault in this run. A crashpad minidump for the first network service could therefore
  not be retrieved (out of scope for the bounded diagnostic; see next step above).

## Files changed and where they live

Committed on `codex/chromium-navigation-unblock`:

- `scripts/g4_native_exit_contract.h` — added the bounded NetLog child args under a
  `WEX_NETLOG` guard (`--log-net-log`, `--net-log-max-size-mb=8`,
  `--net-log-duration=40s`).
- `scripts/check-g4-chromium-native-exit.py` — added a `navigation-unblock` mode (host
  run name `chromium-navigation-unblock-20261028`; BUILD override to the reused
  candidate; browser-only consumer).
- `docs/CHROMIUM_NAVIGATION_UNBLOCK_RESULT_20261028.md` — this report.

Local recipes under gitignored `build/` (not committed, matching every prior g4 trial —
nothing under `build/` is tracked in this repo):

- `build/g4-virgl-20261022/chromium-navigation-unblock-trial.py` — per-mode orchestrator
  (prepare/run + budget guards), reusing the 8 GiB launcher.

The `wine` submodule working-tree pin change in this worktree is left untouched and not
committed (no Wine/compositor pin update). Private run evidence stays in
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-navigation-unblock-20261028/`
(not committed); the parsed NetLog scoped-dir and analysis live under
`D:/DeepThought/Projects/OpenClaude/ac_build/navunblock_netlog_scoped/` (scratch, not
committed).
