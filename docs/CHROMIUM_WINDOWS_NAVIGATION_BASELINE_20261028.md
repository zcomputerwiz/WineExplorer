<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Native Windows Chromium navigation baseline — 2026-10-28

## Result

The one authorized native run **did not load the page**. It is a failed native
comparator, not a successful reference graph and not evidence of Wine correctness
or a Wine defect. The original loopback server received zero requests; neither
HTML nor JS was fetched and no `WEXEDGE` nonce readiness title appeared. The app
window remained titled `127.0.0.1_/.../index.html` through the observation.
The target navigation started, progressed into the URL loader, then finished with
`net_error=-3`, `has_committed=false`. No target response, document commit or
first paint is established. The browser stayed alive until the end of the
45-second observation; the navigation failure was recorded at startup, not
inferred from that wait.

The log records `sandbox_win.cc:805` at 09:04:45.291 local: sandbox cannot
access the cached executable, Windows access denied (5). Later histogram output
records one `Chrome.SystemNetworkContextManager.NetworkSandboxLaunchFailed`,
error code 45 and WinLastError 5, and one utility launch failure. At 09:04:45.510
the browser logs `network_service_instance_impl.cc:652` saying the network service
crashed or was terminated and is restarting. This combination is an actionable
native environment/launch clue; the timestamps alone do not prove the exact
causal chain to the target abort. In particular, the 652 text alone does not prove
an OS process died. A later network service is identifiable from trace metadata,
but no distinct first network-service PID and terminal record are established.
The work order says stop on page failure; no retry, ACL change, extraction,
configuration change or sandbox bypass was attempted.

## Provenance and collection

- Parent `1b47a9e2`; own branch `codex/chromium-windows-navigation-baseline`.
  The existing clean R5 preparation checkout was switched without cleaning or
  resetting; old branch, ignored assets and submodules were preserved.
- Cached `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-1706509-r1-20261023.cache/runtime/chrome-win/chrome.exe`;
  actual file/product version `156.0.8078.0`, SHA-256
  `c84cd1e7ce7865ccc13bf00f33a3cb934d293c38a1a106585de3d3077c888c99`.
  Cached REVISIONS identifies snapshot 1706509 and source
  `7d9824099030f5c28c939ba08654562faaad7c09`.
- Original `scripts/g4-edge-probe/index.html` (637 bytes, SHA-256
  `f0a47b1a6013c015228c3e6c76a0a73b5d3eb6c218ec031a34dd8da7e0c0edc3`)
  and `scene.js` (3750 bytes, SHA-256
  `fe9b1801a7d4baa3512fb4e103330e317d17c9f2e2763abf8f14a275c686ee51`)
  were served by the existing `FixtureServer`, bound to ephemeral
  `127.0.0.1:54342`. The fresh nonce was
  `5a48910f688f70580697f2a40d27700c`; URL uses `/<nonce>/index.html#nonce=<nonce>`.
- Original app/profile, no-first-run, no-default-browser-check, logging and
  `--v=1` arguments were retained. Added logging only:
  `--trace-startup=navigation,loading,net,ipc,renderer.scheduler,blink.user_timing,benchmark`,
  `--trace-startup-duration=35`, `--trace-startup-format=json`,
  `--trace-startup-file=<private trace.json>`, `--log-net-log=<private netlog.json>`,
  `--net-log-capture-mode=Default`, `--net-log-max-size-mb=8`, and
  `--net-log-duration=40`. No SSL key capture, debugger, remote debugging,
  sandbox, GPU, renderer-limit or feature override was used.
