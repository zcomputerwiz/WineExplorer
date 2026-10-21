<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 checkpoint: switch focus to Qwen OOBE review

SUPERSEDED for current graphics status: the user subsequently resumed graphics.
Read CHROMIUM_GRAPHICS_G4_REPAINT_VERIFIED_20261020.md first. The timeout below
was a probe timer-ID bug; the corrected full 22-group controls pass both PEs and
backends on the same Wine bc7011f. This note preserves the historical failed
investigation and OOBE handoff; do not treat its earlier WIP diagnosis as current.

User explicitly requested wrapping/committing current graphics work and switching
to reviewing Qwen's OOBE Components task. Do not continue graphics automatically
or claim G4 finished. No master merge. No other workers' files were edited.

## Stable step versus WIP

- Last verified parent: `97119874`, Wine
  `48000ca83d2240c4dd5d0101d4ebff5fc76f6b74`, both published on
  `codex/g4-r1-completion`. See BACKGROUND_20261020 and UNDERLAY.json reports.
  All 21 groups pass both PEs/software GL/lavapipe, G0-G3 and pacing pass, real
  WebGL1/2 and Electron lifecycle pass diagnostic checks; actual frozen window
  image inspected. This fixes GDI background preservation/resize/detach/recreate.
- Current Wine `bc7011fa4785383988faced54ff7d3e496a95c49` is **WIP, NOT VERIFIED**.
  Parent is the commit containing this note. Both published. Do not substitute
  the stable binary hashes/results for the current WIP build.
- The owned build currently contains the WIP, not the last verified revision:
  /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 (i386,x86_64).
  Source is its ../../wine tree; test scripts in ../../scripts.

## What the WIP tries and what failed

`application_background_expose` first paints a blue GDI background via real
WM_PAINT, without another application Commit/Present. Stable Wine 48000ca FAILs
both PEs in `background-expose-negative/`: backing is blue, screen stays green.

Candidate: window_surface_flush posts a private asynchronous repaint message
only after a dirty backing flush. Active DXGI target publishes a notification
window sink; its handler drains at most 64 queued duplicates and renders CURRENT
retained state with CLEAR_ONLY (no producer capture/rotation). A separate SRW
lock protects notification-window pointer lookup/AddRef against final Release;
no renderer/destruction runs under that lock. Detach/final Release remove only
their own sink. Ordinary non-DComp Release avoids the new global lock. No
production timer, polling thread, raw caller packet or public ABI/IID change.

Evidence root: /var/tmp/wex-g4-tree-20261020 (all paths below relative).

- background-expose-event-build.log and background-expose-final-build.log: build
  PASS. background-expose-event-{gl,vk}: WM_PAINT passes, occlusion setup FAILs
  (screen was still white, not a proven black occluder). Fixed fixture paints
  the STATIC cover through its own WM_PAINT/EndPaint and uses SS_NOTIFY.
- background-expose-observed-{gl,vk}: focused original repaint/actually observed
  black occluder/expose PASS both PEs; NO full acceptance from these passes.
- background-expose-full-22-{gl,vk}: strengthened consecutive-paint test FAILs
  both PEs. After blue then green WM_PAINT, backing is green but screen blue.
  A PeekMessage-only drain does not guarantee Wine's batched GDI idle flush.
- A real GetMessage idle cycle was added with a test-only 80 ms application timer
  (not production pacing), plus unpresented-blue-buffer/pending-offset checks,
  250 ms unchanged-path-counter check and queued-notification/final-release case.
  background-expose-final-22-{gl,vk}: x86_64 **TIMES OUT at 120 s**, after the old
  21 groups pass and before APP_WM_PAINT is printed. i686 is NOT RUN on that final
  test version. Earlier simple focused tests did run both PEs.
  Cause is UNDETERMINED: debug GetMessage/timer dispatch and possible dirty-flush
  notification feedback/starvation/reentrancy before changing assertions. A real
  blocking GetMessage loop must work; do not “fix” by deleting that proof or
  calling Commit/Present from the pixel reader. The timer itself also needs a
  secondary bounded escape in the test, not just the outer process timeout.
- background-expose-electron: lifecycle diagnostic PASS, 34 samples / 559
  post-reload frames, seven checks true, no observed GPU/Skia fatals/crashes.
- background-expose-webgl: strict real WebGL1/2 diagnostic PASS. These do NOT
  override the deterministic suite failure. No new frozen-image inspection on
  this event slice; previous inspection belongs to the stable underlay step.

Both timeout runners cleaned their OWN prefixes/server/Xvfb: results.json records
kill exit 0, wait exit 0, remaining_servers []. No owned tests intentionally left
running. No VM state changed for graphics; lawful cached payloads/images private.
An owned process search for the two test labels returned no matches (exit 1).

## Resume rules

Use scripts/run-isolated-graphics.py with a NEW per-run filesystem socket
directory through --socket-root /var/tmp/wex-g4-tree-20261020/isolated-sockets.
Never use the older shared-socket parallel commands. Never rebuild while an
owned probe uses the build. Preserve the WIP before experimenting; no reset/clean
or unchecked rollback. All tests use fresh output dirs and private prefixes.

After fixing the loop: repeat full 22 groups both PEs/GL/lavapipe, G0-G3, pacing,
real WebGL1/2, Electron lifecycle and actual frozen front-window inspection.
Add deterministic queued notification/Release and idle no-storm evidence. Remaining
soft border/AA, child/top-bottom/DirectX/layered integration, R2/R3, inherited
upstream failures, actual Classic/Luna/X11 compositor OFF/ON/Wayland and physical
older GPU gates remain OPEN. WSL has no /dev/dri. Never claim synthetic GTX470
is hardware or sandbox-request flags are containment evidence.

Usage last checked 52% primary / 33% weekly (later work consumed more); one-time
reset credit untouched. No new wake-up scheduled: user redirected priority to
OOBE. Earlier one-shot wake-up was already delivered.

## OOBE investigation handoff

Read-only discovery found the live unfinished work in D:/Projects/WineExplorer,
branch gemini/oobe-embedded-terms at be7dadb7, not the main graphics checkout.
14 tracked files changed plus 3 untracked acceptance/test files. Do NOT commit,
reset, switch, copy over or edit Qwen's active checkout without coordinating.
Reviewer's separate clean worktree:
C:/Users/Owner/.codex/worktrees/oobe-components-repair/WineExplorer,
codex/oobe-final-correctness at 6a298d28, containing the 300-line bounded work
order docs/OOBE_EMBEDDED_TERMS_COMPLETION_WORKORDER.md (read fully this turn).

Qwen's acceptance draft reports root synthetic suites/native PEs passing, but
live acceptance/deployed hashes/cleanup/blocker sections remain [FILL]. Treat
these as worker claims until independently checked. CoreFonts must stay REVIEW
ONLY, other adapters/install enablement out of scope. No worker messages sent.
One unelevated read-only git diff in D:/Projects/WineExplorer was denied; same
read with scoped escalation succeeded. No worker files were changed.
