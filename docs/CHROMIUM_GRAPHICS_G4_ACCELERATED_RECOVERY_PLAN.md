<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 accelerated GPU-process recovery — implementation and acceptance plan

Status: **PLAN ONLY; execution not started.** Prepared against parent6928264e
on `codex/g4-r1-completion`. No VM boot, test, fault injection, implementation
change or merge is authorized by this planning step. Execution requires the
user's subsequent instruction. Publish this document only.

## Objective and boundaries

Prove that an actual Chromium consumer recovers from termination of its own
GPU child while Wine's native Wayland/private render bridge and the outer
wexcomp GLES2 compositor remain accelerated and alive. Prove replacement,
context loss/recreation, correct recovered front content and bounded cleanup.

Use the established QEMU/VirGL guest, not the known-failing SVGA consumer route.
Baseline evidence: [VirGL acceptance record](CHROMIUM_GRAPHICS_G4_VIRGL_20261022.md).
The previous WSL accelerated-client/pixman-outer recovery evidence is useful
but does not cover this accelerated outer-compositor cell.

This is GPU **process** recovery, not a host GPU reset, DRM device hot-unplug,
kernel hang, compositor restart, device-removal API certification, malware
containment or complete G4 acceptance. Do not induce a host GPU/driver reset.
Do not expand into installed-shell/theme work during this selected task.

## Fixed environment and preservation

- Branch `codex/g4-r1-completion`; start from current HEAD after checking status.
  Preserve unrelated work. Wine4315be8, wexcomp94faa651 are the known baseline;
  pin full revisions and binary hashes in each result. Do not silently mix builds.
- Reuse `D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/` overlay and its intact Debian
  backing image under the preceding VirtualBox test directory. All VM disks,
  snapshots, payloads and evidence stay D:. Tiny transient IPC sockets use the
  owned Linux-filesystem directory because DrvFS socket binding failed.
- Start only this guest; leave WineExplorer-VM/Qwen, existing workers and the
  separate powered-off SVGA guest untouched. Do not restore/reset any disk.
- Existing private launcher `build/g4-virgl-20261022/launch.py` needs a fresh
  output directory per boot. It is bounded900seconds. Before each test, budget
  setup + test + cleanup below that deadline; otherwise shut down and relaunch
  cleanly. Never double-open the overlay or let the launcher expire mid-test.
- Reuse the owned combined-WoW64 build and lawful cached Electron44.9.2.
  Runtime payloads, profiles and screenshots remain private/untracked, not Git/ISO.
  No downloads or dependency upgrades are expected during acceptance runs.
- No sandbox bypass, in-process GPU, single-process, disable-GPU, forced
  SwiftShader, fake caps, assertion weakening or production patch by default.

## Existing mechanisms to reuse

`scripts/g4-electron-probe/main.js`, `exerciseGPURecovery`, already:

1. Creates real WebGL1/2 contexts and tests red default-buffer/green FBO pixels.
2. Requires exactly one app-owned GPU child and GPU compositing enabled.
3. Revalidates its Windows PID + creation time immediately before faulting it.
4. Terminates that child from inside the private Wine app, not by Linux/global
   PID/name matching. Waits for replacement, then tests genuine old-context loss,
   newly created contexts, stable renderer process and resumed frames.
5. Freezes an identifiable recovered scene after its result is successful.

`scripts/check-g4-webview2.py`, `recovery_result` and `recovery_acceptance`, already
cross-check complete logs, reject extra GPU/renderer exits, require unchanged
feature/backend identity and permit only the exact single injected GPU crash
signature. Existing parser tests cover many negative cases. Preserve those
checks; do not add a general crash allowlist to make this backend pass.

Known limitations to close in the harness, **when execution is authorized**:

- Recovery screenshots currently use fixed elapsed times4/8/18/28 seconds.
  They are not necessarily aligned with pre-fault and settled recovery states.
- Recovered front pixels still require an independent compositor capture;
  successful WebGL readback/RAF alone is insufficient.
