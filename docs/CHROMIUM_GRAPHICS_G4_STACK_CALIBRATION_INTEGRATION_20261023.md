<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Original-child selected-stack integration — PREPARED_NOT_RUN

OFFLINE milestone only, implementing STACK_CALIBRATION_INTEGRATION_PLAN.
Code commit `381bb46a`. No VM/SSH/Wine/debugger/Edge/native-PE execution. No
browser runner, production pins, D: master or other checkout edits; pre-existing
unused `M wine` remains unstaged. Parent review is required before any live run.

## Bounded implementation

Separate `scripts/check-g4-stack-calibration.py` reuses the existing isolation,
environment, owned compositor/VirGL, installed-registration and Linux identity
helpers. Only CLI input is a fresh direct `stack-calibration-*` output. Path,
space, idle-prefix, private-lo, original x64 PE and all19 binary-pin guards precede
output/consumer creation. No Wineboot/update/copy/profile/Edge path or retry.

Original waiter gains query-only `--verify NONCE PID CREATION MAIN_TID WORKER_TID`:
exact own module image, creation/liveness and both active thread owners. Nonce is
echoed, NOT read from target memory; readiness plus independently verified Linux
Popen PID/start/prefix/full argv supplies that binding. Win32/Linux IDs are never
equated. Native API behavior has NOT been exercised by the offline tests.

Initial loader/readiness cap30s is distinct from native30s lifetime starting in
main. A conservative receipt-based post-ready budget24s refuses attach unless
20s remains for debugger10 + verification3 + release3 + exit2 + margin2. This is
not an independent measurement of native-main entry/fflush delivery latency;
subsequent active-owner checks and zero release/child exits remain mandatory.
Native wait behavior is unchanged. Whole runner285s, cleanup reserve55s, SSH300s;
debugger10s INCLUDES quota/timeout kill and reap. Child stdout4096 and stderr1MiB
are monitored during loader and helper waits; helper combined logs have a1MiB
soft quota polled every20ms (not a hard filesystem-size cap). Partial logs survive.
Forced debugger termination may kill its attached owned child; never harmless.

Only generated decimal attach/bt-main/bt-worker/detach/quit commands execute.
Debugger env uses exact BUILD wine/WINE/WINESERVER, sanitized Wine settings,
`dbghelp=b`, empty symbol paths and inherited isolated lo. Tool hashes plus loader
policy are NOT independent runtime-loaded-file attestation. Parser follows pinned
source HEX output: exactly one attached PID, two separate full-image/selected-TID
headers and contiguous useful waiter/Windows-module frames; basename/Process_*
fallbacks and unbound current-TID stacks fail. Initial breakpoint/unbound stack
is allowed but never credited; arbitrary trace-prefix numbers are not target PIDs.
Zero frame pointer is valid; zero PC is not. Detach is silent: post-verification,
same live Linux identity, release0 and child0 prove functional resume only, not
the DebugActiveProcessStop API return. Final pass explicitly requires these,
same before/after private-lo proof, payload persistence and clean owned teardown.

## Actual offline verification

Run from the explicit owned C: worktree; all commands exit0:

```text
wsl -d Debian --exec unshare --net sh -lc 'ip link set dev lo up && cd /mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer/scripts && python3 -B -m unittest test_graphics_private_loopback test_g4_edge_headful test_g4_edge_http_fixture test_g4_edge_owned_threads test_g4_edge_install test_g4_virgl_budget test_g4_virgl_launcher test_g4_consumer_harness test_g4_stack_commands test_g4_stack_calibration'
node scripts/test_g4_edge_scene.js
pwsh -NoProfile -File scripts/test_g4_virgl_session.ps1
```

205 Linux tests PASS, no skips (176 prior +29 new); Node actual fixture PASS;
PowerShell real controller intercepted/no guest PASS. New tests exercise source-
format parsing, readiness/quotas, mocked calibration ordering/identity/release/
cleanup failures and real owned Python subprocess timeout/kill/reap transport.
Mocks/Python transport are NOT Wine/debugger/detach execution proof. Existing
expected argparse missing-Electron-helper negative still passes.

Host WSL cross-compile exit0, `-std=c++11 -O0 -g -static -Wall -Wextra`, exact
source `scripts/g4_stack_waiter.cpp`, no warnings (stderr0 bytes). Artifacts/logs:
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/stack-calibration-integration-build-20261023-r1/`.
PE SHA256 `17d1ecdc15542c6b9099f63b4b4c923d36ae768c86ca2ab41c77d364f7da12b0`.
No PE executed, no runtime symbol/source-line availability claim.

## Fresh private handoff

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/stack-calibration-r1-20261023.preflight/`
contains immutable `PREPARED_NOT_RUN`, source-stage.tar, calibration-pins.json and
preflight.json. Existing ignored C: `build/g4-virgl-20261022/presentation-timing-trial.py`
has only an explicit stack-calibration mode/inventory extension; no framework.

- Preflight SHA256: `5cc02499367e61cf2726863ede0054b150e0068dd74a1f9b2654df6a4eff836e`.
- Archive SHA256: `709df46ea14a89f5f36965a4acd2157c3a9898cc82a3768d26e18382bf03b29d`.
- Host audit passed:12 exact tar members (10 source files + original PE + pins),
  current source/member bytes/hashes,5 actual C: helpers,2 public seeds, approved
  installed-reference hash,19 known guest binary pins, exact original PE.
- Future run root absent. All payload/artifacts/storage D:, source/controller C:.
  Old prep/run records untouched. Exact launch/run/stop and consumer argv are in
  preflight.json; guard650 before any guest preflight,435 before consumer unchanged.
  Launcher900/cleanup90 and identity-validated separate SIGINT/QMP stopper retained.

Tool provenance remains DISCOVERY_R1's same-owned-host-build2 PE/source4 bridge;
overall host HEAD differs4315, guest source absent: no reproducible-build or
whole-source attestation. No guest hashes/space/backend were queried this step;
those are mandatory future preflight checks, not claimed results. Next step is
parent source/test/manifest review, NOT an Edge stack probe or live authority.

## Independent parent review / one original-child calibration authorized

Parent read complete fixture/verifier, runner, command/trace parser, tests and
private dispatch additions. Independently passed205 Linux tests/no skips, actual
Node/controller and native-path X-socket peer/inode checks, and separate native
x64 -O0 -g -static -Wall -Wextra cross-compilation without warnings. PE not executed.
Review corrections (in-flight child quotas, exact namespace equality, explicit
functional release/exit gate, full image headers and legitimate zero frame
pointer) are covered by negative/positive controls, not consumer assertion changes.

Independently verified exact preflight/archive hashes above, all12 tar/current
source/PE/pins members,5 current host helpers,2 seeds, installed-reference hash,
19 exact core+tool pins (prior17 unchanged), original PE/source/compiler log
records, correct C: commands/D: storage and absent prospective run.

ONE healthy ORIGINAL child calibration is authorized using this immutable
preparation, all live space/hash/650435/namespace/VirGL/owned-shutdown gates.
No Edge, fault injection, retry or additional run. Preserve partial failures;
debugger timeout may kill the selected original child and is not harmless.
Parent reviews actual selected stacks/detach-responsiveness evidence afterward;
this does not authorize an Edge attach or establish graphics acceptance.
