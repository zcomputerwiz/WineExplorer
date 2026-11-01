<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Edge on the unified development build — run 1 and blockers (2026-11-01)

First Edge run since the AppContainer, token, first-thread and font fixes. The October Edge arms
(`CHROMIUM_GRAPHICS_G4_EDGE_*_20261023.md`) stopped with no renderer and no `scene.js` request. Those
blockers are gone. New blockers below.

## Setup

Same harness as `DEVELOPMENT_VALIDATION_20261101.md` (guest copy of `validate.py`, 8 GiB R4 VM, owned labwc
compositor, VirGL, fresh prefix, private loopback, nonce HTTP fixture, bridge runtime
`feature-runtime/build/wow64` = Wine b5e05301). Only change: `image=` points at the retained Edge 156.0.4314.42
install (`/home/debian/wex-g4/edge-install-r1-v2-20261022/prefix/.../msedge.exe`) instead of `chrome.exe`. The
browser arguments are unchanged: no sandbox or feature switches. Run `edge-bridge-1`; evidence copied to
`D:/DeepThought/ChromiumLab/edge-dev-r1-20261101/`. The VM auto-stops 900 s after boot (`launch.py`).

## Result: not functional (`chromium_functional=false`)

Shell, Start menu, Notepad and Explorer pass before and after. For Edge:

| Observation | Value |
|---|---|
| `GET /index.html`, `GET /scene.js` | 200, 200 |
| Window title | `WineExplorer Edge loading` until the window disappears; the `WEXEDGE {json}` ready title never arrives |
| Renderer processes | launched (26 `--type=renderer` command lines); GPU, storage, entity-extraction utilities start |
| Network service | one launch with `--service-sandbox-type=none` (Chromium's run used `network`); not yet explained |
| Browser exit | ~32 s after launch, Crashpad dumps, window gone |

## Blocker 1 (fatal): browser stack-cookie failure in a Security-Mitigations event-log query

Crashpad report `338ab80e-...dmp`: thread 0x1c8, `EXCEPTION_BREAKPOINT` (0x80000003) at `msedge.dll` RVA
0x462ea9f. That address is the int3 after `call __security_check_cookie` in the shared failure stub reached by
`jne` from the epilogue of the function at RVA 0x243616e (frame 0x290, returns a string by hidden pointer). The
function builds the EvtQuery XPath `Event/System/Provider[@Name = 'Microsoft-Windows-Security-Mitigations'] ...`.
The cookie compared at the epilogue differs from the prologue copy, so the frame was overwritten by a callee.
The last calls on that thread (browser.stderr.log:2590-2592) were `NtQueryInformationProcess` classes 0x2b
(len 6, then len 0x106) and 7. The 30 s timing matches a delayed maintenance task; `UtilWin` is launched at the
same moment (`CreateProcessInternalW` fixme, unsupported attribute 0x20007).

Status: cause inside Wine not yet proven; candidates are `NtQueryInformationProcess` 0x2b/7 return sizes and the
wevtapi stubs. Binary analysis is static (no Edge symbols).

## Blocker 2: the page tab is discarded and killed before WebGL completes

`chromium.log:573`: `page_discarding_helper.cc:173 Discarding multiple pages with target (kb): 0`, then
`sad_tab.cc:391 Tab Killed: http://127.0.0.1:8765/` ten seconds later. The GPU `viz-compositor` starts only
after `GetGpuDriverOverlayInfo: Failed to retrieve video device`. Edge's PerformanceManager appears to see
critical memory pressure. The guest has 7.9 GiB free, so the Wine memory metrics (`GlobalMemoryStatusEx`,
`K32GetProcessMemoryInfo`, memory resource notifications) are suspect.

## Expected noise

`connect failed: 10051`, `WSALookupServiceBegin failed: 8`, `GetAdaptersAddresses failed: 2` (private
loopback), missing `xdg_toplevel_icon_manager_v1`, and unregistered shell and EdgeUpdate COM classes and WinRT
activation factories.

## Next

1. Identify which callee overran the frame (wine-side source review, then a gdb watchpoint or `+seh`
   run).
2. Find what feeds the PerformanceManager discard decision.
3. Only then repeat the run. A pass needs the nonce-bound ready title, the actual front pixels and a clean
   browser lifetime, as for Chromium.
