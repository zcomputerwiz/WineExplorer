<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium native-exit diagnostic — offline preparation

Status: **PREPARED_NOT_RUN**, for parent review under
`CHROMIUM_GRAPHICS_G4_CHROMIUM_NATIVE_RUNNER_PLAN_20261024.md`.
No VM, SSH connection, Wine/native PE/browser execution, download, PE rebuild,
disk creation or disk mutation occurred. Existing R4 and failure evidence remain.

`scripts/check-g4-chromium-native-exit.py` uses the original native parent and
normal eight Chromium arguments, original two-route HTTP fixture, privateLO and
actual compositor/backend checks. The existing collector accepts explicit
Chromium callbacks with an82s bound (30s loader +45s native wait +5s reap +2s
margin); its default original-child calibration remains unchanged. It records
early-exit/identity phases, preserves rejected identities and raw journal, and
reaps the observer before prefix teardown. Full unsigned32 status, including259,
comes only from a bound signaled exit. Native failure/timeout/race records never
invent a child status. Diagnostic exit is always1; observation and cleanup
completion are separate and do not establish graphics/security acceptance.

The new ignored explicit adapter is
`build/g4-virgl-20261022/chromium-native-trial.py`; the giant legacy dispatcher,
launcher and controller were not changed. It distinguishes expected diagnostic1
from orchestration failure, preserves bounded raw SSH errors, and archives after
consumer dispatch. It reuses a complete fixed runtime read-only, or transfers the
existing cached ZIP and extracts only into an absent fixed directory. Partial
runtime/output/source-stage paths fail without repair or overwrite. Current
transfer checksums remain required; historical file differences warn. The runner
records all19 environment files plus the internal loader before launching.

Private preparation and evidence (D: only):

- `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-native-exit-r1-20261024.preflight/`
  contains `preflight.json`, `source-stage.tar`, `PREPARED_NOT_RUN`.
- Preflight SHA256:
  `801558b985bf30ed4e36cebac258251f6de18f59e71f1600bbca533244bf7ea8`.
- TAR SHA256:
  `7cc4a933398b0a6a5f6255b92f17e4a8f4e44005c4a0bdf43a9b81fa01080291`;
  16 members, including unchanged PE366c6928…, original HTML/JS and current
  transitive Python dependencies. No vendor runtime is in the source TAR.
- `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/chromium-native-exit-r1-20261024.change/`
  retains tests, new-adapter absent-before marker/current snapshot, collector
  diff, source snapshots, preparation stdout/stderr, `audit.json` and `seal.json`.
  Audit matched35 current input files and sealed14 artifacts. Adapter SHA256:
  `69292c82c550af0ac21697f012e4c2057ba5b9831f9fb5f18fe976b99e8ee119`.

The manifest records258 actual cached runtime-file observations, current ZIP
hash, host version-resource query (all three PE resources156.0.8078.0), native
helper/header,19 historical environment references, host Wine frontend/loader/
server observations,10 host helpers, tests, seeds/ISO, durable credential/public
seed match and normal-lock existing R4 chain/idle snapshot. These are file/data
observations, not loaded-file attestation. The helper's8fc037… marker explicitly
remains unverified. No private payload/credential is committed.

Fixed GUEST image remains
`/home/debian/wex-g4/chromium-headful-r1-20261023-stage/vendor/extracted/chrome-win/chrome.exe`.
Fixed GUEST output/profile remain
`/home/debian/wex-g4/chromium-headful-native-exit-r1-20261023[/profile]`.
Fresh source stage is
`/home/debian/wex-g4/chromium-native-exit-r1-20261024-stage`.
Both fresh paths must pass live canonical/lexists guards before writes; their
guest availability is unobserved offline. HOST run directory is absent. The
installed prefix is shared, not pristine; parentage changes from the original
baseline. Existing R4 is selected, with no new-layer/create command.

Exact commands are retained in the manifest: persistent PowerShell controller
with `-RunName chromium-native-exit-r1-20261024`; WSL adapter `run`; identity
stopper with `--timeout 80`. They have **not** been dispatched. Limits remain
900/90/80 outer, readiness674→650, preconsumer435, runner285/cleanup55,
consumer SSH300/archive45. An absent-runtime arm additionally requires660s
remaining before ZIP transfer60/extraction120 (runtime staging SSH160); otherwise
it stops. Future acceptance requires actual QMP event/ACKs, QEMU0, no fallback,
owned children absent and Windows controller0; stopper0 alone is insufficient.

Validation: WSL Debian, scripts cwd, `python3 -B -m unittest
test_g4_chromium_native_exit test_g4_chromium_native_adapter test_g4_native_exit
test_g4_native_exit_calibration test_g4_edge_http_fixture -v`:
**50 tests PASS,20.287s, no skips**, plus `git diff --check` PASS. Tests execute
the shared C contract harness, real original HTTP socket routes, safe tiny
synthetic ZIP extraction, actual parser/collector/adapter callbacks, early races,
full32/259, malformed bindings, quota, guard/no-stage failures, runtime drift/
partial refusal, diagnostic1 archive interpretation and reap-before-prefix.
Windows/native APIs, actual Chromium startup/status, guest runtime availability
and G4 remain unproven. Stop for parent review before any live dispatch.
