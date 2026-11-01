<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Plan: resolve intermittent front-content loss after accelerated GPU recovery

Prepared on 2026-10-22 against `fe73e48e`, branch `codex/g4-r1-completion`.
This supplements the accelerated recovery plan and execution checkpoint. The
user authorized implementation after preparation of this plan by GPT-6 Luna.

## Selected outcome

Identify and repair the first loss of visible page content after a single
Chromium GPU-child restart on the accelerated VirGL route. Finish this recovery
acceptance slice with two independent successful trials and post-fault controls.
This is the highest-value next item because actual front content intermittently
disappears despite successful context recreation and WebGL readback; it blocks
the existing recovery gate. Installed-shell themes and sharing efficiency can
follow once this rendering defect is resolved.

## Established evidence and uncertainty

The execution checkpoint records three fault trials with intermittent blank or
partial compositor captures, while a no-fault frozen-scene control has twelve
identical intact captures. A GPU replacement, stable renderer, resumed frames,
and WebGL1/2 pixel checks do not establish successful front presentation.

Commit `11c03894` already added run-nonce/stage capture acknowledgements,
browser/renderer/compositor identity checks, and paired front/page capture
support. Reuse it. Its latest validation was Windows unit testing with an fcntl
shim; Linux execution and live guest validation remain outstanding. The
current capture verdict checks presence, dimensions, ordering and identities;
it does not inspect image content. Do not count it as a visual pixel verdict.

The failure may originate in Chromium raster recovery, Wine surface/buffer
lifetime, compositor presentation, or capture itself. Existing logs do not
establish which. Sequential front and capturePage images are not atomic;
capturePage may itself cause rendering/readback work and affect the result.

## Execution sequence

Host/guest path reminder: Debian WSL is the root-owned **host** for QEMU. Its
launcher is `/mnt/d/DeepThought/Projects/WineExplorer/build/g4-virgl-20261022/launch.py`.
Run it through `wsl -d Debian --exec unshare --mount --propagation private
python3 -B <launcher>` after assigning a fresh output directory. The account
`debian` and `/home/debian/wex-g4` are **inside the QEMU guest** and become
accessible through loopback SSH port 2235 after boot; their absence on the WSL
host is expected. The existing lab keypair is on D: under VirtualBox_VMs;
use the private per-guest known_hosts file and never print private credentials.
See the VirGL report's Guest and containment section for launch details.

1. **Re-establish the lab.** Read the execution checkpoint and original recovery
   plan. Check account usage, branch/submodule status, VM ownership/power state,
   WSL access and free space. Use only the existing D:-backed
   `WEX-G4-VirGL-20261022` overlay and its owned build/runtime. Record full source
   revisions, deployed hashes and helper manifest. Stage all dependencies from
   the committed source. Run the focused suites on Linux without the Windows
   fcntl shim; a skip is not coverage. Resolve staging/access failures before
   booting or faulting anything.

2. **Establish meaningful pixels before faulting.** Reuse the original fixture's
   background and blue-square markers plus observed window/client geometry.
   Add an explicit healthy pre-fault front checkpoint. Decode actual PNG data
   and validate dimensions, mapped client marker regions and applicable frame
   rims; preserve existing frame-pixel helpers. Validate the frozen recovered
   background and square at all three recovered stages. Exclude text antialiasing
   and decorations from page-versus-front comparisons. Derive coordinates from
   observed bounds/output scale, never by searching for any matching colored
   pixel on the desktop. Record sample counts, expected/actual colors, geometry
   and timestamps. Blank or partial images must fail even when nonempty.

3. **Add discriminating controls.** Cover intact, blank and partial-content
   images; wrong dimensions/scale/crop; missing marker or rim; stale stage/run
   data; missing capture; observer failure; and unchanged backend/process checks.
   Existing tests for context loss, extra exits and malformed capture events must
   remain. Use original synthetic images for unit tests. Commit/push this
   bounded harness step after its focused suites pass.

4. **Run the healthy control, then reproduce once.** Verify explicit accelerated
   VirGL client and GLES2 outer renderer, valid WebGL1/2 pixels, and healthy front
   content. Use fresh outputs and isolated prefixes. After a passing control,
   run one single-fault trial with stage acknowledgements, front/page images,
   timestamps and full logs. Keep the existing bounded deadlines. Save every
   failure before changing code. Inspect the real PNGs, not only JSON verdicts.

5. **Locate the first divergence.** Correlate page/front captures with the GPU
   exit/replacement, Chromium raster/SharedImage events, Wine native-Wayland
   surface/buffer attach/detach/commit/release events, and compositor output
   commits. Add only targeted diagnostics where evidence is missing. Log stable
   surface/buffer identities and monotonic timestamps in each process; align
   clocks explicitly rather than assuming elapsed times share an origin.
   Bound any dense capture burst to the observed failure interval. Run a matched
   front-only trial if capturePage changes the failure, and account for capture
   duration and scheduling. A correct page plus wrong front localizes the fault
   downstream only when the scene and observations are temporally consistent;
   it does not alone identify Wine versus compositor versus screenshot capture.

6. **Repair the proven owner.** Preserve a failing reproduction, patch the
   smallest demonstrated lifetime/damage/synchronization/raster defect, and add
   a regression that fails on the old implementation. Do not turn a fixture
   repaint, resize or repeated screenshot into a production recovery mechanism
   to hide the failure. Update STOCK_CHANGES for stock Wine changes. If evidence
   identifies only a harness defect, repair the harness and rerun the original
   failing sequence before crediting recovery.

