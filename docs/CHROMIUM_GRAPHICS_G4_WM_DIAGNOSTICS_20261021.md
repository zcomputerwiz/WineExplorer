<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 WM diagnostics and acceptance scope correction — 2026-10-21

Openbox was introduced only in parent `4814dc4c` as an owned Xvfb compatibility
fixture, not the product's compositor or a new shipping dependency. `xcompmgr`
is a separate optional outer X11 compositor. The user clarified that the
project's custom compositor is the intended target. Primary acceptance must
exercise pinned **wexcomp** (our labwc/wlroots fork), its actual session/private
shell endpoint and Wine route. Do not spend the next stage repairing a foreign
WM-specific behavior unless it is relevant to supported deployments or also
reproduces on the intended stack. Openbox diagnostics remain evidence, not a
substitute for product acceptance or an automatic merge blocker by themselves.

Verified wiring: installer/package-wexcomp.sh builds the pinned wexcomp source
with Xwayland enabled and installs the labwc binary as /usr/local/bin/wexcomp.
scripts/wine-explorer-wayland-session.sh starts that binary using project config,
selects the private wex-shell-0 endpoint, unsets DISPLAY, and starts the Wine
shell. No Openbox import or product session replacement occurred in this work.
The fork's README still describes upstream Labwc's Openbox-inspired design;
that does not mean it runs Openbox. No guarantee that wexcomp automatically
avoids every Wine presentation bug is inferred from the architecture alone.

## Multi-window failure: test occlusion, not proven transaction failure

`r4-multitarget-inspect-live` under /var/tmp/wex-g4-tree-20261020 captured the
failed assertion while the PE was held for inspection. Win32 requested target
two at350,10 with size8300x300 and still reported that geometry; actual X11
Openbox frame geometry was **0,10,8300,300**. The WM constrained its X origin
to zero, occluding target one's screen pixel at26,48 with white. Read-only Xlib
tree/map-state and actual root capture confirm the overlap. The probe's
WindowFromPoint check could not independently rule out this asynchronous host
WM geometry change. Existing width-based failed results remain retained.

The test now overflows height (256x8300) instead. It exercises the same >8192
dimension prepare rejection while target two stays in its separate horizontal
column. Required E_NOTIMPL, old-state preservation, successful retry/new pixels,
ambiguous binding rejection and target detachment assertions are unchanged.
Production Wine remains54bc44a, unchanged. Results:

- r4-multitarget-height-control: both PEs PASS, software GL + Openbox.
- r4-openbox-off-28-height: all28 groups both PEs PASS, accelerated D3D12 GL.
- r4-openbox-on-28-height: all28 groups both PEs PASS, accelerated D3D12 GL.

The diagnostic --inspect-pixel-failure option holds a failed pixel assertion
for ten seconds and still returns false. It does not turn a failure into a pass.
Printed first/second geometry helps identify such occlusion. No Wine fix was
needed for this particular failing oracle.

## Consumer stage capture: API success is insufficient

New --lifecycle-visuals (requires Electron lifecycle, >=45 seconds and import)
holds eight stages until an actual owned root-window capture is acknowledged:
ready, input, scroll, resize, minimized, restored, reload, resize_back. The
fixture waits at most ten seconds; the runner records actual captures and
requires complete ordered request/ack pairs. Bad/missing/duplicate proof fails
closed. Captures are explicitly MANUAL_REVIEW_REQUIRED, never pixel verdicts.
The runner additionally records read-only X11 geometry/map-state snapshots at
each stage via snapshot-graphics-x11.py. It never operates a user's display.

- r4-resume-off-noaction: actual initial window has frame/menu and full X11
  shape0,0,740,520. No-action startup alone does not reproduce the old screenshot.
- r4-resume-off-lifecycle: lifecycle assertions PASS; shape remains full after
  resize. Therefore another speculative shape-offset fix is not justified.
- r4-off-phase-capture: all eight stages captured and lifecycle API checks pass,
  but manual inspection finds blank white restored client and black client
  after reload in that run. Minimized stage is fully hidden, and resize_back
  displays content again. Title/icon painting also differs between stages.
  These are real negative front-window observations despite API success.
- r4-on-phase-capture: eight captured stages, API control exits0; captures still
  need full manual inspection before any visual claim.
- r4-off-phase-x11: eight captures plus X11 snapshots complete, API exit0;
  restored snapshot provides exact child/parent map state for further diagnosis.

Do not claim all visual desktop failures resolved, and do not assume Openbox is
their root cause. Reproduce on wexcomp before changing production Wine for them.
All runtime payloads and captures stay private/local, outside Git and the ISO.
Ten consumer parser tests and eight theme/driver/desktop tests PASS.

## Continuation and diagnostic failures

Next: build/use pinned wexcomp in an owned session and exercise the real project
route (including private endpoint), then Classic/open Luna shell/taskbar/
toolbars/tray and maintained consumer lifecycle. A fresh VM is authorized if
needed, with all storage on D:. Existing full VirtualBox VM and Qwen's work
must remain untouched. No master merge yet. Primary five-hour usage reset was
confirmed0%; last checked10%, weekly61%; reset credit untouched.

Failures/workarounds: the first held-pixel Xlib inspector matched a stale x64
failure log during i686 initialization, capturing a Wine startup window rather
than the failing HWND. Require a recent (<8 second) hold marker; a new uniquely
named run then captured live occlusion. `rg ... -ErrorAction SilentlyContinue`
incorrectly passed a PowerShell option to native rg and failed parsing -E;
remove it. Optional pkg-config wlroots queries returned1 (no installed matching
development pkgconfig modules), not proof the project compositor cannot run.
No test build or production DLL was rebuilt during live probes. Final process
inventory contains no owned Wine/wineserver/Xvfb/Openbox/xcompmgr/test runners.
