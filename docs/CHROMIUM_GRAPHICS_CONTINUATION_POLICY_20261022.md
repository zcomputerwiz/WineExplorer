<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium graphics continuation priorities — 2026-10-22

User direction: the parent plans and reviews bounded work; a GPT-6 Luna agent
implements it. Continue high-value Chromium/Edge graphics work until weekly
usage has **1% remaining**. Record lower-priority work as TODOs.

Current state is superseded by
CHROMIUM_GRAPHICS_G4_WEEKLY_CHECKPOINT_20261022.md: weekly stop reached at 99%
used. Read that checkpoint before the historical selected-work notes below.

Resume update: the user subsequently used a full reset and authorized continued
work. Both windows were verified at 0% used. Follow
CHROMIUM_GRAPHICS_G4_PRESENTATION_TIMING_PLAN_20261022.md for the next bounded
step; the old weekly stop applies only to the exhausted pre-reset window.

Check account limits at each milestone and before a long guest run. At weekly
usedPercent >= 99, stop safely, publish a checkpoint and report the weekly
limit stop; do not schedule another pre-reset work cycle. At five-hour
usedPercent >= 95, finish only the bounded active operation, clean up, commit
and push continuation notes, and arrange continuation after that reset while
weekly usage remains below the stop threshold. Never consume the reset credit.

## Current selected work

Use CHROMIUM_GRAPHICS_G4_POST_RECOVERY_CONTROLS_PLAN_20261022.md (2a1a88de).
The user refreshed both usage windows again; verified 0% at 19:18 UTC. New
five-hour reset is 2026-10-23 00:18:39 UTC. Latest usage always governs.
Visible implementation eecf8bb3 and healthy visual control 69d6c22f were reviewed;
then two independent single-fault visible recovery trials passed (50012793,
ed7f7ccc). Parent inspected actual final fronts, independently checked all 78
archive hashes, all recovery/capture gates and graceful QMP/QEMU/Windows exits.
This is scoped recovery evidence, not a historical stale/blanking fix or full G4.

Luna agent Dirac now prepares the three regression controls for parent review:
x86_64/i686 own/foreign-full DComp exact 4096 pixels, then strict real WebView2
WebGL1/2. No new GPU fault. Complete nested source dependencies must be staged
before boot. Parent reviews manifests before dispatch; preserve failures and
stop on first failed control, not repeated attempts to obtain a pass. These are
fresh-process/prefix controls after a reboot; do not claim faulted-process lifetime
or pre-reboot GPU-global resource survival from them. No production patch or
master merge is authorized. Keep historical stale-front/blanking uncertainty open.

## Priority queue after this rendering blocker

1. Complete accelerated recovery acceptance and matched WebView2/32-bit
   regressions; only close cells supported by actual front images and logs.
   High-priority remaining consumer coverage: visible WebGL1/2 surfaces after
   GPU-child recovery. The current recovery fixture moves its WebGL canvases
   offscreen; the historical failing fixture displayed them. Offscreen exact
   readback plus intact CSS/front content cannot certify visible WebGL
   composition, nor explain the historical all-black-window failure. After
   the current repeat/regression slice, scope a controlled visible-canvas
   variant with actual canvas-region front/page pixel assertions and unchanged
   ownership/backend/error gates. Compare matched source/binary/capture timing;
   preserve any difference before proposing a production fix. This affects
   genuine browser graphics workloads and belongs ahead of cosmetic TODOs.
2. Investigate any remaining reproducible real Chromium/Edge consumer rendering
   or startup failure on the supported accelerated route. Scope a concrete
   failing consumer before implementing additional APIs.
3. Record actual Edge evidence separately from Electron and WebView2: existing
   consumer passes cannot automatically certify the full Edge browser.

TODO after functional consumer blockers: installed Classic/Open Luna shell,
locked/unlocked taskbar/toolbars/tray visual matrix; copy/sharing efficiency;
unrelated inherited upstream API failures; alternate backend completeness.
These deferrals do not turn outstanding full-G4 or merge gates into passes.

Latest reports and Git HEAD govern implementation status. Earlier automation
prompts and historical checkpoints may describe already completed harness work;
inspect current state before acting. Preserve other agents' OOBE/security work,
private payloads and D:-resident storage requirements. No master merge is
authorized by this continuation policy.