- Pinned [tracing switches](https://raw.githubusercontent.com/chromium/chromium/7d9824099030f5c28c939ba08654562faaad7c09/components/tracing/common/tracing_switches.cc)
  define startup categories, duration, file, JSON format, controller default and
  bounded record-until-full default. Pinned [network switches](https://raw.githubusercontent.com/chromium/chromium/7d9824099030f5c28c939ba08654562faaad7c09/services/network/public/cpp/network_switches.cc)
  define Default mode, max size and positive integer seconds; the pinned
  [network service implementation](https://raw.githubusercontent.com/chromium/chromium/7d9824099030f5c28c939ba08654562faaad7c09/content/browser/network_service_instance_impl.cc)
  parses and passes these to StartNetLog.
- Started 2026-10-28 14:04:44.832 UTC. JSON trace (6,224,126 bytes,
  40,908 events) and NetLog (4,248,337 bytes, 33,368 events) both parse; file
  modification times were 40.89 and 40.78 seconds after launch, before the
  45-second observation ended. Trace timestamps span 399328198393–399363000621
  microseconds on its monotonic clock. A bounded buffer and the network-service
  restart limit absence claims; early pre-restart NetLog coverage is not proven.

## Target navigation timeline

Trace timestamps below are raw monotonic microseconds, not wall-clock times.
The URL-bearing constructor and `DidFinishNavigation` both identify navigation
ID 1, frame tree node 1. Browser OS PID 36936, UI TID 19656. The navigation
async track exported as PID/TID 7352 and local ID `0xb` is a trace track; it is
not by itself proof that each event executed in renderer process 7352. Trace
metadata names OS PID 7352 `Renderer`, and target frame metadata later includes
`child_process_id: 7352`; its separate actual creation identity is in the private
observation. Chromium internal process ID 5 is not a Windows PID.

| Time (µs) | Event or evidence | Interpretation |
| --- | --- | --- |
| 399328332463 | `NavigationControllerImpl::LoadURLWithParamsForResult` | Exact target URL |
| 399328332572–399328334134 | `NavigationRequest::NavigationRequest` | Target ID 1 constructed |
| 399328332639–399328348316 | track `Initializing` | Initialization ends |
| 399328348313 | `NavigationRequest::BeginNavigation` | Start of navigation |
| 399328348706 | `WebContentsImpl::DidStartNavigation` | URL-bearing observer, state 3; not a commit |
| 399328349675 | `NavigationRequest::WillStartRequest` | Track enters request-start phase |
| 399328351010 | `NavigationRequest::OnStartChecksComplete` | Start checks callback |
| 399328351319 | `NavigationURLLoaderImpl::Start` | Browser-side loader begins |
| 399328351770–399328352649 | `CreateThrottlingLoaderAndStart`, `ThrottlingURLLoader::Start` request ID -2, `StartNow` | Handoff attempted; not a network response |
| 399328421299 | `NavigationURLLoaderImpl::MaybeCreateLoaderForResponse` | No target HTTP response established |
| 399328442668–399328442800 | `NotifyRequestFailed`, `OnRequestFailedInternal`, `DidFinishNavigation` | Target ends `net_error=-3`, `has_committed=false` |
| 399328442754–399328442765 | track `OnRequestFailed`, then `NavigationRequest` ends | Failure, no successful commit |
| 399328518–399328521 ms | NetLog source 12 `HOST_RESOLVER_MANAGER_REQUEST` | Speculative `127.0.0.1:54342`, after failure; not target URLRequest |

No event in the retained NetLog mentions the nonce or a target document
URLRequest; only the speculative host resolution mentions its port. The fixture
independently reports no received requests. The target renderer's readiness,
document commit and first paint or presentation are not established. The observed
native sequence reaches `StartNow`, whereas the parent Wine report describes no
fixture requests and one speculative resolution; that similarity is not proof
the underlying causes or earlier phases match.

## Identity, cleanup and environmental limits

The launcher retained process handles and creation FILETIME for observed owned
descendants. Browser PID 36936 creation FILETIME `134376698848337761`; target
renderer PID 7352 `134376698854252792`; later network-service PID 49960
`134376698855127788`; GPU PID 38332 `134376698852771735`. Trace metadata supplies
the latter roles, not counts or launch order. After observation the launcher
posted close to the owned browser's windows and waited on held handles: all 16
observed processes exited, wait result 0, exit code 0, no forced termination;
the fixture thread stopped with no errors. Polling can miss a very short-lived
child, so this does not assert a complete creation/terminal history. The helper
itself completed and was reaped by the invoking command. No broad kill was used.

Host video inventory reports NVIDIA GeForce RTX 3090 (driver 32.0.16.1742) and
Intel UHD Graphics 750 (32.0.101.7092); it does not establish which adapter this
failed page would have rendered on. The native host has ordinary networking and
background traffic; the Wine guest uses a private-loopback VirGL environment.
No global network settings were changed. Their GPU, network and filesystem
environments are not equivalent. Neither this failed baseline nor a hypothetical
native pass proves Wine graphics or API behavior.

All private evidence, profile and the exact one-run launcher remain under
`D:/DeepThought/ChromiumLab/windows-navigation-baseline-20261028/`: `launch.json`,
`run.py`, `fixture.json`, `observation.json`, `chromium.log`, `trace.json`,
`netlog.json`, and `profile/`. Runtime, logs, profile and trace are not published.

For parent review before any forwarding: does the Wine target trace reach the
same `OnStartChecksComplete` → `StartNow` → failure sequence, or remain in an
earlier phase? Does it have independent launch/terminal evidence corresponding to
the network sandbox launch failure, rather than only the 652 remote message?
