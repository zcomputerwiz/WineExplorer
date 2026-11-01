<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 recovery blanking — bounded harness milestone

Date: 2026-10-22. Branch: `codex/g4-r1-completion`. Starting HEAD:
`8216bd324b64b21059c4596049ba798d4eb627b0`.

This records phases 1–3 of
[`CHROMIUM_GRAPHICS_G4_RECOVERY_BLANKING_PLAN.md`](CHROMIUM_GRAPHICS_G4_RECOVERY_BLANKING_PLAN.md).
It is a harness and synthetic-test milestone, not GPU-recovery acceptance.

## Changes

- Added an ordered `pre_fault` checkpoint to the existing per-run nonce and
  capture-acknowledgement mechanism. The fixture freezes its existing blue
  square and green background before the checkpoint. The front observer
  records capture completion separately from its pixel verdict. Electron then
  captures its client page and waits for a separate observer pixel verdict. A
  failed healthy checkpoint therefore prevents fault injection after both
  images have been preserved.
- At each stage, recorded the actual HWND bounds, Electron content bounds,
  renderer viewport, marker rectangle, background color, PNG dimensions,
  compositor identity and capture time. The front oracle maps client marker
  coordinates through the observed content bounds and published native output
  scale. It compares recovered frame rims against the pre-fault frame and
  samples the central solid-color region of the fixture's known blue square
  and a known background pixel. It does not use
  glyph rasterization or scan the desktop for colored pixels.
- Kept the checks independent: compositor-front captures prove presented
  pixels, while Electron `capturePage()` captures prove client-page pixels.
  The parser requires both at `pre_fault`, `recovered`, `settled_2s` and
  `settled_4s`, along with the existing stage, run ID and identity checks.
- Added synthetic controls for blank/partial images, missing markers or rims,
  incorrect crop/scale, invalid stage order, missing captures, stale run IDs,
  observer failure, and pixel verdict failure. Existing context-loss,
  unexpected-process-exit and backend checks remain in place.

## Preflight and validation

- Account usage at start: 4% of the five-hour window used and 91% of the weekly
  window used. The available reset credit was not used.
- Root HEAD and branch matched the requested published state; root and
  submodules had no pre-existing edits. Submodule pins were `wine`
  `4315be83d75644f13500381dcec94dcdc5fa904e` and `wexcomp`
  `94faa651f9e267fd03157348b26ffb3c66082484`.
- WSL Debian was available. D: had about 1.8 TiB free; WSL root had about
  645 GiB free. `VBoxManage list runningvms` showed only the unrelated
  `WineExplorer-VM`; three `VBoxHeadless` processes belong to that VM. No
  `qemu-system-x86_64` process was present. No VM was booted or modified.
- Linux focused validation passed: from `scripts/`,
  `python3 -m unittest -v test_graphics_frame_pixels.py test_g4_consumer_harness.py`
  — 23 tests passed, including blank post-fault front with a paired page
  capture, pre-fault injection gating, valid 1.25× scale with nonzero client
  origin, and initial rim rejection. `python3 -m py_compile check-g4-webview2.py
  graphics_frame_pixels.py` passed. Windows Node syntax validation,
  `node.exe --check scripts/g4-electron-probe/main.js`, passed. Linux Node.js
  was unavailable, so the JavaScript syntax check ran on the Windows host.
- Final `git diff --check` passed.
- Usage at the parent-review correction: 6% of the five-hour window used and
  92% of the weekly window used. The reset credit remains untouched.
- Follow-up parser correction after parent review: the live observer and final
  evaluation now use the same exact-event parser, which separates page PNG
  records from their interleaved acknowledgements. Malformed page records or
  acknowledgements fail closed; malformed non-object front acknowledgements
  no longer cause an exception while evaluating the pre-fault gate. The
  focused suite now passes 24 tests, including interleaved events and malformed
  event controls.

## Limits and continuation

No VM or guest payload was staged. No new healthy live run, fault injection,
causal diagnosis, production fix, repeated recovery trial or post-fault controls
have been performed. Synthetic PNG tests do not establish that the observed
Electron and Wine coordinate systems align in the live VirGL guest. The updated
handshake preserves failed front and page evidence separately; it allows
post-fault page capture after a front pixel failure, while the healthy pre-fault
checkpoint refuses fault injection unless both views pass. Only after parent
review should the next phases run. Use the already-owned
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022` overlay and its private build/runtime;
do not touch the unrelated active VM or other checkouts.

No production fix or G4 acceptance claim is made here.

## Historical trial 03 provenance correction

Parent inspection of
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/recovery-trial-03` found
`screen-18.png` showing the complete window with the red canvas, and
`screen-20.png` entirely black, including caption and frame. The `main.js` hash
in that run's `results.json`,
`bbae69f715168249c487c44ad32d8be5dc28b63bfe8c1a8b1c690e40b8842337`, matches
`4b01e419^:scripts/g4-electron-probe/main.js`, not `4b01e419` (whose hash is
`75a3c2437d58b1e72c75fa23c10438f91ed121e4ba40e209ca4b84b571267451`). The
historical checkpoint's statement that trial 03 exercised the offscreen-canvas
revision was incorrect. That trial does not test or reject the offscreen-canvas
change. The black full-window capture is evidence of capture/output loss, but
does not identify its owning layer. Before any next live run, stage the exact
current helper and fixture revisions, record their hashes in a deployment
manifest, and verify the guest's files against that manifest.

Lower-priority TODO after recovery diagnosis: revisit installed-shell theme
acceptance and sharing efficiency; neither is part of this recovery slice.
