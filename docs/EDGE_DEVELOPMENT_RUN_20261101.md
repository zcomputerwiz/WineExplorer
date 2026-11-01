<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Edge on the unified development build — runs and blockers (2026-11-01)

First Edge runs since the AppContainer, token, first-thread and font fixes. The October Edge arms
(`CHROMIUM_GRAPHICS_G4_EDGE_*_20261023.md`) stopped with no renderer and no `scene.js` request. Those
blockers are gone: **Edge now renders the WebGL fixture on this build**, but not reliably.

## Setup

Same harness as `DEVELOPMENT_VALIDATION_20261101.md` (guest copy of `validate.py`, 8 GiB R4 VM, owned labwc
compositor, VirGL, fresh prefix, private loopback, nonce HTTP fixture, bridge runtime
`feature-runtime/build/wow64` = Wine b5e05301). Only change: `image=` points at the retained Edge 156.0.4314.42
install (`/home/debian/wex-g4/edge-install-r1-v2-20261022/prefix/.../msedge.exe`) instead of `chrome.exe`. The
browser arguments are unchanged: no sandbox or feature switches. The VM auto-stops 900 s after boot
(`launch.py`). Evidence: `D:/DeepThought/ChromiumLab/edge-dev-r1-20261101/` and `edge-dev-r2-20261101/`.

## Results

| Run | Outcome |
|---|---|
| 1 | Not functional. `index.html` and `scene.js` served, title stuck at `WineExplorer Edge loading`, browser dies ~32 s after launch (Blocker 1) |
| 2 | **Functional**: nonce-bound `WEXEDGE` ready title, `passed=true`, WebGL1/2 readbacks, actual front pixels correct (green background, blue marker, magenta and cyan canvases), browser alive past the 30 s mark |
| 7 | **Functional** (same verdict as run 2) |
| 3-6 | No verdict: the harness's `scan` probe (`wine desktop-validation-probe64.exe scan`) hit its timeout (8 s, then 40 s in run 6). Harness limit under Edge's load, not an Edge verdict |

Two functional runs, one crash, four harness timeouts. A pass needs repeats; one crash is not yet explained.

## Blocker 1: intermittent browser stack-cookie failure in a Security-Mitigations event-log query

Crashpad report `338ab80e-...dmp` (run 1): thread 0x1c8, `EXCEPTION_BREAKPOINT` (0x80000003) at `msedge.dll` RVA
0x462ea9f. That is the int3 after `call __security_check_cookie` in the shared failure stub reached by `jne`
from the epilogue of the function at RVA 0x243616e (frame 0x290, returns a string by hidden pointer). The
function builds the EvtQuery XPath `Event/System/Provider[@Name = 'Microsoft-Windows-Security-Mitigations'] ...`,
so a callee overwrote its frame (or the cookie changed between prologue and epilogue). The last calls on that
thread were `NtQueryInformationProcess` classes 0x2b (len 6, then 0x106) and 7. The function runs from a ~30 s
maintenance task, which also launches `UtilWin` (`CreateProcessInternalW` fixme, unsupported attribute 0x20007).
Runs 2 and 7 launched `UtilWin` at the same point and survived, so the bug is timing- or state-dependent.

