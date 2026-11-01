<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Owned thread/wait observation — offline review checkpoint

**PREPARED_NOT_RUN.** Implementation `c00f8950`, following OWNED_THREAD_PLAN.
No VM/SSH/Wine/browser/debugger/attach/suspend/token or privilege operations.
All source/tests/commits in the explicit owned C: graphics worktree. D: master,
other checkouts and submodule contents untouched; pre-existing unused ` M wine`
remains unstaged. Original page/HTTP/security/backend and installed payload unchanged.

## Narrow opt-in

Runner --owned-thread-diagnostic requires startup-diagnostic, HTTP and private-lo
proof before output creation. Only a readiness timeout invokes the inventory,
after the existing terminal front and BEFORE prefix cleanup. No readiness retry,
restart or observation on successful readiness/other failures. Primary timeout,
front, raw helper logs, observation errors and secondary cleanup failures survive.
One <=1MiB owned-threads.json, referenced/hashed in results.json. Diagnostic stays
passed=false/exit1; observations are not stacks, a cause or rendering acceptance.

Native selection requires exactly one visible unowned/top-level Chrome_WidgetWin_1
with this nonce route, pinned C:/Z: image, exact numeric HWND/WinPID/creation time
and valid client/window geometry. Existing native observer has a **schema extension**:
default window rows now additionally contain creation_time and top_level. Default
JSON is NOT byte-identical; normal selection/pixel acceptance semantics unchanged.
Separate --owned-threads mode revalidates HWND owner, exact image/title/nonce,
process creation time/geometry and GUI TID before/after; Toolhelp opens/query-filters
ONLY threads belonging to selected WinPID. QUERY_LIMITED_INFORMATION rights only;
no contexts, suspend/debug/write/token operations. <=128 records, bounded names,
optional GetThreadDescription with availability/HRESULT/query-error metadata.
Ownership mismatch/overflow fails rather than silently truncating. Missing names
or failed queries are not invented stacks. Strict JSON/schema/identity/count checks
include exact nested rectangle keys and exact-int values (False/True rejected at0/1).

Linux inventory independently requires the recorded Popen PID/start, exact retained
WINEPREFIX, full installed Linux image, complete app URL/nonce and fresh profile,
no child --type. Handles both exact argv arrays and the archived Wine unquoted,
padded command-title form without shlex-splitting paths with spaces. No basename/
substring acceptance or wider PID scan. Only this PID's task entries, <=128, with
TID/start before/after plus bounded comm/stat/wchan/syscall; process identity checked
again afterward. Disappearance/denial/reuse is explicit limited/error evidence,
never privilege escalation. Native failure does not broaden selection: any Linux
record still uses ONLY its independently validated Popen identity. No Win32/Linux
PID/TID mapping, even if numbers happen to coincide.

Whole observation capped5s inside existing285s/cleanup55 reserve. Query helper
startup/wait/kill/reap budget is inside its call cap (general logger's extra5s reap
tail is unsuitable). SIGKILL on timeout concerns ONLY the newly spawned query
helper's separate Linux process group, never the selected browser. Native stdout/
stderr parsing caps512KiB each; raw error logs retained. Defaults do not load the
new optional helper. No general orchestration framework or changes to other consumers.

## Actual offline validation

From `C:/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer`:

```powershell
wsl -d Debian --exec unshare --net sh -lc 'ip link set dev lo up && cd /mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer/scripts && python3 -B -m unittest test_graphics_private_loopback test_g4_edge_headful test_g4_edge_http_fixture test_g4_edge_owned_threads test_g4_edge_install test_g4_virgl_budget test_g4_virgl_launcher test_g4_consumer_harness'
node scripts/test_g4_edge_scene.js
pwsh -NoProfile -File scripts/test_g4_virgl_session.ps1
wsl -d Debian --exec python3 -B /mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer/scripts/test_graphics_x_socket_isolation.py --output-dir /var/tmp/wex-owned-threads-offline-xsocket-20261023-r1
git diff --check
```

