<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 real GPU-child recovery and native-caption follow-up

2026-10-21, branch `codex/g4-r1-completion`, parent `d300e9f0`. Wine source remains
`1dcd479`, clean. **Not merge ready.** This step adds diagnostics/tests, not a
production fix. A speculative frame-invalidation change was tested and removed;
no Wine commit or submodule advance is included. Other agents/VMs are untouched.

## Final results

All evidence directories below are private, under
`/var/tmp/wex-g4-tree-20261020/` unless a Windows path is explicitly given.

| Evidence | Result | What it proves |
| --- | --- | --- |
| r3-recovery-published-gl | PASS, host0/diagnostic0 | One owned Electron GPU child terminated/replaced; correct context recreation, same ANGLE backend |
| r3-recovery-published-vk | PASS, host0/diagnostic0 | Same narrow recovery on explicit lavapipe |
| r3-recovery-normal-final-gl | PASS, exit0 | Unfaulted animation and all seven lifecycle checks, no checked crash signatures |
| r3-recovery-final-28-gl | PASS, both PEs exit0 | All28 established correctness groups, memory bound retained |
| r3-recovery-final-28-vk | PASS, both PEs exit0 | Same on explicit lavapipe |
| r1-caption-settled-final | FAIL_SETUP, both PEs exit1 | Plain-GDI caption sample black BEFORE D3D/DComp setup |
| caption-settled-x64.exe / caption-settled-x86.exe, native Windows | PASS, both exit0 | Settled real-caption pixels survive graphics setup and repeated Present on this native host |

Nine consumer-harness unit tests and four earlier graphics-harness unit tests
PASS. Existing assertions/normal-run crash rejection remain enforced. The new
caption diagnostic is separate from the existing28-group suite: it is an open
baseline/preservation gate, not a passed29th group or an approved deferral.

## Fault injection, ownership and independent checks

The original Electron fixture has a separate `--gpu-recovery` runner mode,
mutually exclusive with lifecycle/freeze/restart modes and requiring >=30 seconds.
It creates ordinary sandboxed renderer contexts with no preload/Node access.
After warmup, WebGL1 AND WebGL2 must produce exact red default pixels
`[255,0,0,255]`, exact green FBO pixels `[0,255,0,255]`, complete framebuffers,
zero checked GL errors and nonlost contexts. SwiftShader/software-rasterizer
fallback strings are rejected; the lab's explicitly selected Wine/Mesa software
backend is still labeled as such, not physical acceleration.