7. **Close this slice.** Run two serial fresh-prefix single-fault trials with
   intact pre-fault and recovered/settled front pixels, real context-loss and
   recreation checks, stable browser/renderer/compositor identity, unchanged
   accelerated backend, exactly one expected GPU exit, and clean bounded exit.
   Then run both-PE own/foreign DComp pixel controls and strict real WebView2
   WebGL1/2 controls from the original plan. Inspect final front images. Preserve
   earlier failures with explanations; successful reruns do not erase them.

## Decision rules and scope

Never fault after a failed healthy baseline. Missing/invalid captures or
observation errors fail the run. Keep all existing no-fallback, ownership and
whole-log error checks. Optional P010 capability warnings already classified
in the checkpoint do not justify adding a broad new error allowlist.

Use serial runs and unique filesystem X sockets/output directories. Do not
rebuild a loaded binary during a probe. All VM storage and private runtime,
profile, screenshot and log evidence stays on host D:. Preserve other agents'
checkouts, OOBE work and VMs. Do not merge master, change sandbox policy, spoof
capabilities or broaden into host GPU resets, unrelated API completeness,
theme acceptance or efficiency work.

## Deliverables and stop conditions

### Next bounded experiment after the phase-4 pre-fault stop

The parent inspected the new paired pre_fault images: front at approximately
7555 ms still shows Frames: 2 and the old marker position, whereas page at
7589 ms shows the newly frozen scene. The later screen-8.png already shows
the correct frozen front. No fault occurred. This establishes an early stale
observation, not persistent rendering loss or its cause. The runner currently
requests capture immediately after changing the DOM; DOM command completion
does not establish that rendering has had an opportunity to present it.

Luna's next bounded harness change should wait for two renderer animation-frame
callbacks after each frozen-scene change before requesting the existing front
checkpoint. Use a bounded timeout that fails the run, record frame/timing
observations, and retain front-before-page capture order. Do not retry failed
pixel captures until they pass, force a repaint with capturePage first, or claim
that animation-frame callbacks themselves prove presentation. The strict actual
pixel gate still decides whether the healthy front is ready to permit a fault.
Add an executable callback-sequence test and timeout control, rather than only
asserting that a source string contains requestAnimationFrame.

Review this change before live execution. Then rerun the healthy no-fault
control and at most one recovery attempt with the unchanged strict pre-fault
gate. A stale front after the rendering-opportunity wait calls for a separate
fixed-timeline no-fault experiment; do not keep extending sleeps or bypass the
gate. Preserve the previous failed pair and compare actual new images.

Before boot, also resolve the previous launcher's missing final shutdown
record. The parent verified no QEMU process and ran offline qemu-img check:
no errors. This does not prove graceful guest shutdown. Require an owned launch
session that persists, a verified launcher PID/start identity, explicit SIGINT
to that identity for its existing QMP ACPI cleanup, and a complete final
shutdown/exit record. Diagnose premature launcher termination before reuse.

- Committed/pushed instrumentation and pixel tests, then a separately reviewable
  production fix if required, including any Wine/compositor submodule commits.
- A report identifying the first failing layer with exact run paths, revisions,
  commands, timestamps, decoded pixel results, test results and inspected images.
- A results table separating healthy control, reproduction, fixed repeats and
  post-fault controls. Mark blocked or unrun cells explicitly. This slice alone
  does not establish full G4 or master merge readiness.
- Updated continuation notes with guest power/process state and the exact next
  command. Shut down only owned processes/guest after bounded work.

### Repeat and regression slice after the first reviewed pass

The parent reviewed run `recovery-r1-fixed-timeline-20261022`: consumer and
visual gates passed, inspected settled front was intact, and decoded dense
captures from 12 through 32 seconds contained no all-black window. Independent
shutdown review confirmed signal-driven graceful QMP powerdown, QEMU exit 0,
both children stopped and Windows WSL exit 0. This is one revised-fixture pass;
the cause of the historical blank captures is still unassigned.

Next reuse the exact reviewed code and binary pins for one independent fresh
prefix/profile/output recovery trial, with one owned GPU fault and all existing
pre-fault/context/identity/backend/front/page checks. Retain a fresh healthy
control and deployed manifest. Inspect all named pairs and late timed captures.
Do not use a repeated-fault run as the second independent trial.

After a passing repeat, run `check-g4-foreign-present.py` for both x86_64 and
i686 with explicit GL/VirGL/GLES2 and both `--case own-dcomp` and
`--case foreign-full-dcomp`. Require each 4096/4096 actual front-pixel result,
producer exit and verified outer/client acceleration. Then run the existing
private WebView2 strict WebGL1/2 diagnostic with the approved cached host/runtime.
Stage/hash all transitive script/native-include dependencies first; record
actual commands and runtime/host identities, not inferred paths or test counts.

The 900-second launcher budget includes staging, prefix initialization, tests,
evidence collection and shutdown. Reserve at least 90 seconds for cleanup; if
the next control's bounded maximum does not fit, stop cleanly and checkpoint
that cell as unrun for a new boot. Record any reboot between fault and controls;
fresh-process controls do not prove resource lifetime in the faulted process.
Do not change production code on a passing diagnostic or generalize Electron
and WebView2 results to the full Edge browser, installed shell, efficiency or
full G4. Stop at the standing usage limits even if some controls remain unrun.

At the standing 5% remaining usage threshold, finish the bounded operation,
commit/push and checkpoint. Preserve the existing heartbeat and leave the reset
credit untouched. If no causal diagnosis is established, deliver the narrowed
evidence and next experiment rather than an unsupported production patch.
