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
  square and green background before the checkpoint. The front observer must
  decode and pass the front PNG before acknowledging it; Electron then captures
  its client page and waits for a separate observer pixel verdict. A failed
  healthy checkpoint therefore prevents fault injection.
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
  — 21 tests passed. `python3 -m py_compile check-g4-webview2.py
  graphics_frame_pixels.py` passed. Windows Node syntax validation,
  `node.exe --check scripts/g4-electron-probe/main.js`, passed. Linux Node.js
  was unavailable, so the JavaScript syntax check ran on the Windows host.
- Final `git diff --check` passed.

## Limits and continuation

No VM or guest payload was staged. No healthy live run, fault injection, image
inspection, causal diagnosis, production fix, repeated recovery trial or
post-fault controls have been performed. Synthetic PNG tests do not establish
that the observed Electron and Wine coordinate systems align in the live
VirGL guest. The first authorized experiment must verify the healthy pre-fault
front and page pixel verdicts; the fixture will now refuse to inject a fault if
either fails. Only after that baseline passes should the parent review authorize
the next phases. Use the already-owned
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022` overlay and its private build/runtime;
do not touch the unrelated active VM or other checkouts.

No production fix or G4 acceptance claim is made here.
