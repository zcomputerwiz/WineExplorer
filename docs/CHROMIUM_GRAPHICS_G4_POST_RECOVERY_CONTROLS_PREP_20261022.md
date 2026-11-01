<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Post-recovery controls — PREPARED_NOT_RUN

Prepared only, per POST_RECOVERY_CONTROLS_PLAN. No boot, guest/SSH access,
deployment, compile, control invocation, fault or tracked diagnostic/test change.
Parent must review before execution. Existing recovery/preparation records intact.

All directories below are under `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/`:

| Preparation directory | Exact control | Sources / expected binaries |
|---|---|---:|
| `post-recovery-x86_64-20261022.preflight/` | foreign, x86_64, own-dcomp + foreign-full-dcomp | 8 / 16 |
| `post-recovery-i686-20261022.preflight/` | foreign, i686, same two cases | 8 / 16 |
| `post-recovery-webview2-20261022.preflight/` | real WebView2, graphics-diagnostic, 25 seconds | 6 / 19 |

Each contains `PREPARED_NOT_RUN`, `preflight.json` with exact guest/run/launch/
stop argv, `source-stage.tar`, four host-helper copies/hashes and dependency
audit. No destination run directory exists. Every archive member/hash and current
source/helper hash was verified offline; all commands reject accidental Electron/
recovery/timing/trace flags by inspection and executable preparation assertions.
Foreign tar SHA (both): `2899722954df535390f2bd0ba8f44320ee49a3add288908cb3480e19cc1bc8c4`.
WebView2 tar SHA: `29fb3f49789b9ecfd06e892598b1be7afc3a2fa87193fd45ed57cc27aef71921`.

Reused private `build/g4-virgl-20261022/presentation-timing-trial.py`, adding
explicit `WEX_G4_CONTROL=foreign-x86_64|foreign-i686|webview2-strict` command,
inventory and evidence-name selections. No new orchestration framework. Copies
and hashes are in each preparation. Run/boot names are the corresponding table
names without `.preflight`; current manifests choose distinct boots, not an
assumption that all three fit a single launcher. Both foreign commands specify
only `--case own-dcomp --case foreign-full-dcomp`; all three explicitly select
`--renderer gl --gl-driver virgl --compositor-renderer gles2`, existing owned
compositor/config/build and offline per-run isolation wrapper.

Foreign include audit stages BOTH `test_g4_foreign_present_probe.cpp` and its
quoted include `test_g4_composition_probe.cpp`; remaining C++ includes are system
headers. Python local imports/dynamic loads are accounted for, including compiler
helper, shared helpers, wrapper and WebView2 analyzer/resources. Optional unused
Electron/recovery/X11 suite paths are excluded. Full audit and external tool
preconditions are in `post-recovery-preparation-audit-20261022.md` and copies.

Strict host/runtime paths are exactly those in the plan. Expected host/loader/
browser hashes came from private `virgl-consumer-evidence.tgz`, member
`virgl-webview2-strict/results.json`, retained as small provenance JSON in its
preparation. Historical stack/compositor hashes match the approved recovery
baseline. Archive/member SHA and all 19 expected hashes are in its manifest;
no deployment/binary availability is claimed before future guest verification.

Gates: producer exit 0 + submitted first frame + exactly 4096 matching pixels
per foreign case; strict WebGL1/2 availability/not-lost/default/texture/FBO/error
checks, clean classified/raw process logs and actual final front inspection.
All require inner/outer VirGL and owned cleanup. Stop matrix on first failure,
no retry. Existing 300s consumer + 45s archive + six 30s guest preflight operations
+ 90s cleanup = conservative 615s per next cell, excluding boot/host overhead;
launcher remains 900s. Identity stopper/Windows exit proof unchanged. If sharing
a boot is subsequently approved, its names/manifests and full remaining budget
must be reconciled before launch. No cleanup/downloads authorized here.

Only this note tracked. Return for parent review before any control execution.
