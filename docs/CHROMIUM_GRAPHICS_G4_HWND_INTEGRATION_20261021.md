<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 R1 HWND integration: open gates and reproduction

## Scope decision — 2026-10-21

The user approved deferring the two demonstrated general-Wine compatibility
gaps when they do not affect Chromium rendering. Bottom-target child clipping
and simultaneous populated top/bottom targets are therefore DEFERRED, not fixed
or passing, and are no longer prerequisites for the selected Chromium graphics
track. Keep the separate failing probe for eventual compatibility work.

Chromium's current `ui/gl/dc_layer_tree.cc`, DCLayerTree::Initialize, creates one
target with `CreateTargetForHwnd(window_, TRUE, &dcomp_target_)`:
[upstream source](https://raw.githubusercontent.com/chromium/chromium/main/ui/gl/dc_layer_tree.cc).
This supports deferral for that normal compositor path, alongside our actual
consumer controls. It is not proof about every Electron/CEF embedding host or
future browser release. Reopen a deferred gap if a selected consumer trace
exercises it or a real rendering failure depends on it. Other child/layered
integration requirements are not automatically waived.

Next work is R2 deterministic observer registration/rearm and lifetime proof,
then R3 efficient backend image sharing/completion and real-consumer profiling.
Physical-GPU and actual Classic/Open Luna desktop checks remain required for
their acceptance claims. This decision does not authorize a master merge or
declare all G4 gates passed.

This follows published main `1dfc5128` / Wine `61929ac`. Border/repaint controls
are verified in `CHROMIUM_GRAPHICS_G4_BORDER_VERIFIED_20261021.md`; G4 is NOT
complete. No production change after that checkpoint is part of this diagnostic.
No master merge, OOBE/token import, VM session takeover or proprietary payload
publication occurred.

## Contract and discriminating controls

[CreateTargetForHwnd](https://learn.microsoft.com/en-us/windows/win32/api/dcomp/nf-dcomp-idcompositiondevice-createtargetforhwnd)
defines the ordering: application HDC/DirectX base, bottom composition target,
ordinary HWND children, top composition target. Both targets are allowed on the
same window. `topmost` refers to the children, not global top-level Z order.

New `scripts/test_g4_hwnd_integration_probe.cpp` uses public APIs and actual
front-window pixels. Its owned child HWND paints actual black with WM_PAINT;
the base is green, the composition bitmap red/blue. Before presenting, it checks
that WindowFromPoint hits that child and that both the child black and base
green are visible. This preflight prevents a nonexistent/unpainted/occluded child
from being mistaken for a successful clipping control. It then checks child
pixels after Present and after another real child WM_PAINT without a new Commit
or Present. Top and bottom modes use separate windows/targets.

The two-target test attaches distinct swapchains to both allowed targets on one
HWND and records Commit's HRESULT. If publication succeeds, it checks distinct
bottom-only, overlap/top, top-only and base pixels in each producer-present order.
It does not treat an HRESULT failure as pixel success, or infer which producer
would erase another when Commit has already rejected the transaction.

The runner's separate `--hwnd-integration` mode requires all3 groups and a zero
process exit; it is mutually exclusive with other suites. The established24
groups remain separately named/scoped, not advertised as complete R1 acceptance.
The shared C++ include renames its entry point for the integration probe; default
entry-point behavior and assertions remain unchanged. Helper code is reused,
not copied into a drifting second implementation.

## Current evidence

Owned source/build/socket rules and private runtime identities are unchanged
from the border report. Under `/var/tmp/wex-g4-tree-20261020/`:

- `hwnd-layer-owned-child-gl`: both PEs compile with -Wall/-Wextra/-Werror;
  both child preflights PASS. Top child ordering and subsequent child repaint
  PASS in this narrow control. Bottom FAILS: expected black at (12,12), screen
  is composition red, while GetDC's underlying application bitmap is black.
  Both nonempty targets on one HWND fail Commit with `80004001` (E_NOTIMPL).
- `hwnd-layer-owned-child-vk`: same isolated integration control, explicitly
  selected `/usr/share/vulkan/icd.d/lvp_icd.json`. Both PEs give the same
  preflight PASS, top PASS, bottom pixel FAIL and two-target E_NOTIMPL result.
- `hwnd-harness-control-24-gl`: rerun of ordinary24-group suite after adding
  the separate runner mode/include guard: all24 PASS both PEs, exit0.

Both final integration runners exit1, as required for these open gates. Their
result cleanup records and the passing control record have no remaining server
processes or cleanup errors. Production binaries remain the border report's
identities; there was no intervening rebuild or production source edit.

Reproduce using the unique-socket wrapper, fresh output dir and owned build:

```sh
python3 -B /var/tmp/wex-g3-repair-20261018/clean-source/scripts/run-isolated-graphics.py \
  --socket-root /var/tmp/wex-g4-tree-20261020/isolated-sockets -- \
  python3 -B /var/tmp/wex-g3-repair-20261018/clean-source/scripts/check-g3-correctness.py \
  --wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64 \
  --output-dir /var/tmp/wex-g4-tree-20261020/NEW_UNIQUE_DIRECTORY \
  --hwnd-integration
```

Vulkan adds `--renderer vulkan --vulkan-icd
/usr/share/vulkan/icd.d/lvp_icd.json`. The runner exits1 on the current open
gates, retains both architecture logs/results and performs its own scoped
server/Xvfb cleanup. A deliberate failing diagnostic is not a passing suite.
Wine-free `test_graphics_harness.py`: all4 unit tests PASS.

## Source localization and next implementation

1. `dlls/dcomp/device.c`: target topmost is stored and drives duplicate-slot
   ownership, but it does not reach scene descriptors. During commit the loop
   explicitly rejects two nonempty contexts having the same target HWND, before
   preparation/publication. Removing that check alone is NOT a fix.
2. `include/wine/dcomp_layer.h`, `include/wine/wined3d.h`,
   `dlls/dxgi/swapchain.c`, `dlls/wined3d/swapchain.c`: current backend retains
   one tree per producer-bound scene, with one HWND repaint sink. Real two-layer
   support needs coordinated per-HWND ownership/publication, not two unrelated
   fullscreen draws or last-writer-wins notification ownership.
3. Bottom child clipping must be a real visible-region/presentation policy,
   refreshed for child create/show/hide/move/resize/region/Z-order/teardown, on
   GL and Vulkan and eventually X11/Wayland. Do not paint a copied screenshot of
   a child over the output and call that native child integration: DirectX
   child content and later independent updates must remain live and correctly
   ordered. Do not fix this by permanently changing the application's styles.
4. Preserve the GDI base separation, prepared vs active rollback, unpublished
   producer buffer isolation, no timer/poll renderer, and live COM-reference
   protection of queued repaint notification. A shared HWND coordinator must
   not create device/target/producer reference cycles or consume another
   transaction's staged state on detach. Final release of either target must
   leave the other active; thread/device/process boundaries require explicit
   validation and deterministic tests.
5. Extend this probe to child move/hide/show, both targets simultaneously,
   release of either target, ordinary DirectX base/children, actual child HWND
   targets, layered windows and foreign-thread startup. A plain GDI child under
   a single top target passing is not those other gates.

R2 races/faults/lifetimes and R3 backend sharing/performance remain OPEN. Actual
Classic/Open Luna, X11 compositor OFF/ON, Wayland, resolutions/taskbar/toolbars/
tray, physical older GPUs and native Windows pixel-reference acceptance remain
NOT RUN. WSL lacks /dev/dri; synthetic GTX470 identity is not physical evidence.
Inherited upstream DXGI67 failures and D3D11 legacy-sharing exit5 remain OPEN.

## Failed diagnostic attempts (not acceptance)

- `hwnd-layer-before-gl`: C++ macro main redefinition/conflicting entry point.
  Nested helper inclusion already renamed/undefined main. Replaced with explicit
  embedded-probe entry-point guard.
- `hwnd-layer-before-gl-retry`: simply omitting the shared main left static
  controls unused under -Werror; rename rather than omit its entry point.
- `hwnd-layer-before-gl-compiled`: compiled, but child preflight was missing;
  superseded, not accepted for clipping conclusions.
- `hwnd-layer-preflight-gl`: STATIC hit testing is transparent without NOTIFY;
  preflight correctly failed, so neither child result was accepted.
- `hwnd-layer-notify-gl`: child hit test passed, but SS_BLACKRECT uses the system
  window-frame brush (observed RGB106), not guaranteed literal black. Replaced
  STATIC with owned WM_PAINT BLACK_BRUSH, retaining exact black assertions.

These fixture errors were corrected before accepting the owned-child evidence;
no expected production pixel was weakened to make a test pass.
