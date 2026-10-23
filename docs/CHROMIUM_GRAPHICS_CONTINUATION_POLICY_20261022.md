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

Latest milestone: selected-stack R1 executed ONCE,0c4288ee; parent independently
reviewed actual white front/122 archive hashes/19 binary+15 deployed source hashes,
post native/Linux identities, privateLO and graceful shutdown, f41feae5. Eight
selected stacks observed, primary readiness timeout preserved. Own pinned Wine
win32u RVA0x150c4 is NtUserMsgWaitForMultipleObjectsEx syscall return, not a cause.
Calibration12799159 passed once; do not repeat calibration or completed browser
arms. Earlier scoped Electron/WebView2/pixel results remain valid, not full Edge.

Current blocker: guest free725,929,984 bytes, below1GiB. No further browser/profile/
staging may ignore gate; no evidence deletion. Next is CAPACITY_PLAN_20261023
Stage A OFFLINE implementation/read-only GPT proof/tests/preparation; Luna owns
that bounded implementation. Parent reviews before any image creation/resize or
VM boot. Automatic GROWROOT runs on boot: larger-disk boot IS mutation. Preserve
original64GiB system.qcow2 AND Debian backing, use only a new D:80GiB top layer
if separately approved. No downward commit/rebase/compaction/in-place resize.
Later EDGE_NAVIGATION_TRACE_TODO_20261023 is research-only, NOT run authority.

Read latest HEAD reports first. EDGE_STACK_CHECKPOINT_20261023 and descriptions
below are historical, not current next-step instructions. Source checkout rules
remain current. Current window reset15:27:29 UTC Oct23; live usage governs.

Read CHROMIUM_GRAPHICS_G4_SOURCE_WORKTREE_20261023.md FIRST: main D: checkout
is now master and must be left alone. Continue tracked work in the existing owned
C:/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer worktree on
codex/g4-r1-completion, explicit cwd, never stage its unused Wine gitlink mismatch.
Private VM/runtime/evidence storage stays D:. Path portability and HTTP diagnostic
are COMPLETE; read EDGE_HTTP_R1 and EDGE_OWNED_THREAD_PLAN_20261023 next. Latest live
limits govern; at the
05:24 UTC wake they were0% five-hour/30% weekly, reset10:24:23 UTC.

The navigation checkpoint and loopback plan below are historical starting context,
not instructions to repeat controls. Current five-hour reset10:24:23 UTC Oct23;
latest usage always governs. Parent accepted the HTTP implementation after146
independent focused tests/no skips, actual fixture/controller/socket checks and
complete source/helper/preparation audit. One HTTP arm715360ea timed out: full
637-byte HTML response, no scene.js request or sampled renderer/readiness, white
front. Parent viewed actual PNG and independently rehashed107 files, checked
live source/binary/lo/backend/budget records and QMP/QEMU/Xvfb/controller exit0.
No file-only/sandbox/token/graphics cause established. No retry authorized.
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
One ordinary private-prefix MSI install passed (b340f8a7), parent independently
checked all17 archive hashes, actual success/cleanup/shutdown evidence. No repeat
installation needed. The installed executable/registration survived a later
ordinary Wineboot; on-disk pending-operation snapshots are not proof the live
queue was processed. Prefix/payload remain private on the owned D:-backed guest.

Luna implemented headful smoke18363266/f395aa2d; parent independently passed all87
focused Linux tests plus actual-fixture Node tests and verified nine staged members,
five helpers/provenance reference before dispatch. ONE real browser trial FAILED
readiness (6fccc595), with visible owned Edge window/GPU child and FileURLLoader
Start but no observed renderer/fixture readiness. No front PNG was reached. Parent
independently rehashed all280 archived files and checked graceful shutdown. Cause
is unproven: not automatically sandbox/graphics/IO. Keep r1 failure intact.

Instrumented startup65081f50 proves the browser read all637 HTML bytes, but no
renderer/readiness arrived; actual terminal front is white. Parent viewed it and
verified all105 archive hashes/clean shutdown. Private-loopback-UP same-file control
50f3da4f also timed out, with verified private network at start/end, white front,
another105 independently checked hashes and clean shutdown. Loopback DOWN alone
does not explain the failure. Read the latest checkpoint for exact evidence.

Next Luna milestone is OFFLINE implementation of the explicit owned-browser
read-only thread/wait inventory under EDGE_OWNED_THREAD_PLAN_20261023. Parent
reviews source/tests/manifest before one later diagnostic. No debugger/attach,
suspend, token privilege changes or broad fallback; Win32 and Linux IDs remain
separately labelled. Same installed prefix, NEW profile, original HTTP fixture;
no reinstall, security/backend change, repeated trial or speculative production
fix. No master merge; keep historical stale/blanking uncertainty open.

## Priority queue after this rendering blocker

1. Diagnose the current full Edge startup/readiness blocker before retrying its
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