**170 Linux tests PASS, no skips**, including all146 prior controls. Actual Node
fixture, intercepted controller and real native-path X-socket peer/inode control
PASS. Expected missing-Electron-helper argparse stderr is a passing negative.
New tests cover guard/default behavior, archived caption and complete padded argv,
zero/multiple/foreign/child/nonce selection, native schema/numeric/owner/creation/
TID/count/geometry failures, Linux reuse/disappearance/denial/limits/deadlines,
malformed/oversized logs, callback timeout, primary/cleanup error preservation.
Actual Python query-transport sleeper was timeout-killed/reaped within its cap;
raw stdout/stderr retained in the test, NOT a Wine/native API test.

Actual MinGW x64 cross-compile with timeout25, -O2 -static -Wall -Wextra -luser32
exit0, final warnings0; PE machine0x8664. **PE not executed; native API behavior
remains untested on Windows/Wine.** Synthetic native records are explicitly mocks.
Parent separately reported an exact-owned Python sleeper /proc transport check:
before/after identity, one verified task, actual comm/wchan/syscall observed and
child reaped; not Windows/Wine evidence, no cross-domain mapping.

Private artifacts at
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-owned-threads-offline-20261023-r1/`:
initial/final compile logs, original helper PE, offline-inventory-audit.json and
hash-verified X-socket logs/results. Initial FARPROC cast warning corrected;
prior artifact/log retained. Socket endpoints used Linux-native paths as required
by the known DrvFS limitation; logs copied D:, originals retained. No vendor files.

## Fresh preparation / next review

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-headful-owned-threads-r1-20261023.preflight/`
contains immutable PREPARED_NOT_RUN, preflight.json and source-stage.tar. Existing
private procedure adds only explicit edge-owned-threads command/inventory/evidence
parameters. Exact C: launcher/controller/run/stop/helper and D: storage paths recorded.
Offline audit: all11 tar/current source sizes/hashes (native C++ and Python helper
included),5 actual helpers,2 public seeds, installed reference,17 expected binaries,
unchanged original HTTP bytes/flags and absent prospective host run verified.
No deployed/guest hash or space claim. Future profile is fresh named, not guest-checked.

- Archive SHA256: `9592a28766cc3417bfe0b87abef582ea68b4d491f9466ee810af5fc415302479`
- preflight.json SHA256: `6097d9510908cff7043946e3dd38e52f07070e7ea2cf0955545c50e04521822c`
- Private C: procedure SHA256: `4c2aa192e23ba886ec25ff6a0022943072e72739b83e450ecfcb27b1c0735e44`
- Compiled original helper SHA256: `bfc6f2e31b869a1b078f793a4535c9dde9e1485b1df132da8979391acdf48681`

Return for parent source/test/provenance review BEFORE any live diagnostic. Future
dispatch still needs1GiB pre-stage space,17 actual binary/11 deployed-source hashes,
650/435 rawUTC guards, inner/outer VirGL/private lo proof,285 runner/300 SSH/900
launcher/90 shutdown reserve and exact identity SIGINT/QMP/child/Windows exits.
No additional run, debugger plan or speculative production/security patch now.

## Independent parent review: one diagnostic authorized

Parent read all native/Python/runner/test and private dispatch changes. Independent
170-test Linux rerun PASS/no skips, actual Node fixture and intercepted controller
PASS, actual native-path X-socket control PASS at
`/var/tmp/wex-owned-threads-parent-xsocket-20261023-r1`. Independent native x64
cross-compile with -Wall -Wextra emitted no warnings (D: parent-owned-thread-observer
PE; not executed). Parent's real owned Linux child inventory also passed, not Wine.
The nested JSON bool/int review finding was corrected with regression coverage.

Independently verified exact preflight/archive hashes above, all11 tar/current
sources,5 actual helpers,2 seeds, installed reference,17 expected binaries, source/
storage paths, opt-in flags, absent run and unchanged original HTML/JS hashes.
ONE live owned-thread diagnostic is authorized with this immutable preparation,
subject to all live gates above. No retries, debugger or additional run. Preserve
native and Linux records independently, all query limitations and actual front;
parent reviews before choosing any later stack observation or production patch.
