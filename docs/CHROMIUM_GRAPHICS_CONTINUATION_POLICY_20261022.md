<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Chromium graphics continuation priorities — 2026-10-22

User direction: the parent plans and reviews bounded work; a GPT-6 Luna agent
implements it. Continue high-value Chromium/Edge graphics work until weekly
usage has **1% remaining**. Record lower-priority work as TODOs.

Check account limits at each milestone and before a long guest run. At weekly
usedPercent >= 99, stop safely, publish a checkpoint and report the weekly
limit stop; do not schedule another pre-reset work cycle. At five-hour
usedPercent >= 95, finish only the bounded active operation, clean up, commit
and push continuation notes, and arrange continuation after that reset while
weekly usage remains below the stop threshold. Never consume the reset credit.

## Current selected work

Use CHROMIUM_GRAPHICS_G4_RECOVERY_BLANKING_PLAN.md. Parent review of Luna commit
3bc142eb found corrections needed before any guest fault trial:

- Preserve paired page evidence when post-fault front pixels fail. Distinguish
  capture completion from a successful pixel verdict; the final run must fail.
- Establish initial rim validity rather than comparing later rims against an
  unvalidated baseline. Preserve failed capture metadata and pixel verdicts.
- Resume visible updates after pre-fault capture, then freeze for recovered
  captures. Validate content-origin offsets and a valid fractional-scale case.

Luna agent Dirac owns this bounded correction and its focused tests. Parent
independently reproduced the original 21 Linux tests; their passing result does
not close the identified coverage gaps. Parent review precedes VM execution.

After review: reserve only the owned D:-backed VirGL guest for serial healthy
control and one diagnostic recovery reproduction. Inspect actual evidence and
locate the failing layer before selecting any production fix. Request another
bounded Luna implementation step for a demonstrated defect; parent reviews the
patch and evidence before further acceptance trials.

## Priority queue after this rendering blocker

1. Complete accelerated recovery acceptance and matched WebView2/32-bit
   regressions; only close cells supported by actual front images and logs.
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
