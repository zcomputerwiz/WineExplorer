<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 desktop validation continuation checkpoint — 2026-10-21

NOT MERGE READY. Wine remains `54bc44a`; no production edits this slice.
User accepts accelerated virtual GPU drivers, not a physical GPU matrix.
Any new VM/disks/snapshots must be on host D:. Preserve Qwen/OOBE/token work.
Stop new work below5% remaining usage; checkpoint/commit/push and resume after
reset. At preparation of this note primary usage was94%, reset1792607343
(2026-10-21 18:29:03 UTC). The one-time reset credit remains untouched.

## New isolated desktop harness

`check-g3-correctness.py` and `check-g4-webview2.py` accept
`--desktop openbox` (outer compositor off) or `--desktop composited`
(Openbox + xcompmgr -a). Each uses ONLY its owned Xvfb/private filesystem
socket. The helper checks live process handles and X11 WM_S0/_NET_WM_CM_S0
selection ownership at startup and, in final harness runs, at completion.
Exact owned processes are terminated with bounded waits. No global cleanup.
Desktop controls are excluded from consumer restart reuse and full-suite child
runs that do not propagate them. Eight theme/driver/desktop helper unit tests
and nine consumer-harness tests pass. No assertions were weakened.

Installed Debian WSL testing dependencies via
`apt-get install --no-install-recommends -y openbox xcompmgr`:
openbox, xcompmgr, libid3tag0, libimlib2t64, libobrender32v5, libobt2v5,
libspectre1, libstartup-notification0;776kB download/2.8MB installed.
Openbox registered x-window-manager/x-session-manager alternatives; no live
desktop/service/login/VM changes were made. It is launched as openbox
--sm-disable, not openbox-session/autostart.

Same owned build/runtime/driver identity and private paths as
CHROMIUM_GRAPHICS_G4_ACCELERATED_WSL_20261021.md. Evidence root:
`/var/tmp/wex-g4-tree-20261020`. Always use run-isolated-graphics.py and a unique
output directory. `--gl-driver d3d12` explicitly verifies accelerated Mesa GL.

## Results and unresolved findings

- `r4-openbox-on-electron`: accelerated Electron seven lifecycle checks PASS,
  host exit0. This initial run predates end-of-test desktop verification.
  Inspected screen-8.png: frame/title/menu/input/click/text/square visible.
- `r4-openbox-off-electron-final`: seven lifecycle checks PASS, exit0;
  desktop selections verified at start/end. **Visual acceptance FAIL/OPEN**:
  inspected screen-8.png has content/input/click/square but missing title/menu
  area. Do not label this cell visually passed because animation/API tests pass.
- `r4-openbox-on-webview2-final`: strict real WebGL1/2 default/FBO pixel checks
  PASS, exit0, desktop selection ownership verified at start/end.
- `r4-openbox-off-28`:27/28 groups PASS both PEs, but
  **multi_target_transactions FAIL both PEs**. It prints
  `PIXEL_FAIL x=12 y=8 expected=0000ff actual=ffffff`, backing/surface black,
  underlay registered1. The first run predates final end-of-test check.
- `r4-openbox-off-multitarget-software`: exact same single-case pixel failure
  both PEs on llvmpipe, desktop ownership verified at end. Not D3D12-specific.
  Root cause not established. Inspect the8300px target-two resize/failing
  Commit rollback path in multi_target_transactions() and asynchronous host
  WM geometry/expose/occlusion; do not change rendering or expectations blindly.
- `r4-openbox-classic-caption`: explicit Classic active0 verified, baseline
  screen deb391 but backing CLR_INVALID/client-only clip4,30,252,252; both PEs
  FAIL before D3D. Unlike unmanaged Classic, the caption belongs to the host WM.
  The probe also assumes WindowFromPoint(sample)'s Win32 root is the HWND;
  investigate that assumption separately from actual external-frame painting.
  This is neither a Classic pass nor an established Classic renderer bug.
- Final controls completed: `r4-openbox-on-electron-final` (seven lifecycle,
  compositor on) and `r4-openbox-off-webview2-final` (strict WebGL1/2 pixels,
  compositor off), both exit0, start/end ownership verified and cleanup empty.
  This completes the narrow API/pixel OFF/ON pairs, not visual acceptance.

Failure history: `rg docs/CHROMIUM_GRAPHICS_G4* -g '*.md'` produced Windows
invalid-filename error123 (PowerShell does not expand that path glob); use
directory docs with -g. `rg ... /dev/null` failed because this was Windows,
not Linux; use actual Windows source paths. One unescalated WSL read-only sed
command failed Wsl/E_ACCESSDENIED; scoped escalated repeat succeeded.
Openbox logs missing /var/lib/openbox/debian-menu.xml warning (no Debian menu
package installed); WM ownership and rendering controls establish that the WM
ran. Source scripts were staged while early Python runs were already loaded;
their changed traceback line numbers are not proof those runs used the final
helper. Final-named controls explicitly rerun the end-ownership version.
One report-update apply_patch failed on an incorrect context line; no edits
were applied. The smaller exact-context patch succeeded.
Final Debian inventory confirms no Wine/wineserver/Xvfb/Openbox/xcompmgr or
test runners remain. Existing VirtualBox VM and other agents are untouched.

## Next steps after reset

1. Read this note and final control JSONs; preserve all negative evidence.
2. Reproduce missing frame with real Openbox/compositor OFF. Compare initial
   actual X11 bounding shape and configure/expose events to compositor ON,
   no-WM control and Wine's client surface shape handling. Private read-only
   Xlib helpers exist under Windows build/graphics-validation-20261021/.
3. Diagnose multi_target_transactions WM-specific failure independently;
   distinguish host geometry/occlusion/probe invalidity from publication bugs.
   This is not automatically covered by earlier user-approved top/bottom HWND
   deferrals. Keep it OPEN until relevance and cause are established.
4. Complete actual Classic/open Luna installed-shell/taskbar/toolbars/tray,
   real Wine Wayland/wexcomp, remaining R2/R3. winewayland.so exists in owned
   build; no Wayland test has run. No master merge yet. Existing VM is full;
   clone/create on D: if needed rather than filling/changing that VM.
