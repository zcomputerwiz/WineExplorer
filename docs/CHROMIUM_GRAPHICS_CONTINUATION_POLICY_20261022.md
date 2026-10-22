<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium graphics continuation priorities — 2026-10-22

User direction: the parent plans and reviews bounded work; a GPT-6 Luna agent
implements it. Continue high-value Chromium/Edge graphics work until weekly
usage has **1% remaining**. Record lower-priority work as TODOs.

The historical WEEKLY_CHECKPOINT and earlier timing/visible-review checkpoints
predate the user's manual resets and the current results below. Their old weekly
stop and next-step descriptions are NOT current authority. Read the latest parent
checkpoint and inspect live usage/HEAD before acting.

Check account limits at each milestone and before a long guest run. At weekly
usedPercent >= 99, stop safely, publish a checkpoint and report the weekly
limit stop; do not schedule another pre-reset work cycle. At five-hour
usedPercent >= 95, finish only the bounded active operation, clean up, commit
and push continuation notes, and arrange continuation after that reset while
weekly usage remains below the stop threshold. Never consume the reset credit.

## Current selected work

Use CHROMIUM_GRAPHICS_G4_EDGE_INSTALL_PREFLIGHT_PLAN_20261022.md (fc08dc89).
The user refreshed both usage windows again; verified 0% at 19:18 UTC. New
five-hour reset is 2026-10-23 00:18:39 UTC. Latest usage always governs.
Visible implementation eecf8bb3 and healthy visual control 69d6c22f were reviewed;
then two independent single-fault visible recovery trials passed (50012793,
ed7f7ccc). Parent inspected actual final fronts, independently checked all 78
archive hashes, all recovery/capture gates and graceful QMP/QEMU/Windows exits.
This is scoped recovery evidence, not a historical stale/blanking fix or full G4.

Post-recovery x64/i686 own/foreign-full DComp exact 4096 pixels and strict real
WebView2 WebGL1/2 passed (9cb47a12, 9d8e6935). Parent inspected actual fronts,
all 44 archive hashes and actual i686 PE machine 0x014c, plus clean shutdowns.
The x64 host budget calculation failed; preserve that failure. The raw-UTC helper
75bba089/748482c6 repaired enforcement; parent independently passed all 33 focused
tests and both remaining live controls enforced its 650/435 guards. These are
fresh-process/prefix/reboot regression cells, not faulted-process lifetime proof.

Human expressly accepted current Edge terms and private D:-VM download/install.
Edge Stable 156.0.4314.42 MSI provenance dd87d264 was independently verified by
parent: size/SHA256 match Microsoft metadata and Valid Microsoft Authenticode.
Private artifact is on D:; no host/VM installation or browser execution yet.
Luna Dirac completed installer-only runner/tests/offline preparation in
9348b4be/f15ed581. Parent reviewed code/private procedure, independently passed
all 53 focused Linux tests with no skips, and verified the current v2 archive's
seven members, five current source hashes and five host-helper hashes. Read
CHROMIUM_GRAPHICS_G4_EDGE_INSTALL_REVIEW_CHECKPOINT_20261022.md before proceeding.
The VM/installer/browser have NOT run for this item. Resume the same Luna agent
for ONE private-prefix installer attempt after the usage reset, no automatic retries,
administrative-extraction substitution, extra components or security bypass.
No production patch/master merge; keep historical stale/blanking uncertainty open.

## Priority queue after this rendering blocker

1. Finish reviewed Edge installer prerequisite, then actual full Edge executable
   headful graphics smoke under FULL_EDGE_SMOKE_PLAN (consent granted). Install
   success is not browser/rendering acceptance; app-window smoke is not full tabbed
   browser/lifecycle acceptance. Preserve a concrete startup failure before fixing
   or coordinating any dependent token/setup API. No unreviewed worker imports.
2. Add real WebGL shader compile/draw/texture-sampling coverage in a bounded step:
   existing clear/FBO readbacks do not certify general WebGL workloads. Reuse
   original tests/fixtures where appropriate rather than a new browser framework.
3. Diagnose a reproducible current Chromium/Edge failure first. Historical stale
   front/blanking remains unexplained; new passes do not erase it or justify an
   unsupported production patch. Record full Edge separately from other consumers.

TODO after functional consumer blockers: installed Classic/Open Luna shell,
locked/unlocked taskbar/toolbars/tray visual matrix; copy/sharing efficiency;
unrelated inherited upstream API failures; alternate backend completeness.
These deferrals do not turn outstanding full-G4 or merge gates into passes.

Latest reports and Git HEAD govern implementation status. Earlier automation
prompts and historical checkpoints may describe already completed harness work;
inspect current state before acting. Preserve other agents' OOBE/security work,
private payloads and D:-resident storage requirements. No master merge is
authorized by this continuation policy.
