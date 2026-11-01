<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Full Edge: bounded offline headful-smoke implementation

Parent reviewed installer report b340f8a7 and independently verified all 17
archived file hashes, MSI/setup success logs, 650/435 raw-UTC budget gates,
inner/outer acceleration, consumer cleanup and graceful QMP/QEMU/controller exit0.
One ordinary Microsoft MSI attempt installed the actual x64 executable and
matching registration. This accepts the installation prerequisite ONLY, not
browser startup, rendering or sandbox enforcement. Retain all warnings.

Next implement a small original offline headful test of that actual installed
Edge executable. This refines FULL_EDGE_SMOKE_PLAN: no additional acquisition or
installation is needed, and the previously fresh installation prefix is reused
deliberately with a NEW private user-data directory. No prefix copy or reinstall.
The human's explicit Edge terms/private testing consent remains applicable.
Parent plans/reviews; GPT-6 Luna implements. No live VM/browser dispatch until
parent independently reviews implementation, tests and preparation manifest.

## Scope and deliverables

1. Add a focused runner, original page and focused executable positive/negative
   tests. Reuse owned Wine/compositor/resource/isolation/PNG helpers; no new general
   browser framework, Electron-as-Edge substitution, npm dependency or production
   Wine/compositor patch. Keep established Electron/WebView2 behavior unchanged.
2. Pin the one retained guest prefix and installed executable path/hash from
   EDGE_INSTALL_R1 report. Before any Wine launch, read-only record payload/registry
   and pending-rename evidence. Record again after ordinary bounded Wineboot and
   require installed PE/hash/registration still match before browser launch.
   Installer logs schedule the Edge directory for deletion; do NOT clear pending
   operations or hide a resulting persistence failure. Preserve a concise before/
   after inventory of relevant installed files, not proprietary file contents.
   Public Chromium's current delete_after_reboot_helper.cc distinguishes individual
   filesystem deletion from recursive scheduling; our pinned wineboot pendingRename
   attempts nonrecursive RemoveDirectory/DeleteFile. Neither establishes which
   operations this vendor installer actually queued. Observe actual registry/state.
3. Launch the actual msedge.exe headfully, original offline file URL, new private
   profile, ordinary documented/inherited app-window switches as a scoped smoke.
   First-run/default-browser suppression and diagnostic logging may be requested;
   record exact argv and actual behavior. No --no-sandbox, --disable-gpu, GPU/OS
   spoofing, security policy hacks, extensions, renderer Node, debugger attachment,
   CDP or outgoing networking. Do not change system defaults/sign-in/other profiles.
   An unsupported switch or first-run dialog is preserved failure, not permission
   for repeated launches or profile/policy manipulation.
4. Original page: static CSS background/marker and genuinely visible WebGL1/2
   canvases, no CSS-color fallback in canvas regions. Require both APIs, exact
   default/FBO RGBA readbacks, complete FBO, no GL error/loss and actual renderer
   strings; finally draw distinct solid canvas colors and freeze DOM. Shader-draw
   coverage remains a separate later item. Publish bounded nonce-bound structured
   readiness/viewport/canvas geometry via ordinary page title or console logging.
   No network service needed. Missing/duplicate/stale/failed data fail closed.
5. Reuse or add a SMALL read-only native Win32 observer: enumerate actual windows,
   retain title/PID/class/visibility/window and client-to-screen geometry plus native
   output scale, identify exactly one nonce-bearing owned browser window. Never
   select by matching screenshot colors, command-line PID alone or hard-coded
   guessed chrome offsets. Title suffix handling must not admit another nonce or
   ambiguous match. Keep enough all-window/process metadata to diagnose no window.
   Do not silently claim Windows and Linux PIDs are equivalent; record provenance
   actually observed, executable paths and owned-prefix process snapshots.
6. Capture whole actual compositor fronts at fixed bounded offsets after readiness
   (e.g. 0/1/3s), retaining initial failures/lateness. Require final two frozen
   fronts' independently expected CSS/canvas inset pixels and stable observed
   geometry, no lost identity/compositor, and finite in-viewport dimensions/scale.
   Fail on corrupt PNG, missing capture or geometry/protocol error. Do not retry
   until pixels match, force repaint, inject interaction or let pixel verdicts
   drive capture scheduling. A title/readback alone is not front presentation.
7. Preserve full browser/Wine logs, process command lines and exit/cleanup evidence.
   Separate page WebGL renderer evidence from browser GPU-compositing evidence;
   if genuine browser GPU-process/backend evidence cannot establish a broader
   claim, report that limitation rather than infer it from a window or canvas.
   Normal nonfatal unsupported APIs are not automatically rendering failures, but
   do not suppress logs, fatal crashes, GPU/renderer exits or known graphics errors.
   Startup failure is a concrete next diagnostic, not graphics compatibility proof.

Public research reference (design guidance, no code copied):
https://raw.githubusercontent.com/chromium/chromium/main/chrome/installer/util/delete_after_reboot_helper.cc
Pinned local Wine source: programs/wineboot/wineboot.c pendingRename.

## Bounds, private state and testing

Only the owned D:-backed VirGL guest/build/installed prefix. All new output/private
HOME/XDG/temp/profile paths are unique owned children. Sanitize inherited backend/
loader variables. At least 1 GiB free guest space before stage/profile creation;
do not delete evidence if insufficient. No new runtime or package downloads.
Same private mount/network namespace and unique filesystem X sockets; no external
network, existing compositor/session or other worker VM/checkout access. Explicit
inner EGL and outer GLES2 VirGL checks. No loaded-binary rebuilds.

Retain the 300s SSH bound and <=285s complete diagnostic, including preflight,
native-observer compilation, Wineboot, browser wait, fixed captures and cleanup.
Use explicit per-phase timeouts and actual remaining-budget checks. Cleanup only
owned prefix processes/compositor; preserve files and installed payload. Failed
guards before process creation cannot call cleanup into a different prefix.
Existing 650/435 raw-UTC controller guards, 900s launcher, 90s shutdown reserve,
exact-identity SIGINT/QMP/child/controller verification are mandatory.

Executable tests must exercise—not just grep source—fixture execution/protocol,
exact launch command/private environment, before/after persistence gates, stale
nonce/ambiguous titles/wrong owner, invalid/NaN/overflow/clip/scale/client geometry,
missing/invalid readbacks/FBO/API/renderer, decoded valid/wrong/blank/corrupt PNGs,
fixed capture ordering independent of verdict, browser/observer/compositor/timeout
and cleanup failures. Fake processes/vendor bytes only; no live Edge during tests.
Use original platform-independent JS tests where practical for page behavior;
do not count a hand-constructed passed JSON object as actual fixture execution.

Commit AND push the offline implementation/tests and PREPARED_NOT_RUN manifest
with complete source/dependency and installed artifact inventories. Return for
parent review before ONE later live attempt. Keep payload/screenshots/profiles
out of Git; retain failed state and all useful logs. No retry, GPU fault/recovery,
full tabbed-UI/lifecycle/media/CEF claim, master merge or token/OOBE imports.
