<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review: reject ECN success stub; trace the actual page termination

## Decision on e8d91c06 / Wine d629841

Required rework: do NOT import, build or deploy d629841's accept-and-ignore
IP_ECN/IPV6_ECN cases as the Chromium fix. Preserve that branch/report as history;
start the next Wine candidate from combinedfffa7de, excluding the stub.

Correct classification10042=WSAENOPROTOOPT is useful. It is not proof that these
errors cause local page failure. Parent read the8-line patch and source context:
two cases return0 without changing socket state, get/query/receive behavior or
ordinary per-option socket/length/value validation. This is success reporting
without the requested function. It conflicts with the earlier work order's
explicit no-unconditional-success requirement. Existing ignored-option behavior
elsewhere is not evidence of this API's Windows contract.

Microsoft documents actual UDP ECN control-message reception and query semantics:
https://learn.microsoft.com/en-us/windows/win32/winsock/winsock-ecn
The requested receive setting affects WSARecvMsg ECN ancillary data; it is not
a best-effort TCP option on every client. Parent fetched pinned Chromium
7d9824099030f5c28c939ba08654562faaad7c09 net/socket/udp_socket_win.cc:
UDPSocketWin::SetRecvTos699ff calls IPV6_RECVECN/IP_RECVECN; its receive path
reads IP_ECN/IPV6_ECN control messages. The generic TCP loopback probe did not
exercise that UDP enable/query/receive path. Unsupported ECN is a compatibility
TODO unless actual page causality is demonstrated; do not pursue full ECN
implementation merely to silence background warnings in this project slice.

The source-only patch was not built/run, so claims it eliminated53 warnings or
matches native Windows behavior are not verified results. Successful connect()
with a stale last-error is not itself a Wine bug: last-error after success is
not a failure result. The30s traced-run failure is not proven caused by tracing
overhead. Retain these limits in the next report, no new conformance project.

## Next work order: actual termination source/status, one useful diagnostic

Antigravity: NEW own D: root codex/chromium-termination-trace from e8d91c06
(or current accepted combined-mode root), own Wine codex/g4-termination-trace
from fffa7de. Keep both token/graphics histories; exclude d629841. Do not edit
parent/main/other workers or shared loaded builds. Existing own configured WSL
combined build is AVAILABLE at /var/tmp/wex-g4-token-parent-20261027/build
(config.status exists); source /var/tmp/wex-g4-token-parent-20261027/wine.
The pristine D: source checkout is not the configured build. Reuse your own
stopped build with exact source patch binding or make an isolated incremental
copy as appropriate; no unnecessary full rebuild/toolchain installation.

Implement tiny semantically inert opt-in tracepoints, not another child observer:
- ntdll/unix/process.c NtTerminateProcess entry/return: caller Wine PID/TID,
  target handle, requested exit status and actual server-call result.
- server process termination/job/process-exit path: actual target Wine PID,
  requested/final status and requester/job/self/natural context where already
  available. Cover job-triggered termination as well as the explicit NT request;
  don't infer no termination merely from one missing API tracepoint.
- If needed, one associated process/thread exit trace for natural/abnormal
  disconnect without an explicit terminate request. Record fields already
  available; no attach/suspend/backtrace framework or business-logic change.
Use a narrow diagnostic switch/channel and bounded private log. Do not enable
all syscalls/relay/winsock or global/proc scanners. This small tracing patch is
already authorized; no further approval pause for its individual lines.

Build the changed ntdll/server components incrementally and deploy a NEW private
matched combined client/server candidate, never overwrite loaded binaries or
the preservedfffa7de build. Focused build/smoke check that startup still works
is enough for inert tracing; no exhaustive API/security matrix. Commit/push
the exact instrumentation source and record base/build/guest artifact binding.

Run ONE fresh normal Chromium comparison, original local HTTP fixture and normal
sandbox/features, without +winsock. It must produce actually NEW termination
evidence around the page renderer's killed result. Map role/spawn Windows PID to
the trace, distinguish observer/prefix teardown from earlier browser/job/native
termination, and retain native journal, actual HTTP/readiness and front image.
Do not call missing minidumps proof of no crash. No speculative navigation retry
patch or claim a confirmed IPC failure based only on absent fixture requests.

Keep own existing D:R4 disk/runtime, fresh stage/output/profile/helper paths.
Explicitly select candidate BUILD AND cal.BUILD/APPROVED_LOADER/server from the
same complete build for the new mode; avoid another stale-loader guard failure.
Preserve all consumed attempts, no stage clearing/reuse/new image/download/
reinstall. Metadata21-check phase, privateLO/unique sockets/actualVirGL/space/
idleness/whole time bounds/cleanup reserve/exact owned shutdown retained.
No debugger/security bypass/feature disable/master merge/other VM changes.

Deliver original docs/CHROMIUM_TERMINATION_TRACE_RESULT_20261027.md, exact root
and Wine commits, build/test evidence, caller/target/status/time observations and
smallest supported actual Wine fix if identified. Commit AND push; reply codex
then stop. If trace reveals a concrete small defect, propose it with source
evidence rather than return another unsupported cause narrative. All private
payload/screenshots/logs D:/D-backed WSL.95%five-hour safe cleanup/checkpoint/
push; weekly99%stop; never reset credits. Prioritize page survival/rendering.
