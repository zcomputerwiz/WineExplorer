<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# G4 merge-readiness progress: real restart and controlled synchronization

2026-10-21, branch `codex/g4-r1-completion`, parent `228b2058`, Wine unchanged
at `1dcd479`. **Not yet merge ready.** This is a test/harness-only step, not a
production synchronization rewrite. No other worker's branch or VM was changed.

## Newly proven cells

| Check | Software GL | Explicit lavapipe | Scope |
| --- | --- | --- | --- |
| Electron fresh process, cookie write/flush, seven lifecycle checks | PASS | PASS | Real private x64 Electron44.9.2 / Chromium152.0.7977.167 |
| Electron new process, prior cookie read, seven lifecycle checks | PASS | PASS | Same private prefix/profile; prior server stopped |
| Signal after observer publication but before RegisterWait | PASS x64/i686 | PASS x64/i686 | Deterministic test-process barrier |
| Signal while first notification callback remains active | PASS x64/i686 | PASS x64/i686 | Callback-tail barrier; concurrent callbacks allowed |
| 65 registration failures, rollback/retry | PASS x64/i686 | PASS x64/i686 | Actual kernel handles stable, then completion succeeds |
| Replaying stale profile expectation | Rejected as intended | NOT RUN | Negative oracle, not a successful application run |

The existing28-group software correctness evidence is in
`CHROMIUM_GRAPHICS_G4_WSL_VALIDATION_20261021.md`. The two new instrumented groups
run separately, not silently added to a historical full-suite count. No Wine DLL
changed; the previous full-suite and native normal-creator-exit results are not
being relabeled as newly rerun here.

## Real process restart/profile persistence