Reviewed in Wine (stock upstream code, `dlls/ntdll/unix/process.c`): `ProcessImageFileName(Win32)` and
`ProcessDebugPort` bound their writes correctly for the logged sizes. `wevtapi` is stubs (`EvtQuery` returns NULL).
No overrun found statically. A gdb watchpoint run (`edge-gdb.py`: hardware watch on the frame's cookie slot) attached
to the browser but never reached the function in the one run it covered, so the cause is open.

## Normal browser window (tabs, toolbar, menus) and guest memory

The runs above use `--app=<url>`, Edge's tabless app window. With the URL as a plain argument (normal window), in the
original 8 GiB guest Edge produced a blank white window and the harness's `wine ...probe scan` calls timed out. Cause: the
guest was thrashing. During the stall `free` showed 7.35 of 7.95 GB used, 114 MB page cache, `kswapd` busy, the probe in
`folio_wait_bit_common` and the wineserver in `jbd2_log_wait_commit`. The main thread was idle in `MsgWaitForMultipleObjectsEx`,
so the UI thread was not hung.

With the same VM at 16 GiB (`ChromiumLab/launch-16g.py`, a copy of the validated launcher with `-m 16384`) the normal Edge
window renders fully: tab strip with new-tab button, back/forward/reload, address bar, favorites, profile, the "..." menu and
the Copilot Chat button, plus the WebGL fixture with correct pixels (`chromium_functional=true`). Portable Chromium in normal
mode also renders its tabs and omnibox. Used memory climbs from 1 GB to about 9 GB within 40 s.

Memory by role, PSS at t=8-10 s, Edge (`ChromiumLab/edge-dev-r3-20261101/who.txt`): 8 renderers 5.9 GB (about 740 MB
each, anonymous), browser 0.48 GB, GPU 0.48 GB, utilities 0.4 GB each (Extractor, NetworkService, StorageService), explorer 66
MB, labwc 61 MB, wineserver 17 MB. Plain Chromium in normal mode has 7 renderers at 740-1080 MB each, so this is not Edge-specific.

What a renderer holds (Chromium, `smaps.txt`): the browser DLL image about 330 MB as a **private anonymous copy**
(`r-xp` 278 MB fully resident with no pathname, plus 37 MB `r--p`; it is the PE image at 0x6fffe69f1000, not its preferred
base), a 350 MB resident read-write region, and smaller heaps. Every Wine process that loads the DLL therefore holds its own
copy, where native Windows and Linux would share file-backed pages. Open questions: why Wine copies the image instead of
mapping the file (relocation at a non-preferred base, or an unaligned or non-mmappable section), and what the 350 MB region
is (a pre-committed and touched reservation).

## Window styles seen by the probe

`DwmGetWindowAttribute` returns `E_NOTIMPL` (0x80004001) for attributes 1-4, 14, 17, 20, 23, 33-38 (Wine's `dwmapi` stub);
Chromium does not need them. Chromium's top window: style 0x16cf0000, ex 0x100, children `Chrome_RenderWidgetHostHWND` and one
`Intermediate D3D Window` (style 0x58000000, ex 0x280024, `WS_EX_LAYERED` set but `GetLayeredWindowAttributes` fails). Full DWM
work (the project goal) will need real attribute storage and composition state; see `WINLOGON_AND_DWM.md` (D4).

## Notes

- `Discarding multiple pages with target (kb): 0` appears in passing runs too: normal Edge startup, not a cause.
  An earlier hypothesis that bogus memory pressure kills the tab is withdrawn.
- Network service starts once with `--service-sandbox-type=none` (Chromium's validation showed `network`); it did
  not prevent a pass. Not yet explained.
- Expected noise: `connect failed: 10051`, `WSALookupServiceBegin failed: 8`, `GetAdaptersAddresses failed: 2`
  (private loopback), no `xdg_toplevel_icon_manager_v1`, unregistered shell/EdgeUpdate COM classes, missing WinRT
  activation factories.

## Next

1. Repeat the run enough to estimate the crash rate; give the harness probe a generous timeout and keep gdb attached
   from the start of the 30 s window.
2. Find the callee that overwrites the frame of the function at RVA 0x243616e.
3. Treat acceptance as for Chromium: nonce-bound ready title, front pixels, clean browser lifetime, repeated.

## Cleanup (guest disk)

The guest root was 97 % full (70 of 79 GB). Removed disposable, stopped prefix copies only: eight
`prefix.*replay*-20261101` backups of the Edge prefix, `repro-pfx`, `parent-maintenance-*/startup4/prefix`, and the
validation run's `baseline/prefix` and `bridge/prefix`, after checking that no process or mount used them. 28 GB freed
(42 GB used). Kept: the canonical Edge `prefix`, `~/.wine`, runtimes, builds, logs, evidence.
