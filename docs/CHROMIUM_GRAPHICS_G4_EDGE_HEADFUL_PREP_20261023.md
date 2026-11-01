<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Installed Edge headful smoke: offline implementation checkpoint

**PREPARED_NOT_RUN.** Implements only the offline milestone of
`CHROMIUM_GRAPHICS_G4_EDGE_HEADFUL_IMPLEMENTATION_PLAN_20261023.md`.
No VM/SSH/browser execution, reinstall, download or production/established-consumer
change. Parent reviews before any live attempt. No graphics/G4 acceptance here.

## Implementation

`18363266` adds the focused runner, original HTML/JS page, small read-only Win32
window observer, Python tests and actual-fixture Node tests. Runner reuses the
reviewed installer environment/PE/registration helpers and existing owned
compositor, resource sampler, isolated wrapper and logged-process helpers.

The retained installation prefix and msedge.exe SHA256 are pinned to the accepted
installation report. Before any Wine process: require fresh owned output, >=1 GiB,
no existing prefix processes, exact installed payload/registration and C:/Z: alias
mapping. Snapshot actual pending-rename registry blocks/hive hashes and bounded
relevant payload inventory without interpreting installer scheduling messages.
Repeat after ordinary bounded Wineboot; payload/version/mapping failure prevents
browser launch. No pending-operation edits or cleanup of files.

Actual installed Edge launches once with `--app=file:///Z:/...#nonce=...`, a NEW
private user-data directory, first-run/default-browser suppression and diagnostic
logging. HOME/XDG/temp are private. No renderer Node, network service, CDP,
extension, repaint interaction, security bypass, backend spoof or pixel retry.
The existing mount/network namespace and private sockets isolate future execution.

The original page uses genuine visible WebGL1/2 canvases, exact default/FBO/final
RGBA readbacks, FBO completeness, error/loss checks and observed renderer strings.
It freezes distinct magenta/cyan canvases beside a blue CSS marker on pale green.
Two bounded rAF opportunities precede nonce-bound JSON readiness in the ordinary
title; they are not presentation proof. Context loss publishes failed readiness.
Explicit loading is pending; malformed/stale/ambiguous/failed ready data is fatal.

Observer inventories all HWND titles, classes, visibility, native window/client
screen bounds, output scale and Win32 PID/executable path. Only exact pinned
`C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe` or its exact Z:
alias is accepted, with the prefix C:/Z: mapping verified before/after Wineboot.
No basename matching or Windows/Linux PID equivalence. Linux prefix process
snapshots separately retain command lines/start ticks and observed GPU/renderer
roles/exits; sampling is not complete process-lifetime telemetry.

Front captures are fixed at 0/1/3s after first valid readiness observation (NOT
DOM-freeze time). Each is preceded AND followed by native/page identity/geometry
checks. Records retain actual request/start/end elapsed times and lateness,
dense independently expected CSS/canvas inset pixels and original PNG hashes.
Valid stale pixels do not stop or reschedule collection; final two must pass.
Missing/corrupt captures or observer/protocol/identity errors fail collection.

Client dimensions must match the CSS viewport at the independently observed
native scale/DPR. No toolbar/menu offset is guessed. Real Edge custom chrome or
DPI behavior may cause a concrete alignment failure on the first live attempt;
retain geometry and return for planning. Require nonempty Chromium log, healthy
browser/compositor, no classified fatal/GPU/renderer/known graphics errors,
and owned cleanup. **Browser GPU compositing remains NOT_PROVEN**, independent
of page WebGL renderer/clear results. Shader drawing, restart persistence,
tabbed UI, sandbox enforcement and full G4 remain separate work.

Complete runner budget 285s including preflight; 55s reserved for cleanup.
Observer compile <=20s, Wineboot <=60s, readiness <=45s, observer/grim calls <=5s,
all reduced by actual remaining budget. Host bounds/650 and 435 raw-UTC guards,
900s launcher, 90s shutdown reserve and exact-identity stopper remain unchanged.

## Actual offline verification

```powershell
wsl -d Debian --exec sh -lc 'cd /mnt/d/DeepThought/Projects/WineExplorer/scripts && python3 -B -m unittest test_g4_edge_headful test_g4_edge_install test_g4_virgl_budget test_g4_virgl_launcher test_g4_consumer_harness'
node scripts/test_g4_edge_scene.js
```

Final focused Python result: **87 tests PASS, no skips** (34 new headful + 53
existing installer/budget/launcher/consumer controls). Expected missing-helper
argparse stderr comes from an executable negative control, not a test failure.
Node fixture tests PASS: actual scene source with fake GL/frame/timer callbacks,
including readback/FBO/API/loss/error/timeouts and context loss after readiness.
Linux Python tests invoke available Windows Node for actual fixture JSON (Linux
Node absent); no Windows shim claims Linux browser coverage. Tests cover exact
argv/private environment, persistence failure/no launch, guards/no cleanup,
owner aliases/ambiguity, finite/clip/scale/client geometry, valid fractional
scale with caption offset, decoded blank/partial/wrong-scale/corrupt PNGs,
fixed stale collection/lateness and final post-capture move/loss, process and
health/timeout/cleanup failures. No vendor process was executed by these tests.

Linux x86_64-w64-mingw32-g++ syntax AND full static link of the observer passed;
original output is under ignored `build/g4-virgl-20261022/edge-headful-offline-review/`.
Observer execution on Wine is untested. `git diff --check` passed.

## Private preparation

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-headful-r1-20261023.preflight/`
contains PREPARED_NOT_RUN, exact launch/run/stop argv in `preflight.json` and
`source-stage.tar`. Archive SHA256:
`59002220800061d7847c27c9429d7ca407079b7eb2eeab57930a3f00c0744b4d`.
All **9** archive/source members, sizes and **5** host-helper hashes independently
verified; destination run absent. No proprietary payload copied into the archive.
Expected binaries: 16 accepted Wine/compositor hashes plus the pinned installed
msedge.exe. Registration-version/install provenance references the prior accepted
results.json hash; relevant DLL/hive inventory is observed before/after future
Wineboot, not falsely claimed independently known now.

The existing private `presentation-timing-trial.py` gained only the explicit
`edge-headful` command/inventory/evidence parameters and 1 GiB pre-transfer gate;
host helper SHA256 `3f5b9dfa9f3236508e38ce8d8c0436815493b2f0635d4d23b27d5d4124e3e8a8`.
Old preparations/history are preserved. No controller/stopper or existing
consumer implementation changed. Return for parent review before live execution.

## Independent parent review and bounded live dispatch

Parent read the final runner, original page, native observer, executable tests
and private procedure additions. Independently reran all **87 focused Linux tests**
without skips, plus the actual-fixture Node tests; all passed. Independently
verified the tar digest, all nine member sizes/hashes/current sources, all five
host helpers and the accepted installed-evidence reference hash. Output was absent.
Early review corrections retain explicit loading as pending, require exact mapped
C:/Z: owners and check identity/geometry after each front, including the last.

Offline milestone accepted for ONE live dispatch of the manifest above. This is
not advance acceptance of the native observer/Edge custom chrome or persistence.
Preserve a concrete startup, geometry, capture or renderer failure and stop for
parent diagnosis, without repeat launches or guessed offsets. Record all timing,
before/after payload state and graceful shutdown evidence. No GPU-compositing,
tabbed-browser or G4 claim from this smoke. No production changes/master merge.
