<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Weekly-budget checkpoint — 2026-10-22

Stopped at the user's threshold: weekly usedPercent 99 (1% remaining),
five-hour usedPercent 50. Reset credit remains untouched. Do not start more
implementation or guest tests in this weekly window. Preserve the enabled
heartbeat and the standing usage checks.

Branch: codex/g4-r1-completion. Published parent before this note: 397f793f.
Wine 4315be83d75644f13500381dcec94dcdc5fa904e and compositor
94faa651f9e267fd03157348b26ffb3c66082484 remain unchanged. Parent plans/reviews;
GPT-6 Luna implements bounded steps. Dirac completed the assigned work and has
no pending guest action. Other agents' OOBE/security work remains separate.

## Verified progress

The harness now validates actual front/page pixels and frame rims at pre_fault
and three recovered stages, records native/content geometry and scale, preserves
failed pairs, and distinguishes capture completion from pixel success. Bad
healthy pixels block injection. Parent independently ran 48 Linux tests and
both executable Node controls after the final code changes; they passed.
The renderer-frame helper is recovery-only and has renderer plus browser-main
deadlines. Launch/stop now records and verifies Windows/Linux process identities
and preserves final QMP/exit evidence.

One revised-fixture accelerated recovery trial passed: see
CHROMIUM_GRAPHICS_G4_FIXED_TIMELINE_LIVE_TRIAL_20261022.md (5096d87c).
Parent inspected the actual settled image and decoded dense late captures:
full window present, no previous alternating all-black captures. All context,
pixel, backend and process-continuity gates passed. Graceful powerdown, QEMU
exit 0 and Windows WSL exit 0 were independently verified. This is one pass,
not a root-cause assignment for the historical failure.

The independent repeat did NOT inject a fault: see
CHROMIUM_GRAPHICS_G4_RECOVERY_REPEAT_LIVE_TRIAL_20261022.md (397f793f).
Healthy frozen control passed; pre_fault front pixels failed while page pixels
passed. Parent inspected the front image: it still says Frames: 14 and shows
the old marker position. The two rAF callbacks completed in 27 ms, but that did
not prove presentation. The strict gate refused injection. Repeat shutdown
was independently verified signal-driven/graceful/QEMU exit 0, both children
stopped. No native DComp or WebView2 post-fault controls were started.

Private evidence roots, all on host D:
- D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/recovery-r1-fixed-timeline-20261022/
- D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/recovery-r1-independent-repeat-20261022/

Earlier failures and provenance correction remain retained. The historical
trial-03 fixture hash predates the offscreen-canvas change; do not repeat the
old report's incorrect deployed-version claim.

## Next bounded plan — no fault injection

First read the blanking plan, this note and the repeat report. Do not increase
arbitrary sleeps or repeat pixel captures until they pass. Prepare a separate
no-fault diagnostic that freezes the same original scene and records a fixed
front-capture timeline (for example immediate, 250 ms, 1 s and 3 s, with actual
monotonic timestamps and durations), before any capturePage call. Capture the
page only after that sequence, then capture the front again. Keep source and
deployed hashes, geometry, marker/rim verdicts and every failed frame.

This separates spontaneous presentation delay from a capturePage observer
effect without allowing either to masquerade as recovery success. Use explicit
diagnostic classification, deterministic callback/timeline tests, and parent
review before another live run. If the front remains stale, correlate existing
Wine native-Wayland surface/commit and compositor logs before selecting a
production fix. No production rendering fix is justified by current evidence.

After resolving this healthy-readiness discrepancy: independent recovery repeat,
both-PE own/foreign-full DComp 4096-pixel controls, and strict real WebView2.
Visible WebGL1/2 recovery is also high priority: current recovery canvases are
offscreen, whereas the historical failing fixture displayed them. Full Edge,
installed shell/theme and efficiency claims remain unproven/deferred as recorded.

Only reuse the owned D:-backed VirGL guest, persistent session controller and
identity-validated stopper. Stage transitive dependencies before testing, budget
cleanup inside 900 seconds, preserve other VMs, and commit/push verified steps.
No master merge. Leave all incomplete cells explicitly open.