- Explicitly record stable browser-main and compositor PID/start identity,
  not only renderer PID. Confirm backend after the fault, not just at startup.
- Observer-thread errors/timeouts must make the diagnostic fail and preserve
  their cause. Preflight all capture dependencies before launching Wine.

## Ordered work and review gates

### 1. Read-only preflight, then establish a no-fault control

After authorization: check usage, Git/submodule status, owned launcher/VM state,
free D: and guest disk space. Boot only the owned guest and verify no active
probe/package operation. Verify current binary/payload hashes against baseline.

Confirm actual virtio_gpu render node access and explicit guest VirGL EGL.
Confirm host D3D12 acceleration, and both outer GLES2 compositor and native
Wayland client report `virgl (D3D12 (Intel UHD Graphics 750))` (allow normal
driver punctuation/version variation, not software identity). Reject nested
llvmpipe/softpipe/SwiftShader. Wine's exposed G45 identity is not physical GPU
proof; host identity and actual renderer observations are separate records.

Stage and hash the full original fixture dependency set, including
graphics_frame_pixels.py, graphics_resources.py, graphics_wexcomp.py, analyzer,
native-window probe, fixture JS/package, and the shared DComp C++ include.
Ensure no staging/copy can overwrite another run's loaded helpers. New code
must be tested/staged before probes, never rebuilt during a probe.

Run a fresh-prefix serial no-fault Electron control on the same route and
duration as recovery, with front captures. Require correct actual front
content and zero unexpected GPU/renderer exits. Preserve failures; do not
fault a process if the healthy baseline or ownership preconditions fail.

### 2. Make recovery front-pixel evidence event-aligned

Add minimal original fixture/harness support for uniquely identified stages:
healthy/pre-fault, recovered-contexts, recovered-frozen/settled. Retain the
existing fault/result format or version it explicitly; no parser ambiguity.
Capture acknowledgements must correspond to the exact stage/run and cannot
be satisfied by a stale acknowledgement. Use bounded waits and monotonic time.

Hold the scene still for captures; do not freeze away the resumed-frame test.
After proving post-recovery frames advance, freeze a deterministic original
scene. Sample actual outer compositor pixels at settled2/4second offsets.
Check known content marker/geometry and full applicable frame rims against
independently observed HWND state. Do not depend on glyph rasterization or a
full-screen byte-for-byte match. Inspect the final PNG manually as well.

Add deterministic Wine-free negative tests for missing/duplicate/out-of-order
stages, stale acknowledgements, missing/empty capture, wrong content/rims,
observer exception, timeout and software fallback. Preserve existing ownership,
pixel, full-log extra-exit and missing-helper rejection tests. Record test count;
do not assume the previous36 tests cover newly added behavior.

### 3. Run two independent, single-fault accelerated recovery trials

Use fresh output/prefix/profile for each trial; serial runs only. Each trial
injects exactly one GPU-child fault. Do not repeatedly kill a child in one run
and then waive Chromium's crash-loop/fallback policy. A separate repeated-fault
stress test is out of scope for this acceptance slice.

Keep existing10second replacement wait and subsequent bounded context/frame
checks initially. A timeout remains a failure. If shader startup is slow,
diagnose timing with logs before proposing a justified, documented bound;
never retry indefinitely or discard a slow failing run.

Base command shape, **not executed by this plan** (inside the owned guest):

```sh
sudo python3 -B /home/debian/wex-g4/run-isolated-graphics.py \
  --socket-root /home/debian/wex-g4/isolated-sockets -- \
  setpriv --reuid=debian --regid=debian --init-groups env HOME=/home/debian \
  python3 -B /home/debian/wex-g4/check-g4-webview2.py \
  --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 \
  --renderer gl --gl-driver virgl --compositor-renderer gles2 \
  --wexcomp-bin /home/debian/wex-g4/labwc \
  --wexcomp-config /home/debian/wex-g4/wayland \
  --runtime /home/debian/wex-g4/private-electron-44.9.2/runtime \
  --host /home/debian/wex-g4/private-electron-44.9.2/runtime/electron.exe \
  --electron-app /home/debian/wex-g4/g4-electron-probe \
  --seconds 60 --gpu-recovery --resource-profile \
  --output-dir /home/debian/wex-g4/UNIQUE-RECOVERY-RUN
```

