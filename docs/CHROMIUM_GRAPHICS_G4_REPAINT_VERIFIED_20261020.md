<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 retained repaint: verified software checkpoint

Branch codex/g4-r1-completion. Wine remains
bc7011fa4785383988faced54ff7d3e496a95c49. Parent is the commit containing this
report. The Wine commit's historical WIP label is preserved; new controls below
supersede the earlier unverified designation for this scoped repaint slice.
G4 as a whole is still IN PROGRESS / NOT MERGE READY.

## Timeout cause: test HWND timer identifier, not a proven renderer deadlock

The earlier probe called SetTimer(hwnd, 0, 80, NULL) and compared WM_TIMER.wParam
against SetTimer's return value. Wine's NtUserSetTimer returns TRUE when the server
reports timer identifier zero; the server still delivers wParam zero. Thus the
test observed a success return of 1 but never recognized its timer messages.
Relevant implementation: wine/dlls/win32u/message.c NtUserSetTimer and
wine/server/queue.c set_win_timer. The earlier potential renderer starvation/
notification-feedback hypothesis was NOT established by that timeout.

Use a nonzero explicit HWND timer identifier for creation/comparison/cancellation.
An independent test-only watchdog posts a thread wake-up after two seconds, and
elapsed-time checks also bound a busy message loop. It is joined before its
stack context/handles are released. This does not add a production timer/poller,
delete the GetMessage idle proof, issue application Commit/Present from the pixel
reader or relax any expected RGB value. Production binaries were unchanged.

## Current scoped controls

Evidence root /var/tmp/wex-g4-tree-20261020. Build/source and private runtime
locations remain those in CHROMIUM_GRAPHICS_G4_RESUME_NOTE.md. Combined Wine
configuration i386,x86_64; each display has its own filesystem X socket directory.

- repaint-timer-fixed-gl and repaint-timer-fixed-vk: focused repaint/expose
  PASS both PEs, including the actual blocking GetMessage cycle.
- repaint-verified-full-22-gl and repaint-verified-full-22-vk: all 22 required
  groups PASS both PEs. Vulkan uses /usr/share/vulkan/icd.d/lvp_icd.json.
- Repaint group: repeated WM_PAINT updates visible backing without a new app
  Commit/Present; pending visual offset and unpresented blue buffer stay pending;
  a black cover is verified via actual screen pixel AND WindowFromPoint before
  destruction/expose; retained red and new background return; path counters
  unchanged across 250 ms idle; a queued repaint cannot revive detached content
  after producer/target release. This is scoped lifetime/idle evidence, not an
  exhaustive cross-thread final-Release race or whole-system CPU/power result.
- repaint-verified-g0-g3: G0-G3 and eleven ordering groups PASS both PEs,
  including ordinary non-DComp animation/resize. Reported FPS is not acceptance.
- repaint-verified-pacing: all five pacing groups PASS both PEs.
- repaint-verified-webgl: real private WebView2 strict WebGL1/2 default/FBO red
  pixels PASS; 21 samples / 391 frames; no observed GPU/Skia fatal/crash signatures.
- repaint-verified-electron: original private Electron lifecycle seven checks
  PASS; 34 samples / 485 post-reload frames; no observed GPU/Skia fatals/crashes.
- repaint-verified-frozen: 24 samples / 1034 frames; actual screen-18 inspected
  privately: one blue square at x=160, correct frozen text, uniform cream content
  background, controls and scrollbar intact. Internal capturePage/animation was
  not substituted for inspecting the front-window image.

All consumers are software-driver diagnostic controls, not physical GPU or
sandbox containment acceptance. Vendor runtime/SDK/images remain private,
outside Git/ISO. No OOBE/token source or VM configuration was changed.

## Tested binary identities (SHA-256)

