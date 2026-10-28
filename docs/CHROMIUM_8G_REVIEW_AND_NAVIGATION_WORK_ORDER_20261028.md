<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review of the 8 GiB control and next navigation work order

Reviewed actual0dd0e07f script diff/report, private chromium-memory-8g-20261028
serial.log, native results/journal/environment, Chromium/helper logs, QEMU argv/
shutdown records and terminal-front.png. No new VM/build/test execution by parent;
worker32 host-test passes remain reported, not independently rerun.

## Accepted result and corrections

Actual QEMU -m8192; serial has no recorded OOM invocation/kill; PNG is a blank
Chromium app window, unlike the prior Aw,Snap. Native held browser PID224 remained
alive for45000ms. HTTP requests=[] and no JS/nonce readiness response occurred.
Page loading remains FAILED/NOT_PROVEN. Report acceptance mapping saying fixture
index/scene.js and nonce readiness are present is wrong: a URL nonce and server
source files do not prove consumption. WAIT_OBJECT_0 is0, not report's267.

Environment omits WEX_TERM_TRACE, unlike the4GiB control-channel run. Therefore
zero kill_process/POLLHUP trace strings cannot establish no process deaths or
intact control channels. RAM is not literally the only diagnostic-environment
difference. No new RAM repeat or trace-equality certification round is needed:
accept scoped evidence that this actual8GiB run had no recorded kernel OOM,
keep8GiB for the next functional investigation, and retain the comparison limits.
The direct browser timeout is not itself page failure; the empty requests and
viewed blank front establish that the target page did not visibly load.

Guest cleanup complete/remaining servers[]/prefix processes{} and graceful
QMP guest shutdown/QEMU0/no fallback are confirmed in raw results. Do not infer
additional process terminal statuses from missing fields. Source mode changes
are small plumbing, not a production fix or imported global candidate pin.

## Concrete next boundary

chromium.log121-122 records navigation observer for exact loopback URL. Network
service PID724 logs NotifyBeforeURLRequest for background HTTPS requests later,
but no target127.0.0.1 request appears. Background10051 errors are consistent
with the private loopback-only network and do not explain an absent local URL.
The network-service message652 alone still does not prove a service crash.

Helper logs show child ProcessHandleCount stub calls followed by real
ProcessHandleTable calls with4000-byte buffers (e.g.771-773,999-1001). Parent
read exact pinned Chromium HandleCloserAgent::CloseHandles: allocates
handle_count+1000 entries, then checks table query status/returned length. Zero
count is a genuine compatibility gap, but is not automatically fatal. Do not
patch it merely because the assessment calls it P0, nor claim all IPC/TCP works
from static unit-test source. No new full API matrix or EGL work is selected.

## Work order: target navigation to local request, then a supported fix

Antigravity coordinates the SAME implementer on own D: root branch
codex/chromium-navigation-unblock from0dd0e07f, with this parent review applied.
Reuse8GiB and the actual matched02d4ef2 full combined Wine candidate/runtime.
No new Wine build/trace deployment initially; preserve normal sandbox/features.

1. Read retained logs and the pinned7d982409 navigation/network handoff source.
   Identify where target navigation should create its network URLRequest and
   what required renderer/broker/Mojo callback can delay that handoff. Treat
   current evidence as an unresolved boundary, not proof of a particular API bug.
2. ONE fresh diagnostic may add Chromium's existing logging-only NetLog to a
   fresh private OUTPUT path, Default capture, bounded to8MiB; confirm pinned
   handlers and flush behavior first. network_switches.cc declares
   net-log-max-size-mb and net-log-duration; a supported40-second capture that
   flushes before the45-second observation is preferable. No IncludeSensitive,
   Everything, TLS-key capture, remote debugging port or externally exposed UI.
   If needed choose only a few existing source-backed navigation/Mojo VLOGs or
   existing bounded trace events that actually exist in this build; do not add
   inert vmodule names, all-syscall logging or a global process observer.
3. Correlate nonce URL through browser navigation, URL loader/service handoff,
   target URLRequest/connection/completion and fixture server. Distinguish target
   traffic from background requests. A missing event in an unflushed/inactive
   logger is unknown, not proof the call did not happen. Preserve actual child
   identity/status only through existing owned creation/lifecycle evidence.
4. If evidence proves a small real Wine defect, fix it on an OWN child of the
   current combined candidate preserving both histories, use a focused real PE
   or existing regression for that semantic, incrementally build OWN matched
   components, and run ONE fresh normal post-fix page verification without a
   further paperwork pause. An async Winsock probe must reproduce the actual
   nonblocking/IOCP/WSAEventSelect path if implicated; the old blocking GET probe
   cannot certify that path. Otherwise stop with the exact last successful
   boundary/first unsupported operation and the smallest justified next fix.

No speculative API/sandbox/network-service feature switches, proxy bypass,
browser memory caps, swap, new image, reinstall/download, master merge, other
worker/shared loaded build writes, debugger attach/suspend, generic harness or
unrelated conformance work. Fresh stage/output/profile/helper, no evidence/cache
deletion or consumed-path reuse. Same owned R4 disk, unique sockets/privateLO/
actualVirGL/space/capacity/idleness/rawUTC/lifetime/cleanup reserves and exact
owned QMP ACK+guest SHUTDOWN/QEMU0/no-fallback/child cleanup remain required.

Report actual HTTP index/JS/nonce readiness, viewed front, kernel OOM/native and
cleanup separately from collection success. All private material D:/D-backed
Debian. Commit AND push root/Wine changes if any and original
docs/CHROMIUM_NAVIGATION_UNBLOCK_RESULT_20261028.md, reply to Codex, then stop.
At95%five-hour safely finish bounded cleanup/checkpoint/push; weekly99%stop;
nevercredits. The goal is the normal page loading, not a perfect harness.

Pinned sources inspected by parent:
- https://raw.githubusercontent.com/chromium/chromium/7d9824099030f5c28c939ba08654562faaad7c09/sandbox/win/src/handle_closer_agent.cc
- https://raw.githubusercontent.com/chromium/chromium/7d9824099030f5c28c939ba08654562faaad7c09/services/network/public/cpp/network_switches.cc