Replace the output placeholder uniquely. Add event-capture options only once
implemented/reviewed; the command above alone does not certify front pixels.
Do NOT combine current --gpu-recovery with --lifecycle, --freeze-scene or
--restart-from: the existing CLI deliberately rejects those combinations.

### 4. Post-fault regression and cleanup

After both recovery trials, rerun serial x86_64 and i686 own-dcomp and
foreign-full-dcomp actual4096-pixel controls, plus strict WebView2 WebGL1/2.
These detect stale bridge/resource damage and retain the32-bit regression
requirement; they do not establish32-bit Chromium recovery (cached consumer
is64-bit). Existing pre-fault both-PE proofs are recorded in the baseline.

No production patch unless an actual failure is isolated. For a needed Wine/
compositor fix, first preserve a discriminating failing reproduction, implement
the smallest relevant fix, add regression coverage and rerun affected original
groups on both PEs as appropriate. Update STOCK_CHANGES for stock Wine changes.
Do not fix unrelated inherited upstream failures or irrelevant API gaps here.

## Acceptance: every requirement must be evidenced

| Requirement | Required proof |
| --- | --- |
| Healthy accelerated start | Explicit host/client/outer renderer checks; valid initial WebGL1/2 pixels and front content |
| Correct fault target | One owned GPU child; PID + creation time revalidated immediately before termination; no global kill |
| Actual recovery | Exactly one expected non-clean GPU exit and a different replacement identity; browser-main, renderer and compositor identities survive |
| Genuine context loss | Old WebGL1/2 contexts receive loss events; newly created contexts available/not lost |
| Correct recovered rendering | Red default pixel, green FBO pixel, complete framebuffer/errors0 for both contexts; later frames advance |
| No silent fallback | Unchanged backend/features and GPU compositing enabled; post-fault native/outer accelerated verification; no nested software renderer |
| Actual recovered front | Event-aligned settled captures, deterministic content/rim checks and inspected final image; not RAF/readback alone |
| No hidden second failure | Whole-run log scan after result until exit; no extra GPU crash, renderer exit, fatal/Skia/shared-context/output failure |
| Repeatability/regression | Two independent one-fault passes plus matched healthy control; post-fault both-PE pixel and WebView2 controls pass |
| Bounded lifetime | Host exit0 and diagnostic0; observer complete; owned servers/children/compositor gone; guest and launcher shut down cleanly |

Resource sampling is diagnostic only: record CPU/memory and process churn,
but do not equate a phase label, summed RSS, virtual GPU identity or resumed
animation with idle-power, GPU utilization, leak-free or zero-copy proof.

## Failure triage and final deliverables

Classify first failure as staging/preflight, ownership, GPU replacement,
WebGL/ANGLE initialization, native DMA-BUF/bridge attach, compositor front
presentation, observer/capture, or cleanup. Preserve exact command, timestamps,
PID/start identities, before/after backend/features, full child-exit timeline,
context-loss counts, pixel values and actual front PNGs. A missing helper is
a harness failure, not evidence of a Wine rendering regression.

Do not treat a failed run as inconclusive success because a later run passes.
Record any repair and rerun exact failing controls. If an acceptance cell is
unavailable, state the specific missing evidence rather than claiming completion.

Deliver a committed/pushed scoped report with run directories, revision/hashes,
commands, results matrix, timing/cleanup and remaining issues. Keep raw private
artifacts on D:; publish only original code and non-proprietary summaries.
Update continuation notes with machine power state and exact next step.
No master merge or full G4-complete claim from this task alone.

Check account usage before execution and periodically. At5% remaining, finish
the bounded current operation, clean owned processes, commit/push checkpoint
and continuation notes, then schedule after reset per standing instructions.
Never consume the one-time reset credit. This document itself schedules nothing.