`scripts/check-g4-webview2.py --profile-proof` opts the original Electron fixture
into a Chromium persistent-cookie proof. The fixture uses its actual default
session's cookies, an expiration date and awaited flushStore; it does NOT write
a fake profile file directly. These operations follow Electron's
[cookie API](https://www.electronjs.org/docs/latest/api/cookies). The `.invalid`
cookie URL is not navigated/fetched; all runs remain in isolated network namespaces.

The first fresh profile must lack the cookie. The harness generates a random
128-bit nonce; the fixture writes/flushes/reads it. After normal app exit the
harness stops/waits its owned wineserver, verifies no remaining server, closes
its display and writes the evidence manifest. A later independent invocation
uses `--restart-from FIRST_OUTPUT` with a fresh output/display/socket directory.
It reuses only the first run's private prefix/profile, does not run wineboot -u
again, requires the exact previous nonce and writes a new one.

Reuse requires a successful cleaned fresh proof manifest, identical runtime and
fixture hashes, Wine loader/server and both architectures' graphics DLL hashes,
native win32u.so, renderer selection and Vulkan ICD hash. Missing/failed/chained
manifests, mismatches, symlinked state and running servers are rejected. An
exclusive nonblocking flock serializes access to the private state. It is a
diagnostic ownership guard, not a security boundary against malicious manifest
editing. A new cookie value consumes the old expectation; start a new pair for
another reproducible proof rather than pretending the original manifest is current.

Every positive process run exits0 and independently passes all seven lifecycle
checks (input, scrolling, resize, minimize, restore, reload, resize-back), plus
continuing late-frame progress and zero checked GPU/Skia/fatal signatures. Each
has34 samples. DOM reload is not used as a substitute for process restart.
The renderer counters reset on fixture reload; compare late progression, not
first/last frame subtraction across different page loads.

Private evidence root `/var/tmp/wex-g4-tree-20261020/`:

- `r3-profile-gl-first`: nonce `0b4a26caba54da2048819a369e178fb2`, no prior cookie.
- `r3-profile-gl-restart`: reads exactly that nonce; writes
  `a078a53981f9ecfe5a99927682eaabbe`.
- `r3-profile-vk-first`: nonce `5c1f24187336107bbef461cc83e40cda`, no prior cookie.
- `r3-profile-vk-restart`: reads exactly that nonce; writes
  `6b40fe68c7dd5ae54c33896fea1691a7`.

Actual Xvfb front-window `screen-18.png` captures from BOTH restarted processes
were inspected: single blue square, correct fixture text/menu/input/button,
uniform light-green background and intact scrollbar; no blank window, stale
rectangle trail or obvious composition corruption. These are actual display
captures, not capturePage-only evidence. They do not certify temporal/AA fidelity,
Classic/Open Luna or hardware rendering. Captures stay private and ignored.

Real runtime SHA256 remains
`448da7f0a402b7299f87dc7dc896d48fd58a726ccbcf65b69820745f1bd38bf2`.
Fixture main.js SHA256:
`c0e21bd9f74644d781e898328b43efaed4422962ee141b2294d660b93f05436c`.
Full graphics binary identities, child command lines/DLL mappings and flags are
in results.json/processes.jsonl. No sandbox bypass, version spoof, global DLL
replacement or browser software-renderer flag was added. The lab's Wine/Mesa
software renderer remains explicit; sandbox-request flags do not prove containment.

## Controlled synchronization and API-failure proof

`scripts/test_g4_fence_interleaving_probe.cpp` instruments ONLY its loaded
Wine d3d11.dll import-table entry for RegisterWaitForSingleObject. It changes
neither on-disk DLLs nor Wine production code, system prefixes, other processes
or persistent registry state. It restores the import and page protection after
draining its fence callbacks. It fails closed if the expected import seam is
absent; this Wine-implementation probe is not a native Windows conformance test.

The intercepted registration thread is held immediately before the real wait
registration, after the actual Wine observer/event publication and waiter insertion.
An independent producer registration bypasses the hook. Controller Signal1 must
complete (real producer completion event and exact value1) before the test lets
the observer's real RegisterWait proceed. Observer completion must then wake.
This selects the interleaving with event handshakes, not stress/sleep guesses.

The wrapper then holds callback1 AFTER the real Wine callback processed its
waiters but BEFORE returning to the threadpool. Controller registers threshold2,
signals and waits for actual producer completion2, then permits callback return.
Observer must wake at exact value2. Concurrent callbacks are allowed: the test
does not invent serialization guarantees. Final callback counts are2 on GL and
2(x64)/3(i686) on lavapipe, registrations1, timeout0. The extra callback is not
a duplicate external-completion assertion. This covers an active callback tail,
NOT every inaccessible internal threadpool rearm instruction/interleaving.

The separate failure case rejects65 actual RegisterWait calls with
ERROR_NOT_ENOUGH_MEMORY. Every SetEventOnCompletion must return the corresponding
HRESULT and must not prematurely signal the caller event. Nonzero live kernel
handle counts (not Wine's success-zero ProcessHandleCount stub) are constant
after warmup: x64=48/48, i686=50/50 on both backends. Switching off rejection must
allow real registration, Signal1 and exact completed-value1. This proves scoped
registration-failure rollback/retry, not every malloc/duplicate/callback failure.

Final evidence: `r2-iat-final-gl` and `r2-iat-final-vk`, both PEs exit0, both
required groups PASS, `test_process_iat_instrumentation=true`. Initial confirming
runs `r2-iat-interleaving-gl/vk` also pass but lack the later explicit JSON flag.

## Reproduction

Use the same owned combined WoW64 build and private runtime as previous reports.
Always wrap commands in `scripts/run-isolated-graphics.py --socket-root OWNED --`
with fresh output directories/filesystem X sockets. Never rebuild underneath a
running probe or reuse another worker's prefix/display.

For each backend, run check-g4-webview2.py with the private Electron runtime/host,
`--electron-app scripts/g4-electron-probe --lifecycle --seconds 35 --profile-proof`
and fresh `--output-dir FIRST`. After successful cleanup run the same command
with `--restart-from FIRST --output-dir SECOND` (profile-proof is then implicit).
Vulkan adds `--renderer vulkan --vulkan-icd /usr/share/vulkan/icd.d/lvp_icd.json`.
Use new absolute owned paths, not the consumed cookie expectations above.

For synchronization run check-g3-correctness.py with
`--wine-build /var/tmp/wex-g3-repair-20261018/clean-source/build/wow64`
and `--fence-interleaving --output-dir NEW`; Vulkan adds the same renderer/ICD.
This mode is mutually exclusive with the other suites. Wine-free tests:
`python3 -B scripts/test_g4_consumer_harness.py` (6 PASS) and
`python3 -B scripts/test_graphics_harness.py` (4 PASS).

## Negative/failed commands and final state

`wsl -d Debian --exec node --check /var/tmp/wex-g3-repair-20261018/clean-source/scripts/g4-electron-probe/main.js`
failed because Debian has no node executable (`execvpe(node) failed: No such file
or directory`). No package was installed. The existing Windows Node at
`C:/Program Files/nodejs/node.exe --check scripts/g4-electron-probe/main.js`
passes; actual Electron also parsed/executed the fixture in all four positive runs.

The deliberate stale test repeated the GL restart command with
`--restart-from /var/tmp/wex-g4-tree-20261020/r3-profile-gl-first`
and `--output-dir /var/tmp/wex-g4-tree-20261020/r3-profile-stale-token-negative`.
Expected old nonce0b4a... differs from actual cookiea078... . Fixture records
passed=false/flushed=false and exits2 BEFORE navigation/writing a replacement.
The runner records diagnostic_exit4 (PowerShell outer command exit1), no animation
or lifecycle pass, and clean server teardown. This expected rejection is retained
as negative evidence; it is not an application regression, crash-recovery test
or passing normal run. The two original positive pair results are preserved.

All positive/negative consumer runs and final synchronization runs have successful
server cleanup with no remaining servers. No owned test processes remain after
the final process check; displays were stopped. Private prefixes/profiles/logs
are retained for diagnosis; no VM or other worker's state was modified. No
production bug was found/fixed in these slices, and no master merge was made.

Still open: actual GPU-process crash/device-loss recovery (including the native
forced-creator-exit discrepancy), remaining callback/cancellation/allocation
interleavings, broader COM/FD/mapping lifetime, multi-context GPU ordering and
efficient backend sharing/private DXVK interop. Real WebView2 profile persistence
and CEF are not proved by this Electron result. Inherited upstream DXGI67 and
D3D11 legacy-sharing exit5 failures remain open. Installed Classic/Open Luna,
X11 outer-compositor OFF/ON, Wayland/wexcomp, resolution/taskbar/toolbars/tray and
physical older GPU acceptance remain NOT RUN. No G5 or complete-G4 claim.
