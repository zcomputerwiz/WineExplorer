<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 current acceptance status — resumed 2026-10-20

**IN PROGRESS. NOT MERGE READY. No G5 handoff.**

Newest: `CHROMIUM_GRAPHICS_G4_GPU_RECOVERY_20261021.md`. One real Electron GPU
child kill/context recreation passes both software backends without renderer
restart or ANGLE fallback; all28 established correctness groups still pass both
PEs. Separate caption diagnostic fails before graphics in headless Wine and
passes native controls. Startup NC painting stays OPEN; no speculative Wine fix
was retained. Wine source remains `1dcd479`; see the new rebuilt ELF hash.

Newest progress: `CHROMIUM_GRAPHICS_G4_RESTART_SYNC_20261021.md` proves real
Electron restart/persistent cookies and seven lifecycle checks on GL/lavapipe,
plus deterministic Wine registration/callback-tail and API-failure rollback
controls for both PEs. These narrow R2/R3 cells pass; GPU crash/device-loss,
efficient sharing, remaining concurrency and desktop/hardware gates stay open.

Latest WSL validation: `CHROMIUM_GRAPHICS_G4_WSL_VALIDATION_20261021.md`.
All28 groups pass both PEs on GL/lavapipe; native controls prove normal shared
fence creator exit/re-export. Forced termination differs (native UINT64_MAX vs
Wine survival); real-consumer crash/restart compatibility remains OPEN. Wine
unchanged at `1dcd479`. These are software Xvfb controls, not desktop/hardware.

2026-10-21 update: see `CHROMIUM_GRAPHICS_G4_R2_LIFETIME_20261021.md` for the
latest27-group GL/lavapipe controls, event lifetime and WoW64 allocation fixes.
The older matrix below is historical where superseded. Bottom child clipping
and simultaneous populated top/bottom HWND targets are user-approved deferrals
for the selected Chromium track, not passing general-Wine compatibility cells.

**Latest repaint slice now passes scoped controls** at Wine bc7011f.
Read CHROMIUM_GRAPHICS_G4_REPAINT_VERIFIED_20261020.md first. The previous timeout
was traced to the probe's zero HWND timer identifier versus nonzero success
return, not an established renderer deadlock. With explicit ID and watchdog,
all 22 groups pass both PEs/GL/lavapipe; G0-G3, pacing, strict real WebGL1/2,
Electron lifecycle and inspected frozen front window pass scoped checks.
The earlier RESUME_NOTE and UNDERLAY.json are historical, not current acceptance.

Branch: `codex/g4-r1-completion` in parent and Wine. Last verified Wine:
`bc7011fa4785383988faced54ff7d3e496a95c49`. Parent revision is the commit
containing this report. Historical Gemini acceptance totals are not current
acceptance. Detailed chronology/failures: CHROMIUM_GRAPHICS_G4_CONTINUATION.md.

Latest continuation: CHROMIUM_GRAPHICS_G4_REPAINT_VERIFIED_20261020.md contains
current binary identities and controls. BACKGROUND_20261020 and UNDERLAY.json
record Wine 48000ca; RESUMED.json records 273cacd. Ordinary GDI underlay survives
clip/move/opacity/resize/detach/target recreation, and repeated real WM_PAINT/
observed obscure-expose without an app Commit/Present now passes. This is not
general layered/DirectX/native-child integration. Full borders/AA and remaining
R1-R4 gates below are still open.

## Actual progress

