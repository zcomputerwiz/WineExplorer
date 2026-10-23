<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Edge startup diagnosis: offline checkpoint

**PREPARED_NOT_RUN.** Implements the bounded offline milestone in
EDGE_STARTUP_DIAGNOSIS_PLAN. No VM, SSH, browser or native Wine execution.
Preserve r1's readiness timeout and clean shutdown (6fccc595); no cause inferred.

Implementation `e29eb59c` changes only the existing Edge runner, original native
observer and focused tests. Original HTML/scene, retained prefix/payload,
ordinary launch URL/backend/security and normal smoke argv/environment are
unchanged. No production/security fix or broad framework.

## Bounded changes

After an exception with a created browser, attempt at most ONE whole-front
`failure-front.png` before cleanup, only after verifying the owned compositor.
Capture timeout <=3s, shortened by remaining operational budget; never borrow
the55s cleanup reserve. Missing browser/pre-process guard failure produces no
capture. Dead compositor, timeout, corrupt PNG or capture/log failure is secondary
metadata: the primary error is retained. The image is labeled failure-only,
not a readiness retry or fixture pixel verdict. Browser log-close failure also
cannot hide the primary error/results. Existing healthy fixed captures remain
0/1/3s, independent of pixel verdicts.

Explicit `--startup-diagnostic` changes only logging/observation. Browser-tree
Wine tracing adds **+file,+seh** to existing +process/timestamp/PID/TID channels;
Wineboot/native-observer environments remain at the prior baseline. Chromium
VLOG2 is limited to file_url_loader_factory, navigation_request,
render_process_host_impl, child_process_launcher_helper and its _win variant.
No security, OS, file-access, URL-scheme, payload, pending-operation or feature
change. Diagnostic mode **always passed=false/exit1**, even if readiness and
front collection succeed: classification is rendering acceptance NOT_EVALUATED.
Inspect actual error/collection metadata, never treat this mode as a rendering pass.

Combined browser stdout/stderr/Chromium log soft stop threshold **128 MiB** is
checked during readiness/capture health checks and again after cleanup. Over-limit
data is retained, not truncated; the run stops rather than retrying. Writers may
overshoot between checks and during the bounded terminal capture/cleanup. This
is a quota-stop diagnostic, not a product failure. Oversized logs are not fully
re-read by the final health scanner after the quota stop.

The same tiny native observer has a diagnostic-only `--fixture-file` mode:
restricted owned staged index.html path, GetFileAttributesEx, read-only CreateFile
with read/write/delete sharing, and ReadFile of at most64 bytes. Size/prefix/error
JSON must match the original local fixture before browser creation. Process bound
<=5s; failure/mismatch/timeout gates browser launch, without path rewriting.
No Microsoft payload file is probed. Success would prove only simple Win32 IO,
NOT Chromium async loader/header/MIME/renderer completion.

## Actual offline tests

```powershell
wsl -d Debian --exec sh -lc 'cd /mnt/d/DeepThought/Projects/WineExplorer/scripts && python3 -B -m unittest test_g4_edge_headful test_g4_edge_install test_g4_virgl_budget test_g4_virgl_launcher test_g4_consumer_harness'
node scripts/test_g4_edge_scene.js
```

**104 Python tests PASS, no skips**: original87 plus17 new executable controls.
They cover terminal image success/failure/corruption/timeout, primary preservation,
dead/no compositor/browser guards, cleanup-reserve refusal, log-close failure,
diagnostic/normal command/environment/mode dispatch and no-acceptance behavior,
combined quota boundary/stop and untouched logs, file-probe protocol/error/timeout
and browser gating. Expected missing-helper argparse stderr is a passing negative
control. Actual original fixture JS tests PASS (Windows Node; Linux Python tests
also invoke it rather than fabricate a passed page object).

Updated native helper fully cross-compiled/linked with Linux MinGW:
`x86_64-w64-mingw32-g++ -O2 -static -o build/.../edge-headful-offline-review/window-observer-startup.exe scripts/g4_edge_window_observer.cpp -luser32`.
Ignored original output retained on D:. Native syscalls were NOT run/tested on
Wine; mocked probe JSON is not compatibility evidence. `git diff --check` passed.

## Private preparation for parent review

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-headful-startup-r1-20261023.preflight/`
contains a fresh source archive, PREPARED_NOT_RUN marker and exact launch/run/stop
commands in preflight.json. Stage archive SHA256:
`d71a92635d5af632af6c6269f3755be58f3b935f914c3392d310382f95d7c122`.
Independently verified all9 members/current sources/sizes, all5 host helpers,
archive bytes and accepted installed-evidence reference; future run absent.
Inventory retains17 expected binary hashes (16 stack + installed msedge.exe).
No vendor payload or old failed profile is copied into the source archive/Git.

Existing private procedure adds only explicit `edge-startup` control/flag/evidence
parameters, reused9-file inventory and the same1 GiB pre-transfer gate. New helper
SHA256 `d4ced0c073f6c6b2ce5ecc6dbc9bb7e9efc71d2a510e735a892d98c44472f79d`.
Old manifests/evidence unchanged. Retained installed prefix is deliberate; future
profile/output are NEW. Bounds remain285s/300s SSH, 45s readiness, 650/435 raw-UTC
guards, 900s owned launcher, 90s shutdown reserve and exact-identity stopper.
No acquisition/reinstall, browser retry, bypass, production patch or G4 claim.
Return for independent parent review before a separately authorized live attempt.

## Parent review / one diagnostic dispatch

Parent read implementation/native helper/tests/private control additions and
independently passed all104 focused Linux tests, actual-fixture Node tests and
diff checks. Independently verified the current nine archive members, sizes,
current sources, archive hash, five host helpers and installed-evidence reference;
future output absent. Offline milestone accepted for ONE diagnostic execution
using this manifest. Preserve the primary failure/front/logs and exact shutdown
evidence; diagnostic exit1 is expected by design, not itself a new product defect.
No inference of readiness/rendering success or a security/graphics cause in advance.
