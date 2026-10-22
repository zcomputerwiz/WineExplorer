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

Use CHROMIUM_GRAPHICS_G4_VISIBLE_WEBGL_PLAN_20261022.md (2a1c4400).
Luna agent Dirac owns implementation/tests only; parent independently reviews
before any live control or fault trial. Three no-fault timing runs had correct
fronts before page readback (reports ed5229e8/e1eca023), without explaining or
fixing the intermittent stale front. Keep that failure and historical post-fault
blanking open. The next high-value coverage is actual visible WebGL1/2 surfaces,
not offscreen readback alone. No production patch or master merge is authorized.

After implementation review, dispatch one fresh-prefix visible no-fault timing
control in the owned D:-backed VirGL guest. Inspect actual images before separately
authorizing one visible GPU-child recovery trial. Preserve failures; do not retry
until success. Use the existing persistent launcher/identity stopper and bounded
cleanup procedure. An unhealthy pre-fault image must prevent fault injection.

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
