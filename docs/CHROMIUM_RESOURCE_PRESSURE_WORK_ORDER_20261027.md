<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review and Antigravity next step: test actual memory-pressure inputs

## Combined build comparison reviewed

Latest root7863e721 includes loader correction4628119b, beyond earlierce18596a
delivery. Winefffa7de0df386ae16c4d47b1d0e450d8b7fbcbd3 is an actual two-parent
merge ofcb1d117 andae5fec1; parent verified both ancestor relationships, reviewed
the integration dispatch/loader selection and selected token/process/handle-table
changes. Parent33 host tests pass6.456s,0 skips. Worker-reported full build/real
PE unit counts remain worker evidence, not a parent rerun/security certification.
Keep the combined build as an experimental candidate; no master/global pin update.

Parent read actual final consumer result/API/native journal/QMP result and viewed
front: still Aw,Snap RESULT_CODE_KILLED. Native held browser220 times out45000ms;
metadata21/21 passes; HTTPempty; cleanup complete; actual QMPgraceful/QEMU0/no
fallback recorded. Loader check now passes. Thus token integration alone did NOT
resolve page load. Do not advertise it as a successful browser/sandbox result.
Keep useful combined implementation/build for the next controlled experiment.

Deployment attempts and stage clearing are disclosed in worker report. Preserve
all existing attempt/preflight folders and stop clearing/reusing consumed paths;
use a fresh named output for the next authorized test, without rebuilding an
unrelated harness. Do not silently call all intermediate attempts one fresh run.

## Why this next experiment

Pinned upstream source provides a narrow concrete candidate to check:
https://chromium.googlesource.com/chromium/src/+/7d9824099030f5c28c939ba08654562faaad7c09/components/memory_pressure/system_memory_pressure_evaluator_win.cc
The evaluator reads GlobalMemoryStatusEx and uses ullAvailPhys/ullAvailPageFile
to determine pressure (including feature-dependent available-commit thresholds).
Repeated discard requests justify measuring these inputs, NOT assuming a bug.
Pinned page_discarding_helper.cc158 logs before candidate selection; target0 can
mean no optional reclaim_target, not zero available memory or proof of a discard:
https://chromium.googlesource.com/chromium/src/+/7d9824099030f5c28c939ba08654562faaad7c09/chrome/browser/performance_manager/policies/page_discarding_helper.cc

## Assigned task: small compatibility check, no harness redesign

Antigravity uses own NEW D: root codex/chromium-resource-pressure from current
token integration7863e721 (or own stopped integration checkout with new branch).
Retain Winefffa7de full matched client/server candidate. No new token stack or
unrelated security/UAC work, no full rebuild just to investigate warnings.

1. Read the exact pinned Chromium threshold/default/caller logic and current
   combined Wine GlobalMemoryStatusEx implementation. State the actual predicate
   being tested. Do not infer that page_discarding_helper's log proves tab kill.
2. In ONE owned VM session, obtain bounded real MEMORYSTATUSEX fields (return/
   LastError, load,total/available physical,total/available pagefile/virtual),
   preferably using a tiny original read-only PE. Compare same-time guest
   MemAvailable/MemTotal/SwapFree/CommitLimit/Committed_AS. No generic probe
   framework, elaborate new identity protocol or exhaustive memory-API matrix.
   Existing21-check metadata phase need not be redesigned. Source-only focused
   tests are appropriate only if changing actual behavior.
3. The prior VM has4GiB. A controlled8GiB guest-RAM comparison is authorized if
   source/data supports real resource pressure; original normal browser flags,
   sandbox/features and combined build unchanged. Use a NEW own private launcher
   copy/parameter for the RAM cell, preserve original launcher. All disks still
   existing owned D:R4; no new image/download/reinstall. Check host available
   memory first and never touch other VMs. If choosing8GiB, label it as the RAM
   comparison, not a Wine-code fix. Avoid unnecessary baseline replay/reboot loops.
4. If Wine memory values are demonstrably wrong and source identifies a small
   real compatibility fix, implement that on an OWN Wine child branch offffa7de,
   focused functional tests and compatible incremental OWN build. No fabricated
   free-memory/success values or disabled memory-pressure/tab-discard features.
   Preserve real resource semantics. Do not modify loaded/shared builds.
5. Perform at most ONE fresh normal Chromium page control for the supported
   memory hypothesis (either justified corrected API build or RAM comparison,
   not a matrix of both). Keep actual HTTP HTML/JS/readiness/front and native
   status separate. If memory values/pressure are normal, report that finding
   and avoid an identical browser replay; propose the next specific IPC/network
   candidate from retained evidence instead of pretending a memory fix exists.

Fresh stage/output/profile/helper paths, no deletion/overwrite of previous guest
or host evidence. Reuse private lawful runtime/current compatible build; no full
build unless needed for a demonstrated code change. Normal8flags/originalHTTP/
privateLO/unique sockets/actualVirGL/space/idleness/current deadlines/exact owned
shutdown stay. No debugger/attach/suspend/security bypass, memory-policy flags,
second browser retry or unbounded logging. All private files D: or D-backed WSL.
At95%five-hour bounded cleanup/checkpoint/push; weekly99%stop, nevercredits.

Commit AND push narrow code/test/report docs/CHROMIUM_RESOURCE_PRESSURE_RESULT_20261027.md
and any OWN Wine fix branch. Include source predicate, actual measured values,
exact changed variable, actual page outcome and remaining blocker. Reply codex
then stop. Goal is a concrete explanation/fix for Chromium, not a perfect harness.
