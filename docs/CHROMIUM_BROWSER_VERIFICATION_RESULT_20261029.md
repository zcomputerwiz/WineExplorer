<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium browser verification (AppContainer kNetwork fix) — iterations 1 & 2

> **Status across iterations.** Iteration 1 (wine `3a47f7c`) was **NEGATIVE**: the
> kNetwork app-container SID was registered and rolled back ~7 ms later, and the
> network service crashed and restarted. **Iteration 2 (wine `871f6183`, this run) is
> POSITIVE on both acceptance criteria** — `utility!kNetwork` initializes and remains
> under `--service-sandbox-type=network` with no rollback, and the loopback fixture
> loads HTTP 200. See the "Iteration 2" section at the end of this document; the
> iteration-1 narrative below is retained as the historical baseline.

## Iteration 1 (wine `3a47f7c`) — NEGATIVE

WO `WO-20261029-CHROMIUM-VERIFICATION-ITERATION-1`, Verification Run 1. Target
identity `chromium-browser-verify-r1-20261029`. Verified the wine AppContainer
correctness fix (commit `3a47f7c3271b082ab82a10b7861ed7d4a137bb34`, branch
`codex/g4-appcontainer-correctness`) against the two acceptance criteria:

- **Q1** — does `utility!kNetwork` initialize and *remain* under
  `--service-sandbox-type=network` (no `=none` fallback, no crash-restart)?
- **Q2** — does the fixture `index.html` load with HTTP 200?