| Scope | Status | Evidence / boundary |
|---|---|---|
| ANGLE/Skia startup format failure | Repaired | Real WebView2/Electron no longer crash on the observed MSAA capability defect; unsupported sample counts remain unsupported |
| Partial Present1 coherent images | Repaired slice | Before/after window-pixel regression, scroll/dirty priority, overlaps, retained movement, invalid metadata, TEST, extreme offsets, >64 rectangles |
| Bitmap filtering | Repaired slice | Nearest/linear/inherit, pending Commit, retained property-only updates, alpha, both-axis interpolation; no soft-edge AA certification |
| Custom reference/opaque GPU route | PASS scoped | 22 groups x 2 architectures x software GL/lavapipe; narrow GL opaque route avoids producer image readback, NOT GDI underlay traffic; transformed/alpha reference path still reads back |
| Ordinary GDI underlay / dirty repaint | PASS scoped | Colored backing, opacity/clip/move/resize/detach/recreate, repeated WM_PAINT and observed cover/expose, pending content preserved, unchanged idle path counters; no full native-child/layered/DirectX proof |
| Dead observer process / idle wait | PASS scoped | All 64 slots reclaimed; measured waiting processes below 5% of one core over two seconds; not whole-system power or exhaustive race/lifetime proof |
| WebView2 WebGL1/2 | PASS diagnostic | Real default/FBO red pixels, complete framebuffer, no context loss/API errors or observed GPU/Skia fatals; no physical GPU/security certificate |
| Electron input/window lifecycle | PASS diagnostic | Injected typing/click/wheel, resize, minimize/restore/progress, reload/reset, resize back; not physical input or full Win32 route |
| Pacing | PASS scoped | Five groups both architectures, logical D3D11 backbuffer index 0, resize and waitable events; not physical 0/1/2 cycling |
| Upstream DXGI/D3D11 | FAIL inherited | DXGI 67 failures both architectures; D3D11 exit 5 at legacy sharing; no assertion weakened |
| G0–G3 baseline | PASS serial, failure retained | One i686 G1 HWND-startup failure under parallel checks; serial retry passes unchanged assertions; test socket-isolation defect independently reproduced |
| Full R1 | INCOMPLETE | Soft-border/AA, general layered/DirectX/native-child/top-bottom integration, broader fanout/publication-failure/device-loss cases |
| Full R2 | INCOMPLETE | Deterministic registration/rearm races, allocation faults, creator death, broad handle/mapping/COM leak and context-ordering proof |
| Full R3 | INCOMPLETE | Efficient cross-process GPU sharing/completion fences, private coherent DXVK interop, broader maintained consumer/profile/restart coverage and security integration |
| Full R4 | NOT RUN | Actual installed Classic/Open Luna, X11 outer compositor OFF/ON, Wayland/wexcomp, resolutions and target physical GPU matrix |

No current source claim upgrades unavailable evidence to a pass. The WSL lab has
no `/dev/dri`; its synthetic GTX 470 string is not the physical adapter. Current
driver controls are software Mesa, not old Intel/AMD/Nvidia acceptance. No
unreviewed OOBE/token branch, machine-global DLL replacement, version spoof,
single-process or sandbox-disabling flag was imported. Sandbox requested is not
proof of containment. Vendor runtime/SDK payloads and screenshots remain private,
outside Git and the ISO; no proprietary art was added.

## Correct isolation for all new runs

Do not reuse the older continuation commands' shared filesystem socket directory
for parallel runs. Separate mount/network namespaces alone were insufficient:
the second Xvfb reused X0 and replaced the filesystem socket, so a client in the
first namespace reached the second server. `test_graphics_x_socket_isolation.py
--shared` reproduces this with actual SO_PEERCRED server PIDs (expected FAIL);
the default separate-directory control PASSes. This is a proven test-isolation
defect, not proof of the precise cause of the historical G1 HWND failure.

Use the per-run launcher, which creates a fresh owned filesystem socket directory
and binds it only inside that run's private mount/network namespace:

```sh
python3 -B /var/tmp/wex-g3-repair-20261018/clean-source/scripts/run-isolated-graphics.py \
  --socket-root /var/tmp/wex-g4-tree-20261020/isolated-sockets -- \
  python3 -B /var/tmp/wex-g3-repair-20261018/clean-source/scripts/check-g3-correctness.py \
  --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 \
  --output-dir /var/tmp/wex-g4-tree-20261020/NEW-UNIQUE-OUTPUT --g4-correctness
```

For lavapipe add `--renderer vulkan --vulkan-icd
/usr/share/vulkan/icd.d/lvp_icd.json` on the same command. For G0–G3 use
`--full-suite`; for inherited upstream controls use `--upstream-tests` instead
of `--g4-correctness`. Every output directory must be new. Never rebuild while
an owned probe is using this build. Never modify global X sockets, live prefixes,
existing VMs or another worker's checkout to make tests pass.

## Next continuation

1. Treat current reports and explicit negative controls as the baseline; do not
   restart from Gemini's historical report or relabel custom tests as full G4.
2. Finish R1 soft-border/AA and general underlay/child integration with
   discriminating target-window pixels, then broad lifecycle/publication faults.
3. Complete R2 deterministic race and fault/leak coverage and R3 supported
   backend sharing/fences, avoiding a Vulkan requirement for OpenGL-only GPUs.
4. Arrange/run the actual installed desktop and available physical cells. Record
   missing hardware as NOT RUN with the specific missing adapter/driver/session.
   Include 32/64-bit apps, 800x600 and normal resolution, Classic/Open Luna,
   locked/unlocked taskbar, toolbar grippers/separators/overflow, tray chevron
   expanded/collapsed/absent, Start/dialogs and hosted Wine/native children.
5. Keep each verified step committed AND pushed. No master merge or G5/security
   completion claim until the outstanding gates are actually met.

The original pre-reset JSON remains historical at Wine f5f4312. Read the new
post-reset checkpoint for current binary hashes and evidence, not that snapshot.
