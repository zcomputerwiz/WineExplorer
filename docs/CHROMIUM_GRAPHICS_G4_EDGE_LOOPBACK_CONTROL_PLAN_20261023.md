<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# One ordinary-HTTP navigation control before a speculative patch

Parent reviewed65081f50: rehashed all105 archived files, inspected the real blank
front and browser file trace. The browser read all637 original HTML bytes; no
scene.js read/renderer launch/readiness was observed. Payload persists and all
owned guest/controller shutdowns are clean. Neither file-open failure nor sandbox
failure is established. Keep both failed file-URL arms intact.

Next high-value diagnostic is the SAME original page through ordinary loopback
HTTP, with unchanged Edge binary, app-window mode, Wine stack and security.
This separates file-response/navigation handling from ordinary web navigation.
It is a controlled transport/origin change, not a fix or replacement acceptance
for the file case. It does not isolate the exact cause if outcomes differ.
No security/file-access-policy relaxation, external networking or production patch.
Parent plans/reviews; Luna implements OFFLINE first, no VM/browser until review.

## Minimal implementation

Reuse the existing runner/fixture/observer/pixels/diagnostic mode. Add one explicit
loopback-fixture option requiring startup-diagnostic mode; normal file-smoke and
startup defaults stay unchanged. Keep NEW output/profile, same retained installed
prefix. Do not modify the original HTML/JS content or inject renderer privileges.

Use a small standard-library HTTP server bound to127.0.0.1 with an ephemeral port
INSIDE the existing private network namespace. Only GET of two exact nonce-bound
routes serves the staged original index.html and scene.js; reject all other paths,
traversal, queries/unknown methods and avoid request/redirect proxying. Correct
explicit Content-Type and Content-Length, complete request/status/byte records and
source hashes. Page URL retains #nonce as existing fixture expects. No general
file server, arbitrary command/URL inputs, public listener, dependency or download.
Server receipt/write alone is not browser consumption or renderer readiness.

If required, add an explicit opt-in to run-isolated-graphics.py that raises ONLY
the loopback interface AFTER unshare --net, before dropping to the test user.
Default isolation remains unchanged. Verify this is not the original/guest network
namespace and no other interfaces/routes are introduced; refuse loopback mode
without isolation. Do not touch the guest's real NIC, routes, DNS, SSH, firewall,
existing sessions or another worker. No access to internet/LAN/host services.

Record exact URL/argv, listener/namespace/loopback state and all original source
hashes. Start/stop the owned server in the existing total<=285s bound with joins/
cleanup errors fail-closed. Preserve the45s readiness bound, fixed front scheduling,
terminal failure image,128MiB soft trace quota, observer ownership/geometry guards
and honest no-GPU-compositing classification. Diagnostic mode always remains
non-acceptance/exit1; if readiness arrives, retain exact page/frontend observations
without silently crediting a normal graphics/G4 pass.

## Executable tests and handoff

Tests must actually request the two routes and compare exact original bytes,
headers and hashes; exercise wrong nonce/path/traversal/query/methods, no arbitrary
filesystem access and listener shutdown. Test opt-in isolation configuration with
mock subprocesses plus an actual bounded isolated loopback-only check if available;
do not bring up loopback in the parent/real guest namespace for testing. Test
mode gating, normal-mode unchanged command/environment, server startup/request/
cleanup failures and preservation of primary browser errors. Execute the existing
104 focused controls/actual-fixture Node tests and relevant X-socket isolation tests.
Avoid a new framework: reuse stdlib and existing contracts.

Commit AND push implementation/tests and a fresh complete PREPARED_NOT_RUN
manifest; return for parent source/test/manifest review before ONE HTTP control.
Only owned D:-backed guest; >=1GiB space before transfer/create, all deployed/
installed17-binary/source/helper hashes,650/435 raw-UTC guards,900s launcher/90s
shutdown reserve and exact identity/QMP/child/controller exit evidence. No reinstall,
new proprietary payload, source-art publication, loaded-binary rebuild, token/OOBE
imports, master merge or silent retries. At95% five-hour used safely checkpoint;
weekly99% used stop; never consume reset credits.

After this ONE reviewed control: if HTTP reaches renderer/readiness, record the
file-navigation gap separately and plan the observed remaining native-geometry/
graphics issue or an uninstrumented confirmation. If HTTP also stalls, preserve
that evidence and select a narrowly owned thread-stack/navigation observation.
Do not infer token/API causality merely because a renderer was not sampled.
