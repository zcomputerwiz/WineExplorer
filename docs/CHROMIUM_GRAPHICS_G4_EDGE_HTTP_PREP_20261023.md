<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Original fixture over isolated HTTP — offline review checkpoint

**PREPARED_NOT_RUN.** Implementation `698fa94e`; follows the activated HTTP stage
of EDGE_LOOPBACK_CONTROL_PLAN, after the preserved failed FILE/LO-UP control.
No VM, SSH, Wine/browser, installer or guest action. No HTTP live authority yet.
All edits/tests/commits used the explicit owned C: worktree from SOURCE_WORKTREE.
D: master, other checkouts, old preparations and submodule contents untouched;
pre-existing unused ` M wine` gitlink mismatch remains unstaged. No production fix.

## Minimal contract

`check-g4-edge-headful.py --http-fixture-control` requires BOTH
--startup-diagnostic and --private-loopback-control, including inherited private
namespace/lo proof, before output creation or any listener. Default FILE mode
does not import the optional helper and retains its argv/environment/behavior.
HTTP mode changes only app URL; original HTML/JS bytes, retained installed prefix,
NEW profile, backend, security, observer/pixels, logging quota, readiness/fixed
front schedule and terminal failure front remain unchanged. No renderer privilege,
extension, CDP, external networking, download or pixel/readiness retry.

New stdlib `g4_edge_http_fixture.py` snapshots just index.html/scene.js, each
bounded to1MiB, hashes them and binds127.0.0.1:ephemeral. Two exact GET routes:
`/<nonce>/index.html` and `/<nonce>/scene.js`; app URL includes #nonce. No path
resolution/file/proxy server, redirects or arbitrary URL. Reject wrong nonce,
paths, queries, traversal/encoded aliases, normalized // aliases and non-GET
methods. Explicit MIME/Content-Length/Connection:close; one HTTP/1.0 worker,
64-request quota,0.5s accepted-socket idle timeout,<=2s startup/join. No keepalive
or separate handler threads. Global runner285s/readiness45s bounds unchanged.

HTTP results retain listener/URL, source paths/sizes/hashes, actual monotonic
request start/end, method/target/status, confirmed body-write bytes, completion,
client events and internal errors. Status is attempted: a cancelled write is NOT
a completed200 response, and even a complete write is NOT browser consumption or
renderer readiness. Idle speculative connections/cancels are bounded observations,
not internal failures. Actual listener/worker/programming/quota errors fail closed.
Owned close shuts active sockets/listener and joins even after a cleanup error;
runner preserves primary errors, secondary cleanup errors and failure front.
Diagnostic mode always passed=false/exit1, even with complete healthy captures.
Neither unit tests nor a future HTTP outcome establish a file-specific cause/G4.

## Actual offline validation (all final exit0)

From `C:/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer`:

```powershell
wsl -d Debian --exec unshare --net sh -lc 'ip link set dev lo up && cd /mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer/scripts && python3 -B -m unittest test_graphics_private_loopback test_g4_edge_headful test_g4_edge_http_fixture test_g4_edge_install test_g4_virgl_budget test_g4_virgl_launcher test_g4_consumer_harness'
node scripts/test_g4_edge_scene.js
pwsh -NoProfile -File scripts/test_g4_virgl_session.ps1
wsl -d Debian --exec python3 -B /mnt/c/Users/Owner/.codex/worktrees/chromium-graphics-plan/WineExplorer/scripts/test_graphics_x_socket_isolation.py --output-dir /var/tmp/wex-http-offline-xsocket-20261023-r1
git diff --check
```

**146 Linux tests PASS, no skips**, plus actual-fixture Node and intercepted
PowerShell controller PASS. Expected missing-Electron-helper argparse stderr is a
passing existing negative control. HTTP tests actually request exact original
bytes/headers/hashes and reject other routes/methods; include idle preconnect then
queued GET, cancelled write then valid GET, HTTP/1.1 keepalive refusal, active-idle
shutdown, bind/start/join/accept/programming/quota failures. Runner tests exercise
mode/proof guards before output/listener, actual server via mocked browser callback,
URL-only change, normal optional-helper absence, timeouts and preservation of
primary errors/secondary cleanup failure. Existing namespace/runuser and renderer/
pixel/process/deadline/cleanup controls preserved. Host socket tests ran ONLY in
fresh isolated namespaces, not real guest/parent network modifications.

Actual X-socket control PASS: two owned Xvfb processes, distinct socket inodes,
matching SO_PEERCRED PIDs including first-after-second-start; test children exited.
Five results/log files copied and hash-verified at
`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-http-offline-xsocket-20261023-r1/`.
Initial Windows UNC copy spelling failed; WSL cp succeeded; Linux originals retained.
Native socket paths avoid the historical DrvFS bind limitation. No Wine coverage.

## Fresh private preparation / next review

`D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/edge-headful-http-r1-20261023.preflight/`
contains PREPARED_NOT_RUN, source-stage.tar and preflight.json with exact C:
launch/controller/run/stop/helper paths and D: VM storage. Private procedure adds
only explicit edge-http mode/inventory/evidence naming using the existing flow;
no new framework. Future profile/stage are fresh named paths, NOT guest-verified.

Offline audit PASS: all10 tar/current-source sizes/hashes (including new helper),
5 actual C: helper hashes,2 public seeds, accepted installed-evidence reference,
17 retained expected guest binary hashes, correct opt-in flags and absent host run
directory. Source commit in manifest is698fa94e; original HTML/JS unchanged.
No key copied/accessed, payload included, guest binary/deployed hash or space claim.

- Archive SHA256: `2a8a8583e21c4281101fa0e7dddc7246a89cfb32e4f5055037bf3dd5e470cdd4`
- preflight.json SHA256: `88a5784caa5dce0cf696a645b880064e2615969a9b7c09c66d2507892f26b5f4`
- Private C: procedure SHA256: `91e941ab1c055478ec47a02056a07a0f7a7a767551c5ac2eaa97220f198b6c21`

Parent reviews source/tests/private procedure and manifest BEFORE ONE HTTP
diagnostic. Future dispatch still requires>=1GiB before staging,17 actual binary/
deployed hashes,650/435 raw-UTC guards, inner/outer VirGL,285s runner/300s SSH,
900s owned launcher/90s cleanup reserve and identity SIGINT/QMP/child/Windows exits.
No boot now, no speculative security/production patch, no acceptance claim.
