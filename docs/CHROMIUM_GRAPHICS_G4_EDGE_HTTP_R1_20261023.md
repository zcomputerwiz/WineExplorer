<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# One HTTP startup diagnostic: readiness timeout, clean shutdown

**EXECUTED ONCE; no retry.** Approved immutable EDGE_HTTP_PREP preparation
`edge-headful-http-r1-20261023.preflight` was used unchanged (source698fa94e,
preflight SHA25688a5784caa5dce0cf696a645b880064e2615969a9b7c09c66d2507892f26b5f4).
Historical PREPARED_NOT_RUN marker is retained as preparation history; actual
execution is recorded in execution.json/procedure-exit.json and the audit below.
No diagnostic/fixture/security/backend changes, FILE repeat, debugger, reinstall,
download or production patch. Explicit C: workdir throughout; D: master/other
checkouts and submodules untouched. Unused ` M wine` remains unstaged.

## Actual result

Runner/consumer SSH exit1; procedure exit0, evidence archive exit0. Primary error:
`TimeoutError('No nonce-bound ready browser window')`. Total80.223354911s,
unchanged45s readiness bound. Diagnostic classification remains rendering
NOT_EVALUATED/passed=false, never graphics acceptance. No ready protocol, scene
pixels or fixed front_samples were obtained. One failure-only front captured.

HTTP endpoint was
`http://127.0.0.1:42967/8933d94f574aea8473129850412e076f/index.html#nonce=8933d94f574aea8473129850412e076f`.
Three accepted-connection records: EOF without request, bounded idle timeout,
then GET index.html HTTP/1.1, attempted200 **response_completed=true**,637 confirmed
body bytes, exact approved HTML hash. GET interval guest-monotonic
103.851786596..104.521186684s. No scene.js request. No internal server errors;
listener/active connections closed and worker joined. Server write is NOT proof
of browser consumption, parsing or renderer readiness.

Actual failure-front.png viewed: intact framed Edge app window with URL caption,
white client, no original marker/canvases. Process sampler saw one GPU-process
role and no renderer role; no sampled pre-cleanup role exits. Absence in sampling
does not prove no renderer ever launched or establish why readiness failed.
Classified fatal/GPU/renderer/graphics counters0; monitor/cleanup errors absent.
Raw Chromium logs additionally retain network-notifier/connect10051 and overlay
video-device errors; do not infer causality from those optional/error messages.
Trace quota14,258,473 bytes <134,217,728. Ordinary Wineboot retained exact pinned
msedge.exe SHA256bb74db4c... and registration156.0.4314.42; before/after snapshots
and original-file Win32 probe retained, not evidence about navigation completion.

## Gates, commands, shutdown

Before launch: host inventory/archive/source10/helper5/public-seed2/installed-ref
hashes matched, prospective outputs absent, no host QEMU/launcher observed.
Existing authorized credential public identity/seed/mode600 verified without
printing private contents. Guest free2,500,620,288 bytes BEFORE transfer/create;
all17 actual binaries and10 deployed source hashes matched. Raw-UTC650/435 guards
passed with828.303625/825.894329 seconds remaining. Inner Wayland EGL and outer
compositor both passed VirGL (D3D12 Intel UHD750); WSL host D3D12 also passed.
Private namespace4026532239 differed from parent4026531840, only loopback UP/
loopback addresses/routes at start AND end. No guest networking/security edits.

Executed the exact launch/run/stop commands in preflight.json: C: PowerShell
controller -RunName edge-headful-http-r1-20261023; C: private procedure `run` with
WEX_G4_CONTROL=edge-http and matching run/boot names; separate C: stopper with
this run's launcher-identity.json --timeout60. Full argv/commands are also retained
in readiness.json, execution.json and the adjacent .wsl-session.json. No Ctrl-C.
Consumer SSH300s/runner285s/launcher900s bounds unchanged; cleanup reserve90s.

Identity SIGINT stopper exit0; QMP graceful_powerdown=true, stop_reason=signal,
QEMU exit0, qemu_stopped/xvfb_stopped=true. Recorded launcher28471, QEMU28486 and
Xvfb28473 all absent in independent post-stop /proc check. Windows WSL PID40156
exited0 and absent; foreground controller session74271 also returned0. Controller
UTC05:55:19.3130658..05:58:16.4980023, safely within900s. Prefix cleanup wait0,
no remaining servers/prefix processes. No inference of gracefulness from absence
alone: explicit QMP/result/controller records were checked.

## Private evidence / handoff

All under `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-headful-http-r1-20261023/`:
edge-http-evidence/results.json, failure-front.png, browser stdout/stderr,
chromium.log, wexcomp.log, failure-front-capture stdout/stderr, per-native-window
logs, Wineboot/probe/cleanup logs, processes.jsonl and persistence snapshots.
Host procedure/archive/stopper exits/logs, budget guards, launcher results/identity,
readiness and independent-evidence-audit.json retained; Windows WSL session and
stdout/stderr are adjacent run-named files. Prefix/profile remain private in guest;
excluded from evidence tar/Git. No source/vendor payload added to Git.

Independently rehashed all107 extracted files against execution hashes AND tar
members, zero mismatches (also independently confirmed by parent).

- Evidence tar SHA256: `3b1494fe0bf053735502ece01b0e75a34f0226e06353be9515966dd64669a89c`
- Results SHA256: `18a38b6951d042c30e62f20e194e07633224069f90a6a1c26325397f11364b1f`
- Failure front SHA256: `6238aba396f5e6a160e1734aef66103a5dbfb89f7bd40c8fce9d1555b06307b4`

Report-only change; no unchanged unit reruns. Live prerequisite/cleanup gates
passed; browser readiness failed on this HTTP arm too. This does not establish
a file-only gap, sandbox/token/API cause, graphics failure or G4 pass. Stop here
for parent review and separately scoped owned-browser/navigation observation.
