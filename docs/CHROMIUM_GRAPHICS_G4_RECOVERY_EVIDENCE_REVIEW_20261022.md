<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review: recovery evidence provenance

Read-only review on 2026-10-22 while Luna implements the bounded harness
corrections. No guest was started by this review.

The execution checkpoint says the offscreen-WebGL-canvas change was used in
trials 2 and 3. At least for trial 3, this claim is disproved by the saved
manifest and source history. The trial remains a valid negative rendering
observation, but does not establish that the offscreen change was deployed.

Evidence under D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/recovery-trial-03:

- results.json records main.js SHA256
  bbae69f715168249c487c44ad32d8be5dc28b63bfe8c1a8b1c690e40b8842337.
- `git show 4b01e419^:scripts/g4-electron-probe/main.js | sha256sum`
  produces exactly that hash.
- `git show 4b01e419:scripts/g4-electron-probe/main.js | sha256sum`
  produces 75a3c2437d58b1e72c75fa23c10438f91ed121e4ba40e209ca4b84b571267451.
- Actual inspected screen-18.png contains the full recovered fixture, caption,
  menu, scrollbar and the red test canvas. Actual inspected screen-20.png is
  entirely black, including the previously visible caption/frame.

The stale fixture undermines the prior claim that moving the canvas offscreen
was ruled out as relevant. It does not explain the black capture or invalidate
the separate no-fault control. Reproduce with the current verified deployed
fixture before asserting either result. The whole-window disappearance also
means page raster tile failures alone are not yet an adequate causal account;
investigate native surface/output/capture events with the paired page evidence.

Next live run must retain exact deployed helper/fixture hashes alongside source
revisions and binary hashes, with a fresh output directory. Capture failures
must preserve front and page evidence whenever capture remains possible.
The current parent-reviewed milestone still requires correction before VM
execution; synthetic test success is not live recovery acceptance.
