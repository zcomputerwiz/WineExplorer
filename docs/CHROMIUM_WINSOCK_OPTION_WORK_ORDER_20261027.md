<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review and next fix target: actual Winsock error10042

## Review of3d7ed787 network-bootstrap delivery

Read report and actual private guest archive (run-summary/Chromium/Wine logs).
Network child PID700 logged background operations, but that is not successful
socket I/O or proof that every Mojo channel is healthy. No fixture request or
ready page is demonstrated. Renderer→network dispatch-drop causal claim is
unproven: absence of NotifyBeforeURLRequest does not locate every earlier stage.
Sad-tab is a termination UI state, not proof of a navigation timeout/retry bug.
Keep actual observation (one network spawn seen, subsequent background logging,
HTTPempty, killed page) and treat the stronger explanations as candidates.

Important concrete overlooked finding: actual Chromium log repeatedly reports
Unknown error10042 mapped to net::ERR_FAILED. Microsoft defines10042 as
WSAENOPROTOOPT (bad/unsupported protocol option), NOT network unreachable from
offline isolation. Worker labeling it expected air-gapping is incorrect.
https://learn.microsoft.com/en-us/windows/win32/winsock/windows-sockets-error-codes-2
WSAENETUNREACH is10051, a different error. This does not prove10042 caused the
fixture failure, but it is a concrete API compatibility candidate to investigate
before claiming native socket functionality or patching Chromium navigation.

Parent read pinned7d982409 net/socket/tcp_socket_win.cc. Socket creation/options,
nonblocking setup, keepalive, port randomization and TCP initial-RTO requests
exist on relevant paths. Some optional-option failures are ignored, so do not
implement a guessed TCP option solely because its name appears. Identify the
actual failing API/level/option/return and caller's handling of that return.
Previous vmodule absence can mean no VLOG2 statements were exercised; it does
not itself prove command-line reconstruction is broken. No vmodule framework.

## Antigravity assignment: targeted real Winsock compatibility fix

Use own NEW D: root codex/chromium-winsock-unblock from network branch3d7ed787,
retain full combined Winefffa7de/build. Own Wine childcodex/g4-winsock-unblock
only for needed instrumentation/real fix. Preserve other workers/shared builds.

1. Trace exact10042 source using retained logs and pinned Chromium/Wine source.
   If needed, targeted +winsock diagnostics or tiny semantically inert failure
   tracepoints in OWN Wine build are authorized. Record API/socket family/type/
   level/option/length, actual return and WSA error with PID/time/caller context.
   Keep private bounded logs (a modest diagnostic-only byte limit may be chosen
   rather than failing prematurely at the old1MiB observation quota). No broad
   framework/global proc sampler, no identity/logging perfection project.
2. Use a tiny original real PE loopback socket control if useful: normal WSAStartup,
   socket/connect/send/recv GET against the existing exact-nonce local fixture,
   plus only the concrete Chromium option sequence implicated by source/trace.
   Linux/Python server receives actual bytes. Test candidate Wine, not hand-built
   success JSON. No arbitrary internet traffic or package installation required.
   Basic connect success alone doesn't validate a failing option sequence.
3. Implement the smallest demonstrated Wine API/translation/error-state fix with
   focused real socket regression(s), preferably native Windows control for the
   exact contract where useful. Never return unconditional success or remove
   security/features to hide a required behavior. If10042 is optional/irrelevant
   and handled correctly, report that precise result; do not invent a fix.
4. Keep target actual Chromium page fetch/render. One targeted diagnostic browser
   run is authorized if needed to locate the specific API failure. If it yields
   a demonstrated fix, ONE additional fresh post-fix normal Chromium verification
   is authorized without another paperwork pause. No unchanged repeated runs or
   broad matrix. If no supported fix emerges, preserve useful precise new status
   evidence and stop; don't chase unrelated API warnings.

Reuse owned D:R4 disk/private runtime/full combined compatible client/server.
Incremental OWN build for changed component, no unnecessary full rebuild/new
image/download/reinstall. Never modify loaded guest DLLs/builds: stop own prefix
and deploy new candidate privately under a fresh location as needed. Check space
before copying, preserve all attempt directories and consumed stages. Fresh
outputs/profiles/matched helper, original HTTP page and normal security/features,
privateLO/unique sockets/actualVirGL/timebounds/cleanupreserve/ownership and exact
owned shutdown retained. No sandbox bypass, feature disabling, in-process network
workaround, debugger attach/suspend, master merge or other VM/worker changes.

Commit AND push root/Wine candidate branches and original
docs/CHROMIUM_WINSOCK_UNBLOCK_RESULT_20261027.md with precise10042 classification,
actual test results/fix/page outcome and next blocker. Keep screenshots/runtime/
private logs D:/D-backed WSL. At95%five-hour safe bounded cleanup/checkpoint/push,
weekly99%stop; nevercredits. Reply codex then stop. Prefer a real small Wine fix
over another speculative network-service narrative or harness round.
