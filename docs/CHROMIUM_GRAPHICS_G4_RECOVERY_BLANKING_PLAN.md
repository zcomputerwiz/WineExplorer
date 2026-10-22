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

- Committed/pushed instrumentation and pixel tests, then a separately reviewable
  production fix if required, including any Wine/compositor submodule commits.
- A report identifying the first failing layer with exact run paths, revisions,
  commands, timestamps, decoded pixel results, test results and inspected images.
- A results table separating healthy control, reproduction, fixed repeats and
  post-fault controls. Mark blocked or unrun cells explicitly. This slice alone
  does not establish full G4 or master merge readiness.
- Updated continuation notes with guest power/process state and the exact next
  command. Shut down only owned processes/guest after bounded work.

At the standing 5% remaining usage threshold, finish the bounded operation,
commit/push and checkpoint. Preserve the existing heartbeat and leave the reset
credit untouched. If no causal diagnosis is established, deliver the narrowed
evidence and next experiment rather than an unsupported production patch.