The fixture obtains exactly one GPU child from Electron's
[app metrics](https://www.electronjs.org/docs/latest/api/structures/process-metric),
revalidates PID AND creationTime immediately before injection and refuses to
kill if identity changes or any child/renderer already exited. `process.kill`
uses this owned Win32 child PID within the private Wine server; it is not a Linux
PID, user-global process search, system GPU reset or another worker's process.

Following one expected `child-process-gone` event, the test requires a replacement
GPU PID, unchanged renderer PID, actual WebGL context-loss events and newly
created contexts with the same exact pixel checks/backend strings. It compares
feature dictionaries before/after and requires gpu_compositing=enabled, continuing
frame progression and no renderer exit. Context creation is deliberate fixture
recovery, not a claim that arbitrary app resource contents automatically survive
device loss. The observed old contexts had already auto-restored by inspection
(isContextLost=false) but each had a genuine context-loss event; the new proof
does not fabricate loss via WEBGL_lose_context or reload the renderer/page.

The runner independently validates structured values, not just passed=true. It
checks whole-run child/renderer exit logs, including after the fixture's result
snapshot, and cross-checks the separate fault record. The unchanged normal
animation analyzer correctly reports diagnostic_passed=false for the injected
crash. ONLY this separate fault mode can accept exactly one GPU-crash log
signature with a complete recovery proof and zero other checked signatures.
Zero/two crashes, later renderer/GPU exits, incomplete records, stale PIDs,
wrong pixels, fallback changes and missing/unknown signature keys are rejected
by unit controls. No general crash allowlist or assertion relaxation was added.

Final GL: owned GPU448 ->832, renderer560 unchanged; recreated-context frames
140 ->186 over the observed follow-up. Final Vulkan: GPU448 ->836 in its different
private server, renderer560 unchanged; frames168 ->224. Both observed exactly
one GPU killed/exitCode1 event, one context-loss event per WebGL API, all ten
independent recovery checks true,34 samples and continuing late-frame deltas.
The repeated numeric PIDs across runs are distinct private Wine server identities.

The GL ANGLE string still uses Wine's synthetic NVIDIA GTX470 branding; it is
NOT physical Nvidia evidence. Vulkan reports ANGLE/Mesa llvmpipe through D3D11,
with the explicit Wine ICD `/usr/share/vulkan/icd.d/lvp_icd.json`. Neither run
switches to Chromium SwiftShader. Early feature-status output taken before GPU
initialization lists disabled features; the actual warmed-up before/after
snapshots and pixel tests are the applicable evidence. Video/WebGPU feature
flags in those dictionaries do not certify those deferred APIs.

Actual screen4/8/18/28 captures bracket the fault. Published screen28 captures
were inspected on both backends: coherent cream client background, single blue
square at x160, two red WebGL canvases, correct menu/input/button/scrollbar and
"GPU recovery verified" text. The native frame is incomplete as described below;
these images are NOT a whole-window/desktop pass. Captures and vendor payloads
remain private/untracked; no copied Microsoft art or binaries enter Git/ISO.

## Separate open caption issue, not attributed to GPU death

An initial fault screenshot had no painted caption/border. The before-fault
screen4 capture already has the same absence. The unfaulted, non-lifecycle control
`r3-gpu-no-fault-no-lifecycle-gl` also has it at screen18 with zero GPU-crash
signatures. Normal resize/minimize/restore controls display a painted frame.
Thus this is a real observed startup/non-client issue, not demonstrated damage
caused by the injected GPU exit.

`--nonclient-diagnostic` now creates a plain overlapped GDI window FIRST, explicitly
requests a frame redraw and waits for a stable, visible, nonblack caption-area
pixel for25 consecutive10ms observations (bounded3 seconds). It checks actual
screen ownership. Only after that baseline would it initialize D3D/DComp and
require exact caption preservation across setup/Present; no post-setup forced
repaint is used to make the comparison pass.

Both headless Wine PEs return `NONCLIENT_BASELINE ... color=000000` before any
D3D/DComp device is created. Both native Windows controls pass the settled baseline
and all later comparisons exactly. Native caption-settled executables are ignored
free-source outputs in `build/graphics-validation-20261021/`, hashes:

```text
x64 1bd3aeef68541694f83daa316da18d7d7a3f241b1e7e53f66ffda9ab195fd3f7
x86 bf67d1997be7ed830f36bdc3a1837d08dc6b013d16655cd5db3b6051dcc1c061
```

The headless baseline failure cannot establish a DComp preservation regression.
The startup frame remains OPEN and needs a plain-GDI/X11 message/surface/NC-paint
trace and an actual installed-shell/window-manager comparison. No openbox,
fluxbox, xfwm4 or xcompmgr executable is on this Debian PATH. Do not call the
problem environmental-only, waive it for Classic/Luna or claim a fix from a
resize workaround; neither its complete cause nor installed-desktop scope is proved.

## Identity, reproduction and cleanup

Owned source/build remain `/var/tmp/wex-g3-repair-20261018/clean-source` and its
combined WoW64 `build/wow64`. No win64-only build. Source is normalized-identical
to Wine1dcd479 after removing the experimental edit. Rebuilding the reverted
win32u source produced a different ELF identity; final native win32u.so SHA256 is
`f450466f4a2d7584051de1cc9bce99436aff84813632f506ef876afcfad62dce`.
Do not silently substitute the earlier cac4026... identity. Both final28-group
runs and published recovery runs use this final rebuilt binary. Other identities
are in each consumer results.json/stack_sha256, along with runtime/fixture hashes,
child command lines and graphics DLL mappings.

Private runtime remains Electron44.9.2 / Chromium152.0.7977.167, executable SHA256
`448da7f0a402b7299f87dc7dc896d48fd58a726ccbcf65b69820745f1bd38bf2`.
No sandbox disable, browser software-renderer switch, version spoof, system
graphics DLL replacement or security/OOBE branch import. Sandbox request/feature
status are not proof of containment.

Use the prior report's isolated Electron command with `--gpu-recovery --seconds 35`
instead of --lifecycle/profile/restart, and fresh output directories. Vulkan
adds `--renderer vulkan --vulkan-icd /usr/share/vulkan/icd.d/lvp_icd.json`.
For the separate caption diagnostic use check-g3-correctness.py with
`--nonclient-diagnostic` instead of --g4-correctness. All runs must use
run-isolated-graphics.py and unique per-run filesystem X sockets. No rebuilding
under live owned probes, reused shared bind sockets or other workers' displays.

All final positive and negative results report successful server waits and empty
remaining-server lists. Independent WSL/Windows process checks find no owned
test processes. All owned displays are stopped; private prefixes/logs retained.
No existing VM, login/password, SSH/NAT, shell session or agent checkout changed.
Usage checked at46% of five-hour window; reset credit remains untouched.

## Failed attempts preserved

- `r3-gpu-recovery-gl/vk` and `r3-gpu-recovery-final-gl/vk` are earlier positive
  fault controls; `r3-recovery-published-*` supersede them for the final binary.
- Initial GetWindowDC colored-marker probe failed before Present on both PEs
  (`r1-nonclient-probe-gl`); adding queued-message delivery still failed
  (`r1-nonclient-delivered-gl`). It never established visible marker setup, so
  it was not a valid preservation oracle. The replacement tests an actual
  settled plain-GDI caption baseline and retains its failure explicitly.
- A seven-line asynchronous root-frame invalidation after underlay SetProp was
  compiled into win32u/window.c (candidate ELF29bcc488...). It did not repair the
  marker experiment; `r1-frame-invalidation-29-gl/vk` still fail that added
  exploratory case while the established28 groups pass. The speculative source
  edit was removed with apply_patch and the module rebuilt with no probe running.
  It is not a shipped fix, a successful29-group run or a Wine commit.
- Caption attempts `r1-caption-before`, `r1-caption-explicit-baseline` and
  `r1-caption-independent-final` also fail the plain-GDI baseline. The final
  settled probe repeats that result in `r1-caption-settled-final`; all exit1
  with required-marker rejection/RuntimeError retained, not silently skipped.
- Initial native `caption-x64.exe`/`caption-x86.exe --nonclient-diagnostic` return1
  because the first color was sampled during DWM's initial caption transition
  (c0c4ca/787b7e ->ecf1f9) BEFORE graphics setup. The final probe requires250ms
  of exactly stable observations first; no broad color tolerance was introduced.
  `caption-settled-* --nonclient-diagnostic` both return0 afterward.
- Early `cp /var/tmp/wex-g4-tree-20261020/r3-gpu-normal-control-gl/screen-18.png ...`
  failed "cannot stat" because that owned run had not yet reached its capture.
  Reissued successfully after completion; no file was fabricated or overwritten.
- `rg -n 'DCOMP|dcomp' wine/dlls/wined3d/swapchain_gl.c` and
  `rg -n 'swap_buffers|SwapBuffers|GL_SCISSOR_TEST|clear' wine/dlls/wined3d/swapchain_gl.c`
  address a nonexistent file; these paths are in wined3d/swapchain.c.
- `rg -n '^void update_window_state|update_window_state\(' wine/dlls/win32u/window.c wine/dlls/win32u/window_private.h`
  partially matched window.c but the guessed header does not exist. Read the
  actual implementation in window.c instead.
- `rg -n 'update_window_state' wine/dlls/win32u/* -g '*.c'` fails because PowerShell
  did not expand that path glob (invalid filename syntax). Direct file reads worked.
- `command -v openbox; command -v fluxbox; command -v xfwm4; command -v xcompmgr`
  ends1 with no paths; no window manager/compositor was installed or taken over.

Next: diagnose the caption baseline and installed-shell behavior; remaining R2
fault/lifetime/multi-context proofs and R3 efficient sharing/private DXVK remain
open. One process kill/context recreation is not repeated GPU failure, driver
reset, native UINT64_MAX/fence-loss parity, WebView2/CEF recovery, physical GPU
or arbitrary application's resource recovery. Inherited DXGI67/D3D11 exit5 and
the actual Classic/Open Luna/X11 compositor/Wayland/hardware matrix stay open.
No merge, G4-complete or G5-handoff claim.
