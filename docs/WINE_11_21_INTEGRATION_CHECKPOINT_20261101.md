<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Wine 11.21 integration checkpoint — 2026-11-01

## User authority and published stable state

The user authorized updating WineExplorer master to development, and integrating
upstream Wine 11.21 into Wine development as appropriate. Preserve Wine master
as its upstream-synchronized branch. Use Claude/Qwen for focused assistance.

WineExplorer remote master and development are both 31d228ee. Master was
fast-forwarded, preserving every prior commit. The main D: checkout also reached
31d228ee; its existing Wine checkout and untracked files were left alone.
The accepted result remains DEVELOPMENT_VALIDATION_20261101.md: fresh-prefix
normal Chromium with actual front pixels and basic desktop functions passed.

Wine remote master matches the upstream release commit
2528aff26208257fdbf980666a4f0e66d47563ce (Release 11.21).
Wine development is still the validated b5e05301. No Wine master rewrite occurred.
The upstream site denied automated access through Anubis; the primary mirror
confirms the release: https://github.com/wine-mirror/wine/commit/2528aff26208257fdbf980666a4f0e66d47563ce

## Candidate, not yet accepted

Wine branch codex/wine-11.21-integration contains merge
310c8d0d40f41b6b11501753dd49533502235c83, parents:

- b5e05301c4a52bff777a6b9fd6bfe03c3428d035 (validated project development);
- 2528aff26208257fdbf980666a4f0e66d47563ce (upstream 11.21).

The upstream side adds 115 commits since the shared 66caeb46 base. Merging
preserves the existing project histories and the upstream release ancestry.
The root candidate branch is codex/wine-11.21-validation; its candidate gitlink
is not permission to promote either development branch before runtime checks.

Only two source files conflicted:

1. kernelbase/process.c: adopt upstream's attribute-list forwarding to
   create_nt_process; retain is_elevation_required, SECURITY_CAPABILITIES
   extraction, NtCreateLowBoxToken token substitution, failure handling and
   single owned-token close. Parent/job/machine/handle-list handling now follows
   upstream, including inheritance gating.
2. winewayland.drv/wayland_surface.c: take upstream's update function, removing
   the unused child win_data get/release and diagnostic. Retain the v11
   cross-process present/attach/detach paths that fixed white Chromium windows.

Regenerated tools/make_requests from LF inputs in a disposable Linux directory.
The generated handler/trace files agreed; the combined protocol version required
an additional increment. The full matched client/server build is required;
never deploy this as a handful of DLL overlays on an old server.

## Independent reviews

Claude auto-merge review 92d39a51-2cb5-4672-85bb-f4d79d67cefa found no concrete
security regression. Its follow-up 78ac3acc-041c-42d8-abc0-b2b74594d97d verified
resolved process creation, inheritance gating, lowbox and cleanup preservation.
Qwen's completed continuation 1f955c60-35ff-422f-a8f3-8dbb294cba45 approved the
Wayland resolution with no concrete locking/role/lifetime defect. Its initial
34564677 job ended with an intention rather than a verdict, so it was explicitly
continued. These are source reviews, not runtime acceptance.

## Live build and exact continuation

No VM is running. Only the owned background Linux build is active.

- Workspace: C:/Users/Owner/.codex/worktrees/development-validation/WineExplorer
- Wine submodule branch: codex/wine-11.21-integration
- Linux source/build reused in isolation:
  /var/tmp/wex-development-validation-20261101/source and build
- New status/logs: /var/tmp/wex-development-validation-20261101/wine-11.21/
  status.json, configure.log, compile.log
- Driver: D:/DeepThought/ChromiumLab/wine-11.21-integration-20261101/build-11.21.py
  (Linux PID 587242 at launch; verify current identity before any signal).
- Configure: --enable-archs=i386,x86_64 --disable-tests; make -j16.

At checkpoint status was phase=compile with no reported compile errors. Wait for
passed=true; do not mistake an in-progress log for a completed build. Existing
source assembly modifications were saved in prior-assembly.patch; only owned
generated configure/configure.ac/include Makefile changes were restored to
switch the source to the merge, then normal project assembly was rerun.

1. Check the status and both completed agent results; do not duplicate workers.
2. Run the prepared stage-runtime.py under the NEW private host directory below.
   It checks the NEW build status and writes staging/archive/runtime-manifest
   under lab/wine-11.21, preserving the prior b5 runtime and evidence. It includes
   real TTF fonts, metadata and both architectures, with the loader symlink kept.
3. Boot the existing reviewed 8 GiB R4 launcher using a fresh unique run name.
   Reuse the earlier launch.py and identity-bound stopper; do not create a VM
   or alter other workers. Check disk space before copying.
4. Deploy the complete new archive into a fresh owned guest runtime, re-hash all
   files, and transfer validate.py, its probes, fixture and helper. Use the
   existing pinned compositor binary (94faa651) unchanged.
5. Fresh guest root is /home/debian/wine-11.21-validation-20261101. Prepared
   validate.py uses that root and adds 1000 suspend/context iterations for each
   PE architecture under C.UTF-8, with a watchdog and outer deadline.
6. Run through run-isolated-graphics.py with --private-loopback and a unique
   socket root, then runuser debian, just like the accepted previous control.
   Validate fresh init, 32/64 startup, suspend/context, Start menu, Notepad,
   File Explorer, sandbox-mode network service, HTTP200, nonce/WebGL and actual
   screenshot pixels. No browser security/feature bypasses.
7. Preserve results/screens/logs and use owned server/compositor cleanup and
   QMP acknowledgement + guest shutdown + QEMU0/no-fallback. No broad pkill.
8. Only on success publish Wine development, update root development pin/docs,
   and bring WineExplorer master to that newly validated development state.
   Wine master remains upstream-only. No force pushes or active branch rewrites.

Private host directory:
D:/DeepThought/ChromiumLab/wine-11.21-integration-20261101/

Prepared files there: build-11.21.py, wine-11.21.bundle, stage-runtime.py,
validate.py, desktop-validation-probe64.exe/32.exe,
suspend-context-probe64.exe/32.exe, run-isolated-graphics.py,
checkpoint-build.txt. The earlier accepted lab contains reusable deploy/launcher
examples, fixture files, graphics_wexcomp.py and exact compositor binary build.
The new deploy orchestrator has not yet been adapted/executed: use fresh paths
rather than rerunning the old deploy over its existing directories.

## Tool failures and workarounds

exec_command still fails CreateProcess with batch file arguments invalid.
Use the Node REPL child_process bridge, structured argv and bounded output.
The main checkout fast-forward's enormous diffstat exceeded the capture buffer,
terminating output capture; its completed HEAD and source status were separately
verified at 31d228ee. Do not repeat or reset it. Preserve untracked scratch files.
All prior accepted runtimes, prefixes and evidence remain intact. Automatic
polling remains disabled per the user; no automation was re-enabled.
