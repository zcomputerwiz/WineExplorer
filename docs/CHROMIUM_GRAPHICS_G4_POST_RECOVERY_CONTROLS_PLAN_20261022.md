<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Post-recovery regression controls

Parent reviewed visible recovery r1/r2 (50012793, ed7f7ccc): both passed every
recovery/capture gate, all eight PNG verdicts, stable surviving identities and
unchanged backend. Parent inspected both final fronts, independently checked
39 archived hashes per run and graceful Linux/Windows exits. Both recovered
fronts visibly contain the full scene and yellow/blue WebGL1/2 surfaces.
These are two scoped recovery passes, not a historical-blanking fix or full G4.

## Next bounded preparation

Luna prepares three serial fresh-prefix controls using existing runners and
the approved owned build/compositor/runtime. No diagnostic/production changes.
Prepare manifests, complete source archive, expected binary hashes and exact
commands on D: BEFORE any boot. Reuse the private staging procedure with small
explicit command/inventory/evidence-name parameters, not another framework.
Return for parent verification before execution.

1. check-g4-foreign-present.py, --arch x86_64, only --case own-dcomp and
   --case foreign-full-dcomp, --renderer gl --gl-driver virgl and owned GLES2
   compositor/config. Require producer exit 0, submitted first frame and EXACTLY
   4096 matching front pixels in each case; retain both PNGs and logs.
2. Identical independent control with --arch i686 and a fresh prefix/output.
3. check-g4-webview2.py --graphics-diagnostic --seconds 25, no Electron or fault
   flags, same accelerated native route. Use the approved cached host
   /home/debian/wex-g4/private-runtime/webview2-graphics-probe.exe and runtime
   /home/debian/wex-g4/private-runtime/runtime/Microsoft.WebView2.FixedVersionRuntime.155.0.4283.45.x64.
   Expected host/loader/browser/stack hashes are in the historical private
   virgl-consumer-evidence.tgz / virgl-webview2-strict/results.json; verify them
   before execution, not just version strings. Require both WebGL APIs available,
   not lost, exact default/texture pixels, complete FBO, errors 0, host/diagnostic
   exit 0 and no classified GPU/renderer/fatal/output/Skia/shared-context errors.
   Inspect the final actual front image as well as JSON.

Dependencies include check-g3-correctness.py, test_graphics_probes.py,
graphics_wexcomp.py, graphics_resources.py, run-isolated-graphics.py and the
consumer/analyzer dependencies. The foreign C++ source INCLUDES
test_g4_composition_probe.cpp: stage/hash BOTH, not only the top-level probe.
Audit remaining local includes/imports rather than discovering omissions live.
Preserve all manifest/source/compiled probe hashes and existing negative records.

## Execution after parent dispatch

Only the owned D:-backed VirGL guest; other agents/VMs are out of scope. Explicit
inner and outer renderer checks, offline per-run isolation/unique sockets, no
build of loaded binaries, no sandbox bypass, no fallback/capability override.
Prepare all three manifests before boot. Use unique post-recovery-* run names.
Serial controls may share one boot only while each next operation's complete
bounded maximum plus 90 seconds cleanup fits the launcher's 900 seconds.
Otherwise shut down cleanly and use another owned boot with new output names.
Record any reboot and the shared cache context. Stop the matrix on the first
failure; preserve it, collect bounded evidence and return for diagnosis, no retry.

These run AFTER recovery trials but in fresh processes/prefixes and a rebooted
guest. They prove matched regression cells, NOT resource survival in the faulted
process or absence of pre-reboot GPU-global corruption. The two recovery trials
already provide surviving-process/actual-front evidence; do not conflate scopes.
No additional GPU fault is needed or authorized in this matrix.

Inspect final PNGs and full logs; verify owned Wine/compositor cleanup and exact
identity SIGINT/QMP shutdown, QEMU and persistent Windows WSL exits. Commit/push
a concise report with actual cells, limits, paths and unrun cells. Parent reviews
before the next item. Keep historical stale-front/blanking uncertainty open;
no full G4/master merge or installed-shell/Edge/efficiency claims.
Standing stop limits remain 95% five-hour and 99% weekly used, with a pushed
checkpoint and continuation after reset; never consume reset credits.
