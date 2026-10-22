<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review and next-window checkpoint

Branch: codex/g4-r1-completion. Use current HEAD and preserve other agents.
Parent plans/reviews; GPT-6 Luna agent Dirac implements (agent ID
01a163b6-80db-7cb1-9ab3-7687bbd04067). No master merge is authorized.

## Reviewed this window

- Three no-fault offscreen-canvas timing collections were healthy and had
  correct front/page pixels. Parent inspected an actual front and verified all
  43 archived file hashes in each repeat. Reports ed5229e8/e1eca023. This is
  bounded non-reproduction, NOT a fix for intermittent stale fronts or earlier
  post-fault whole-window blanking. Keep those distinct failures open.
- Visible-canvas implementation eecf8bb3 is reviewed for a bounded live control.
  Parent independently ran all 90 Linux tests (74 focused plus 16 remaining)
  and all four executable Node controls: PASS. Code review includes actual
  scene observation, stage-distinct WebGL clears, independent front/page pixel
  checks, geometry/identity gates and incidental-capture suppression. An early
  regex-escaping error was corrected before commit and actual fixture execution
  covers it. No Wine/compositor production change was made.
- Visible no-fault report 69d6c22f: all five fronts plus page passed scene and
  both canvas checks, 1024/1024 matching pixels per canvas per image. Parent
  inspected actual first front; all five fronts are byte-identical. Independently
  verified all 43 archive hashes, source/binary verification and exits.
  Health/collection/experiment-valid flags are true. Timing classification stays
  invalid/inconclusive: first whole observation lasted 713.703 ms and crossed
  the 250 ms slot. This is a healthy visual baseline, NOT on-time sampling or
  presentation-latency proof. No retry/fault/trace occurred.

Latest private evidence is under
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/visible-webgl-timing-r1-20261022/`.
Launcher result independently confirms graceful QMP powerdown, QEMU exit 0 and
both children stopped; Windows WSL record is exited/0 at 15:56:18.802Z.
No owned guest/compositor/Wine test session is left running.

## Next bounded step

Read VISIBLE_WEBGL_PLAN, IMPLEMENTATION and TIMING_R1 reports first, then the
Luna offline-preparation note (651cbe68). Offline preparation is for
`visible-webgl-recovery-r1-20261022`, marked PREPARED_NOT_RUN, on D:. No recovery
trial ran in this window. Its preflight archive hashes match the reviewed source
and its exact command is --gpu-recovery --visible-webgl --seconds 45, VirGL GL,
owned GLES2 compositor. The historically named private presentation-timing-trial.py
now prepares recovery; do not assume its old timing argv. Archived copies preserve
earlier procedures. Parent must review its final preparation note/helper and
recheck manifest/source hashes and temporary credential availability before boot.

After reset and parent verification, dispatch ONE fresh-prefix visible recovery
trial to Luna, not another implementation or repeated timing run. The fixture
must validate fresh healthy pre-fault front AND page before terminating exactly
its owned GPU child. Preserve any failure; no retry to get a pass. Inspect actual
pre-fault magenta/cyan and recovered yellow/blue canvas regions, whole scene/frame,
recovered/2s/4s pairs, context-loss/recreation, stable browser/renderer/compositor,
exact backend/error/fault-count gates and verified graceful shutdown. Failed
pre-fault pixels mean NO fault. A failed trial returns to diagnosis.

Use only the existing D:-backed owned VirGL guest, persistent Windows controller,
offline isolated consumer, unique sockets and identity-validated stopper. Prepare
before boot; keep shutdown inside 900 seconds with 90 seconds reserved. Never
operate WineExplorer-VM/Qwen/SVGA sessions. No proprietary runtime/art publication,
sandbox bypass, capability spoofing, OOBE/token imports or assertions weakened.

Independent recovery repeat and post-fault x86_64/i686 strict pixels/WebView2
remain future gates. Actual full Edge, installed-shell visual matrix and efficiency
remain TODOs; Electron controls do not certify them or full G4/merge readiness.

## Usage and continuation

Final checkpoint observation: five-hour 95% used, weekly 15% used. No new boot
was permitted after the visual control; only bounded offline preparation/review.
Five-hour reset is 2026-10-22 19:37:02 UTC (14:37:02 America/Chicago).
At 95% used stop safely, commit/push and arrange continuation after reset.
Weekly stop remains 99% used. Never consume reset credits. Keep the existing
heartbeat enabled, updating it to this checkpoint/current HEAD; notify only
meaningful progress, failure, completion or required user action.

Offline preparation 651cbe68 is committed/pushed. Parent inspected the prepared
command, PREPARED_NOT_RUN marker and private procedure; next-window verification
still applies, particularly recreating the authorized mode-600 temporary key
before SSH. No source/test/VM changes remain uncommitted. The existing heartbeat
is being rescheduled to 14:40 America/Chicago (19:40 UTC), after the reset.