> **Note on the fix revision.** The WO named wine commit `3a47f7c`. While this run
> was being prepared, zcomputerwiz pushed a follow-up wine commit `9a70d724` and
> advanced the superproject gitlink to it (superproject commit `86eaa298`). Per that
> commit's message, `9a70d724`'s only change is the `include/userenv.h`
> `CreateAppContainerProfile` declaration fix (`PSID*`→`SID**`) — the same one-line
> signature mismatch this build hit and worked around locally (build patch #1 below).
> The verification candidate was built from `3a47f7c` with that identical header fix,
> so it is functionally equivalent to `9a70d724`; the negative result applies to both.

## Verdict

**NEGATIVE. The fix did not resolve the failure.** Both criteria failed, and the
Wine-level trace reproduces Run 1's exact signature: the kNetwork app-container
SID is registered and rolled back 7 ms later, the network service crashes and is
restarted, and the in-flight fixture navigation is dropped before any HTTP
response.

| Criterion | Result | Decisive evidence |
| --- | --- | --- |
| Q1 kNetwork stays `=network` | **FAIL** | `AppContainerRegisterSid` → `AppContainerUnregisterSid` 7 ms apart (Run 1 signature); `Network service crashed or was terminated, restarting service.` |
| Q2 fixture loads HTTP 200 | **FAIL** | navigation began 08:29:14.372; network service died 08:29:14.664; netlog holds only a *speculative* preconnect to the fixture host, and all 3210 events have `http_response_code=None`; outcome `native_timeout` (45 s wait) |

The negative is not a build/deploy artifact: the deployed candidate was proven to
be a byte-faithful representation of commit `3a47f7c` (see below).

## Candidate provenance and completeness

The guest candidate wine was assembled as a hardlink copy (`cp -al`) of the
control-channel candidate, then overlaid with exactly the components that differ
from the fix base, measured live by SHA-256. The run recorded
`candidate_coherent = true` and a differing set of **6 DLLs** —
`{kernelbase, ntdll, userenv} × {x86_64, i386}`. `wineserver` was *not* in the
differing set because the control-channel candidate's `wineserver` was already
byte-identical to the fix base (`6c063646…`), and the control-channel source
`wineserver` was witnessed unchanged (same SHA before and after deploy).

Completeness was established by diffing the build base against the fix commit:

    $ git -C wine diff 002153a 3a47f7c --stat
     dlls/kernelbase/security.c        |   2 +-
     dlls/kernelbase/tests/security.c  |  73 +++++++++++++-
     dlls/userenv/Makefile.in          |   2 +-
     dlls/userenv/tests/userenv.c      | 136 +++++++++++++++++++++++++++
     dlls/userenv/userenv.spec         |   1 +
     dlls/userenv/userenv_main.c       | 128 ++++++++++++++++++++----
     include/userenv.h                 |   3 +
     include/winbase.h                 |   1 +

The only *functional* (non-test) source changes are in `kernelbase/security.c`
and `userenv/userenv_main.c` — i.e. **kernelbase.dll and userenv.dll**. `ntdll`
and `wineserver` do not change between the base (`002153a`) and the fix, so the
candidate's copies of those (from the base) are already fix-compatible. Both
changed DLLs were recompiled from `3a47f7c` for both architectures, so the
deployed candidate fully represents the fix.

Fix-base binary SHA-256 (the set asserted and re-verified in the deployed
candidate):

| Component | x86_64 | i386 |
| --- | --- | --- |
| `dlls/userenv` | `2a953131cb7fd45a837ed66318e1d895814eacf4f2e7a1d4c716b6b5324eca6e` | `5ecf095f3c1a3d60643acc87f90b3023f80ca4671501e1095c083c47af770904` |
| `dlls/kernelbase` | `711b23181de67e3b286f9c12008d6fe0c2ddb1935ff645ea7e7090ea00ede30e` | `f77049e3f300708feba38d4d6b7810c0c3c11cf0d230cc240562bbc0e4d7c255` |
| `dlls/ntdll` | `b86d89551e1013819e6866c516833cbf80f42915de04e88773d92ecf38b9d2e6` | `9e6fdbad05651d51ed1b9d39e3a7853299cc875a0b8c56b70015bf41b16c7f75` |
| `server/wineserver` | `6c063646076bb199e82ee70d594b98783c64871fcb413dfa9e437e590c5cc969` (single, host binary) | — |

## What commit 3a47f7c actually changes

- **`kernelbase/security.c` (1 line):** `AC_MAPPING_ROOT` moved from the
  non-standard `Software\Wine\AppContainer\Mappings` to the standard Windows path
  `Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppContainer\Mappings`. This is where
  `AppContainerRegisterSid` stores the moniker→SID mapping.
- **`userenv/userenv_main.c`:** `CreateAppContainerProfile` now creates
  `%LOCALAPPDATA%\Packages\<container_name>\AC` after a successful registration;
  a new `GetAppContainerFolderPath` is implemented (reads the standard mapping
  key, returns that folder); `DeleteAppContainerProfile` removes those
  directories; kernelbase proc-pointer loading was made thread-safe via INIT_ONCE.
- **`userenv/userenv.spec`:** new export `GetAppContainerFolderPath`.
- **`include/winbase.h`:** new prototype `CreateAppContainerToken` (the
  implementation already existed in the base — see build notes).

## Build and unit tests

`userenv.dll` and `kernelbase.dll` were recompiled from `3a47f7c` for both
x86_64 and i386 in the project wine build tree. Three **build-only** local source
adjustments were required to compile the fix and its tests in this tree; none of
them change the fix's runtime logic:

1. `include/userenv.h` — `CreateAppContainerProfile` was declared with last
   parameter `PSID*` but implemented as `SID**`; the header prototype was
   corrected to `SID**`. (This is a genuine signature mismatch inside commit
   `3a47f7c` itself; zcomputerwiz's follow-up wine commit `9a70d724` carries the
   identical fix upstream, so this local patch reproduces it.)
2. `dlls/kernelbase/tests/Makefile.in` — added `IMPORTS = advapi32`; the extended
   security tests call advapi32 symbols the kernelbase test DLL did not otherwise
   import.
3. `dlls/userenv/tests/Makefile.in` — `IMPORTS` changed from
   `userenv advapi32` to `combase userenv advapi32`; the new
   `GetAppContainerFolderPath` test path uses `CoTaskMemAlloc` (combase).

Unit tests (rebuilt binaries, both architectures):

| Suite | x86_64 | i386 |
| --- | --- | --- |
| kernelbase security | 61 assertions, 0 failures | 61 assertions, 0 failures |
| userenv | 280 assertions, 0 failures | 280 assertions, 0 failures |

## Verification environment

- Isolated paths (fresh, not reused): stage
  `/home/debian/wex-g4/chromium-browser-verify-r1-20261029-stage`, output
  `/home/debian/wex-g4/chromium-browser-verify-r1-20261029`, profile under the
  output dir. Guest candidate wine at
  `/home/debian/wex-g4/wine-verify-r1-20261029/build/wow64`.
- Normal 8 GiB boot (`launch.py -m 8192`) from
  `system-native-identity-r4-20261024.qcow2`; SSH hostfwd `127.0.0.1:2235→22`.
- Path-locked helper PE for the identity; child args byte-identical to the
  consumer's `verify-r1` contract (`WEX_DIAGNOSTIC=1`).
- Wine debug channels: `-all,+timestamp,+pid,+tid,+winediag,+process,+userenv,+security`.
- Scoped Chromium VLOG: `--vmodule=*/content/browser/*=2,*/content/browser/renderer_host/*=2,*/content/browser/loader/*=2,*/services/network/*=2,*/sandbox/*=2,*/base/process/*=2`.
- Fixture nonce `89e93f9cfde20c0bc1eb21ed467c3147`; ephemeral loopback port
  **41113** (not the WO's illustrative 44175).
- Consumer mode: `scripts/check-g4-chromium-native-exit.py --verify-r1`. Per-run
  orchestrator: `build/g4-virgl-20261022/chromium-browser-verify-r1-trial.py` (local
  harness; `build/` is gitignored, so it is not tracked in this repository).

## Q1 evidence — kNetwork sandbox rollback (FAIL)

From `chromium-native-evidence/helper.stderr.log` (the WINEDEBUG trace), thread
`022c` is the `utility!kNetwork` process. The app-container SID lifecycle:

    56.991:00e0:022c:trace:security:AppContainerRegisterSid (000037500004C3F0, L"cr.sb.netED86A9060BF9087DCF4C7462DCD014893457BF94", L"Chrome Sandbox")
    56.998:00e0:022c:trace:security:AppContainerUnregisterSid (000037500004C3F0)

The kNetwork app-container SID (`cr.sb.net…`, display "Chrome Sandbox") is
registered and **unregistered 7 ms later** — Run 1's exact signature, unchanged by
the fix. The surrounding token sequence (same thread) shows the sandbox is built
via `CreateRestrictedToken` (lowbox), not an app-container token:

    56.954:00e0:022c:trace:security:CreateRestrictedToken token …, flags 0x1, disable_sids 4 …, restrict_sids 5 …
    56.954:00e0:022c:trace:security:CreateRestrictedToken token …, flags 0,   disable_sids 0 …, restrict_sids 9 …
    56.991:00e0:022c:trace:security:AppContainerRegisterSid (…)
    56.998:00e0:022c:trace:security:AppContainerUnregisterSid (…)
    57.030:00e0:022c:trace:security:CreateRestrictedToken token …, flags 0x1, disable_sids 8 …, restrict_sids 1 …
    57.030:00e0:022c:trace:security:AdjustTokenPrivileges (…)

Two further observations narrow where iteration 2 should look:

- **`CreateAppContainerToken` is never called** anywhere in the trace, even though
  it is implemented and exported in the candidate (`kernelbase/security.c`,
  `@ stdcall CreateAppContainerToken`). Chromium's kNetwork path uses
  `CreateRestrictedToken` (lowbox) exclusively; the lone RegisterSid→UnregisterSid
  pair is an app-container attempt that Chromium unwinds within 7 ms.
- **`GetAppContainerFolderPath` (which carries a `TRACE()` in the fix) never
  fires**, and `CreateAppContainerProfile` is not on this flow's path either —
  Chromium calls `AppContainerRegisterSid` directly. So the userenv additions in
  the fix (folder creation, `GetAppContainerFolderPath`) are not exercised by the
  kNetwork sandbox; only the kernelbase `AC_MAPPING_ROOT` change is plausibly on a
  live path.

The consequence is visible in `chromium-native-evidence/chromium.log`:

    [224:228:1029/082914.664:ERROR:content\browser\network_service_instance_impl.cc:652] Network service crashed or was terminated, restarting service.

This is the only network-service line in the log; at these VLOG levels no
`--service-sandbox-type`, `kNetwork`, or app-container string is emitted, so the
exact fallback type cannot be read from chromium.log — but the crash/restart of
the network service is unambiguous. Notably there are only **4** error/warn/fixme
lines in the entire WINEDEBUG trace (2× `RegisterGPNotification`, 1×
`GetProfileType`, 1× winediag OpenGL) — wine logs no AppContainer failure, so the
rollback is Chromium's own cleanup rather than a logged wine-side error.

## Q2 evidence — fixture never loads (FAIL)

From `chromium.log`, navigation to the fixture began and was then dropped by the
network-service crash 292 ms later:

    [224:228:1029/082914.372:VERBOSE1:chrome\browser\enterprise\data_protection\data_protection_navigation_observer.cc:261] enterprise.data_protection: URL to scan: http://127.0.0.1:41113/89e93f9cfde20c0bc1eb21ed467c3147/index.html#nonce=89e93f9cfde20c0bc1eb21ed467c3147
    [224:228:1029/082914.664:ERROR:content\browser\network_service_instance_impl.cc:652] Network service crashed or was terminated, restarting service.

From `chromium-native-evidence/netlog.json` (3210 events), the only event that
references the fixture is a **speculative preconnect**, not a document request:

    { "allow_cached_response": true, "dns_query_type": "UNSPECIFIED",
      "host": "http://127.0.0.1:41113", "is_speculative": true,
      "network_anonymization_key": "http://127.0.0.1 same_site", "secure_dns_policy": 0 }

Across **all 3210 netlog events, every `http_response_code` is `None`** — no HTTP
response (fixture or otherwise) completed in the capture window. The consumer's
`results.json` records `outcome = native_timeout`, `accepted_observation = false`,
`manifest_marker_verified = false`, terminal `wait_result = 258`, `wait_ms = 45000`.

## Graceful shutdown (WO requirement met)

`results.json`: `stop_reason = "deadline"`, `graceful_powerdown = true`,
`fallback_used = false`, QMP `powerdown_requested/ack = true`,
`guest_shutdown_observed = true`, `protocol_complete = true`; events `POWERDOWN`
then `SHUTDOWN {guest:true, reason:"guest-shutdown"}`; **`qemu_exit_status = 0`**;
`cleanup = {qemu_stopped: true, xvfb_stopped: true}`. The guest serial console
ends in `[896.283329] reboot: Power down`. No stray qemu/launcher process remains.

## Findings for iteration 2 (hypotheses, to be confirmed)

1. The rollback is **Chromium's own cleanup**, not a wine-logged failure: the
   RegisterSid→UnregisterSid pair is silent on wine's side and Chromium then
   proceeds with a `CreateRestrictedToken` lowbox. The likely decisive step is
   whatever makes Chromium abandon the app-container route immediately after
   `AppContainerRegisterSid`.
2. Because Chromium calls `AppContainerRegisterSid` directly and never calls
   `CreateAppContainerProfile`/`GetAppContainerFolderPath`, the userenv portion of
   the fix (folder creation + new folder API) is not on this flow's critical path.
   The kernelbase `AC_MAPPING_ROOT` standard-path change is the part most likely to
   be live — but it did not by itself stop the rollback, so either that mapping is
   not what Chromium consults here, or a downstream token step still fails.
3. Suggested instrumentation: add a `TRACE`/WINEDEBUG in kernelbase
   `AppContainerRegisterSid`/`UnregisterSid` capturing the HRESULT and confirming
   which registry path was written; raise `*/sandbox/*` and the network-service
   VLOG to `=3` to capture *why* Chromium falls back (the current scope does not
   emit the fallback reason); and confirm whether Chromium ever attempts
   `CreateAppContainerToken` on a code path this build/version takes.

## Evidence archive

All primary evidence is archived under
`chromium-browser-verify-r1-20261029/chromium-native-evidence/`:
`chromium.log`, `helper.stderr.log` (the WINEDEBUG trace — the Q1 instrument),
`netlog.json`, `results.json`, `trace.json`, `native-exit.jsonl`, and the run's
`execution.json` / `serial.log`. The candidate coherence, differing set, fix-DLL
SHA verification, and control-channel `wineserver` before/after witness are in
`execution.json`.

## Iteration 2 (wine `871f6183`, level-3 VLOG) — POSITIVE on Q1 and Q2

WO `WO-20261029-CHROMIUM-VERIFICATION-ITERATION-2`, Verification Run 2 (final
authorized trial). Target identity `chromium-browser-verify-r2-20261029`. This run
re-tests the same two acceptance criteria against the follow-up wine fix:

- **Q1** — does `utility!kNetwork` initialize and *remain* under
  `--service-sandbox-type=network` (no `=none` fallback, no crash-restart)?
- **Q2** — does the fixture `index.html` load with HTTP 200?

### What changed since iteration 1

Wine commit `871f6183eeb37b6ea9d54254ce44bb216ba1050d` (branch
`codex/g4-appcontainer-correctness`, zcomputerwiz) — *"kernelbase, userenv:
instrument AppContainer registration return status and fold build fixes"* — is a
focused delta on top of iteration 1's fix base (`3a47f7c`):

    $ git -C wine show --stat 871f6183
     dlls/kernelbase/security.c        | 21 +++++++++++++++++++++
     dlls/kernelbase/tests/Makefile.in |  1 +
     dlls/userenv/tests/Makefile.in    |  2 +-
     3 files changed, 23 insertions(+), 1 deletion(-)

- **`kernelbase/security.c` (+21):** the AppContainer registration return is now
  instrumented — `AppContainerRegisterSid` logs the HRESULT and the exact registry
  mapping path it wrote on return (visible in the trace as
  `AppContainerRegisterSid returning hr=…, path=…`) — and creates the AppContainer
  profile folder `%LOCALAPPDATA%\Packages\<container_name>\AC` *directly inside* the
  registration call. Per the WO, matching TRACE was added to
  `AppContainerUnregisterSid` and `AppContainerLookupMoniker`. This is the decisive
  change: iteration 1's trace showed Chromium calling `AppContainerRegisterSid`
  directly (never `CreateAppContainerProfile`/`GetAppContainerFolderPath`) and then
  unwinding the registration 7 ms later; folding the folder creation into the
  registration removes the downstream step that was failing.
- **test `Makefile.in` (+1 / 2±):** the build-dependency fixes for the extended
  security/userenv tests, folded in so the tree builds cleanly (no local source
  patches were needed this iteration).

Superproject commit `06d722b8` (branch `codex/chromium-boundary-diagnostic`) added a
`--verify-r2` mode to `scripts/check-g4-chromium-native-exit.py`, raised the scoped
Chromium VLOG from level 2 to **level 3**
(`--vmodule=*/content/browser/*=3,*/content/browser/renderer_host/*=3,*/content/browser/loader/*=3,*/services/network/*=3,*/sandbox/*=3,*/base/process/*=3`),
and pinned the wine gitlink to `871f6183`.

### Candidate provenance and completeness

As in iteration 1, the guest candidate was assembled as a hardlink copy (`cp -al`) of
the control-channel candidate overlaid with exactly the components that differ from
the fix base, measured live by SHA-256. The run recorded `candidate_coherent = true`
and a differing set of **6 DLLs** — `{kernelbase, ntdll, userenv} × {x86_64, i386}`;
`wineserver` was not in the differing set (the control-channel candidate's wineserver
was already byte-identical to the fix base). The four functional fix DLLs were
recompiled from `871f6183` for both architectures and re-verified by SHA-256 after
deploy:

| Component | x86_64 | i386 |
| --- | --- | --- |
| `dlls/userenv` | `fe856723eff1715c5aaccd569507ce59c7fdf52f37580b84f3b0d4230c94d828` | `ac8954dac002a274fa68737d912e8f1ce3f89efb33d22b0e544212d9f1d66f24` |
| `dlls/kernelbase` | `f233fabf7404024532f2bdfa4e8bd2e854ceb4916b1615bfab8fe95b996331d4` | `df23ceca05345c455e4de07ec2611acdad4eb8804c15244afbc81039fdfe63fd` |

The control-channel source `wineserver` was witnessed unchanged by the deploy (same
SHA-256 `6c063646…` before and after). `userenv.dll` and `kernelbase.dll` were
rebuilt from `871f6183` in the project wine build tree; the corresponding wine
unit-test suites (kernelbase security and userenv) were rebuilt and run with
**0 failures**.

### Verification environment

- Isolated paths (fresh, not reused): stage
  `/home/debian/wex-g4/chromium-browser-verify-r2-20261029-stage`, output
  `/home/debian/wex-g4/chromium-browser-verify-r2-20261029`, profile under the output
  dir. Guest candidate wine at
  `/home/debian/wex-g4/wine-verify-r2-20261029/build/wow64`.
- Normal 8 GiB boot (`launch.py -m 8192`) from
  `system-native-identity-r4-20261024.qcow2`; SSH hostfwd `127.0.0.1:2235→22`.
- Path-locked helper PE for the identity; child args byte-identical to the consumer's
  `--verify-r2` contract (`WEX_DIAGNOSTIC=1`, level-3 VLOG).
- Wine debug channels: `-all,+timestamp,+pid,+tid,+winediag,+process,+userenv,+security`.
- Scoped Chromium VLOG at **level 3** on the six categories above.
- Fixture nonce `f5017addaaa244530f64af22d0604ab9`; ephemeral loopback port **44403**.
- Consumer mode: `scripts/check-g4-chromium-native-exit.py --verify-r2`. Per-run
  orchestrator: `build/g4-virgl-20261022/chromium-browser-verify-r2-trial.py` (local
  harness; `build/` is gitignored, so it is not tracked in this repository).

> **Level-3 VLOG helper variant.** The tracked `scripts/g4_native_exit_contract.h`
> carries only level-2 vmodule branches. Because the consumer's `binding()` requires
> the compiled helper's command line to match its `--verify-r2` child args
> *character-for-character*, the r2 helper PE was compiled from a local build-dir copy
> of that header with the `WEX_DIAGNOSTIC` vmodule bumped 2→3. This is an untracked
> build artifact; the tracked header remains level-2 and the verify-r1 helper is
> untouched.

### Verdict

**POSITIVE on both criteria. The AppContainer kNetwork fix resolves iteration 1's
failure.** The wine-level trace shows the network app-container SID registered with
`hr = S_OK`, `CreateAppContainerToken` now actually invoked, the network service
spawned under `--service-sandbox-type=network`, and **no `AppContainerUnregisterSid`
anywhere** — the iteration-1 rollback is gone. The fixture loads over loopback with
HTTP 200, which independently requires a live, functioning `=network` service.

| Criterion | Result | Decisive evidence |
| --- | --- | --- |
| Q1 kNetwork stays `=network` | **PASS** | `AppContainerRegisterSid returning hr=0x00000000`; `CreateAppContainerToken` invoked (×2); `network.mojom.NetworkService` spawned with `--service-sandbox-type=network` + `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES`; **no `AppContainerUnregisterSid`**; no "Network service crashed" line |
| Q2 fixture loads HTTP 200 | **PASS** | loopback server served `GET …/index.html` → **200** (637 B) and `GET …/scene.js` → **200** (3750 B); netlog captured the document requests |

### Q1 evidence — kNetwork stays `=network` (PASS)

From `chromium-native-evidence/helper.stderr.log` (the WINEDEBUG trace), thread
`01f4` performs the registration and token build, and thread `0178` spawns the
service. The app-container SID lifecycle now completes without rollback:

    56.666:00e0:01f4:trace:security:AppContainerRegisterSid (00001DB80004C3F0, L"cr.sb.netED86A9060BF9087DCF4C7462DCD014893457BF94", L"Chrome Sandbox")
    56.667:00e0:01f4:trace:security:AppContainerRegisterSid returning hr=0x00000000, path=L"Software\Classes\Local Settings\Software\Microsoft\Windows\CurrentVersion\AppContainer\Mappings\S-1-15-2-3633287796-2419025165-1059878537-2377320999-1806066623-2572522406-411374689"
    56.668:00e0:01f4:trace:security:CreateAppContainerToken (00000000000005B8, …)
    56.669:00e0:01f4:trace:security:CreateAppContainerToken (00000000000005B8, …)

Contrast with iteration 1, where the same `AppContainerRegisterSid` was followed by
`AppContainerUnregisterSid` 7 ms later and `CreateAppContainerToken` was **never**
called. Here:

- The registration returns **`hr = 0x00000000` (S_OK)** and materializes a real SID
  (`S-1-15-2-…`) at the standard mapping path — the new TRACE from `871f6183`.
- **`CreateAppContainerToken` is now invoked (twice)**, i.e. Chromium proceeds down
  the app-container token route instead of abandoning it.
- The network service is then spawned with the app-container SID attached:

      56.669:00e0:0178:trace:process:CreateProcessInternalW … --type=utility --utility-sub-type=network.mojom.NetworkService --service-sandbox-type=network …
      56.672:00e0:0178:trace:process:CreateProcessInternalW PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES 00001DB8000C43C0
      56.672:00e0:0178:trace:process:NtCreateUserProcess … --service-sandbox-type=network … parent (nil)

- **`AppContainerUnregisterSid` does not appear anywhere in the trace** (grep over the
  full log: no matches), and `chromium.log` contains **no** "Network service crashed
  or was terminated, restarting service" line (iteration 1's signature). The service
  therefore *remains* under `=network`; it stayed alive for the full 45 s observation
  window and served the fixture during that time (see Q2).

The other utility processes keep their expected types — `storage.mojom.StorageService`
and `tracing.mojom.TracingService` spawn with `--service-sandbox-type=service`, and
the `chrome.mojom.ProcessorMetrics` utility with `--service-sandbox-type=none` (a
metrics helper, not the network service). The kNetwork process specifically holds
`=network`.

### Q2 evidence — fixture loads HTTP 200 (PASS)

From `chromium-native-evidence/results.json` → `http_fixture.requests`, the loopback
server (port 44403) served the fixture document and its script:

    GET /f5017addaaa244530f64af22d0604ab9/index.html HTTP/1.1  → 200, 637 bytes, response_completed
    GET /f5017addaaa244530f64af22d0604ab9/scene.js     HTTP/1.1  → 200, 3750 bytes, response_completed

(`GET /favicon.ico` → 404 is expected — the fixture serves no favicon; one early
connection timed out at the server's 0.5 s probe window before the real document
request.) This is the opposite of iteration 1, where all 3210 netlog events had
`http_response_code = None`. Q1 and Q2 mutually corroborate: a fixture HTTP 200 is
only possible if the `=network` service is genuinely up and serving.

### Consumer bookkeeping note (helper wait-for-exit timeout)

The consumer's own `results.json` records `passed = false`,
`observation_complete = false`, and
`error = "ValueError('Linux helper failed/exit mismatch')"`. This is a **harness
wait-for-exit bound, not a sandbox or acceptance failure**: the path-locked helper
bound correctly to `chrome.exe` (the journal's `bound` event carries the exact
level-3 command line) and then waited for the child to *self-exit* within 45 s
(`terminal: {event: "timeout", wait_result: 258, wait_ms: 45000}` — `WAIT_TIMEOUT`).
A live browser window does not self-terminate, so the wait expired and the helper
recorded `native_timeout`. The binding succeeded, `chrome.exe` ran for the full 45 s,
fetched the fixture, and produced all logs; the two acceptance questions are answered
affirmatively by direct evidence above. This is consistent with the run being a
diagnostic status observation (`classification: … held-handle Chromium status ONLY;
never rendering/security acceptance`), not a rendering-acceptance gate.

### Graceful shutdown (WO requirement met)

The authoritative run was stopped via the SIGTERM stop command: `results.json`
records `stop_reason = "signal"`, **`graceful_powerdown = true`**,
**`qemu_exit_status = 0`**, QMP protocol complete, and
`cleanup = {qemu_stopped: true, xvfb_stopped: true}`. No stray qemu/launcher process
remains.

> **Operational note.** The first verify-r2 VM launch self-terminated on its 900 s
> launcher deadline *before* `run()` could execute (the readiness budget had been
> consumed by preparation time between launch and run), shutting down cleanly via QMP.
> It was restarted fresh and the run above executed within budget; that aborted launch
> is preserved under `aborted-verify-r2-launch-1027/` and is not part of this result.

### Conclusion

The AppContainer kNetwork fix (iteration-1 base `3a47f7c` + iteration-2
instrumentation/folder-creation `871f6183`) **resolves the failure**:
`utility!kNetwork` initializes and remains under `--service-sandbox-type=network`
with no rollback, and the loopback fixture loads HTTP 200. Both acceptance criteria
PASS for iteration 2.

### Evidence archive

All primary evidence is archived under
`chromium-browser-verify-r2-20261029/chromium-native-evidence/`: `chromium.log`,
`helper.stderr.log` (the WINEDEBUG trace — the Q1 instrument), `netlog.json`,
`results.json`, `trace.json`, `native-exit.jsonl`, and the run's `execution.json` /
`serial.log`. Candidate coherence, the differing set, fix-DLL SHA verification, and
the control-channel `wineserver` before/after witness are in `execution.json`.