| Artifact | SHA-256 |
| --- | --- |
| x86_64 dcomp.dll | c1f4fea4c3518202498264ad337b35a068503c4915de3a2fd9946db4e0e86d12 |
| i386 dcomp.dll | af4e1b820f4e8f170934fdfa427a4139ded59da171ca172c2ab0918316cf39c7 |
| x86_64 dxgi.dll | e103ba8ec2eadcddfb8679d007aa0cc01c0c13bcec5e3fd3ceb9f9c8062701cb |
| i386 dxgi.dll | 3c48afc817fc694e8a12787ee123fc1296d2023e5b9922e0974de97101ca4f93 |
| x86_64 wined3d.dll | 6f938cb57c30d20a9393959b4e4b60533c2f5af55345ed9aba75fca1f879c9bb |
| i386 wined3d.dll | 34f4397da7af41a4bd8ed01808d52668be8666e4a75a3d15d38547f3b9a43de1 |
| native win32u.so | 535fac5d3162002a484d5a3eb9a71a4304b538c0c1b4c8d0106453ba05c5ee0a |
| native winex11.so | b688da4f7631fa7e1728b090e2a1dd3442d9c614dfff6c18468baf19964db3df |

## Remaining work / next continuation

1. R1 border/AA: dcomp_visual_SetBorderMode currently returns S_OK without
   retaining/validating the value. Reference composition takes one center sample;
   dcomp_inside_clip and dcomp_render_pixel are the current hard-coverage path.
   Research the public HARD/SOFT/INHERIT contract; write discriminating fractional
   transform/clip/rounded-boundary and transactional/inheritance tests before
   implementation. Do not infer exact native AA pixels or copy proprietary art/
   implementation. Preserve the narrow opaque GL path for genuinely eligible
   scenes. Do not weaken unsupported-property honesty or existing RGB assertions.
2. Finish general top/bottom/native child/DirectX/layered and foreign-thread
   target-startup integration; current ordinary GDI coverage is not all of R1.
3. R2 deterministic registration/rearm, faults, creator death, leak/context-order
   and concurrent final-Release proof; R3 efficient backend GPU sharing/completion
   and private DXVK interop. Keep OpenGL-only older hardware supported.
4. Actual installed Classic/Open Luna, X11 outer compositor OFF/ON, Wayland/
   wexcomp, 800x600/normal resolution, locked/unlocked taskbar, toolbars and
   expanded/collapsed/absent tray chevron remain NOT RUN. Physical older Intel/
   AMD/Nvidia cells are unavailable in this WSL lab (/dev/dri absent); synthetic
   GTX470 branding is not a physical adapter. Full maintained consumer/profile/
   restart/sandbox enforcement evidence and inherited upstream DXGI 67 failures/
   D3D11 legacy-sharing exit 5 remain OPEN, not waived by these controls.

Use run-isolated-graphics.py and unique output directories. Do not rebuild while
owned tests run, reuse shared X socket binds or touch another worker's checkout.
Commit AND push verified steps; no master merge or G5-complete claim.

Failure/workaround notes this continuation: the initial compact consumer-result
read raced Electron completion and results.json was not yet present; no null
fields were counted as a pass. Waiting for the owned runner completed with exit
0 and the report's actual counters. No new graphics implementation failure was
observed in the reruns. Previous timeout/occluder/batching failures remain in the
historical resume note rather than being erased. Test runners clean only their
own prefixes/servers/Xvfb. One-time usage reset credit remains untouched.

The verified parent code/test checkpoint is 8537caca. At 83% of the five-hour
usage window, a one-shot same-chat continuation was scheduled for October 20,
2026 at 23:25 America/Chicago, after the reported 23:23:27 reset. Existing
automation resume-chromium-g4-after-usage-reset was updated, not duplicated.
Use branch HEAD for later documentation commits. One-time reset credit unused.
The first patch to add this schedule note failed exact-context verification
because a wrapped line was matched without its preceding word; the append at
this verified paragraph succeeded. No source or test was affected.
