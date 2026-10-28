<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Control-channel parent review and renderer-exit work order

Parent reviewed root15558e62 and Wine02d4ef27 (child ofa91ceea), the retained
helper.stderr.log, native journal/results, launcher QMP results and actual PNG
under private D:/VirtualBox_VMs/WEX-G4-VirGL-20261022/
chromium-control-channel-20261027. This is a source/evidence review; no new build,
browser run or independent rerun of worker unit counts was performed.

## Accepted evidence and limits

The exact new event is confirmed at helper.stderr.log1207: Wine PID0268,
event0x11 (POLLIN|POLLHUP), is_terminating0, server fd726. Existing server policy
then calls kill_process(...,1) and forces status1. Browser Wine PID00dc (220)
later requests termination of0268. No new request_error lines were recorded;
two client_socket_close lines concern other processes. This identifies the
server trigger, not the initiating reason for losing the connection.

The report's claims that the renderer Unix process had already exited, that
wineserver could not contribute to its death, and that a client close is ruled
out are unsupported. POLLHUP describes a channel, not an OS wait status:
https://man7.org/linux/man-pages/man2/poll.2.html . Limited close instrumentation
does not cover arbitrary close/shutdown, native fatal signals, or every Wine
exit path. In exact02d4ef2 dlls/ntdll/unix/thread.c1497-1510, abort_thread() can
reach abort_process(), whose _exit() bypasses process_exit_wrapper(). This is
a concrete missing observation, not proof that this path was taken.

The DXIL/dxcompiler warnings at82.311/82.323 belong to Wine PID0200, not the
affected0268. Their proximity does not establish a renderer graphics failure.
Do not select a DXIL download, graphics patch or sandbox change from these logs.

Actual native results remain passed=false, native_timeout after45000ms on held
browser PID220, helper error Linux helper failed/exit mismatch, HTTP requests[].
The viewed PNG is Aw,Snap RESULT_CODE_KILLED. Orchestrator consumer0/archive0
means collection completed, not Chromium passed. Guest cleanup_complete=true,
remaining_servers=[], remaining_prefix_processes={}; launcher records matching
QMP ACKs, guest SHUTDOWN, QEMU0 and no fallback. No broader Windows/Xvfb exit
claim is inferred from absent fields.

Trace additions preserve the main control flow. The request.c log captures
saved_errno but perror still reads ambient errno after logging; use saved errno
for the diagnostic when touching that code, without a separate hardening round.
Do not interpret errno from a successful/zero-byte syscall as its failure cause.
No production fix is accepted or imported from this delivery.

## Assigned next step: initiating renderer exit, not another framework

Antigravity coordinates the SAME implementer on OWN D: root branch
codex/chromium-renderer-native-exit and OWN Wine child of02d4ef2 named
codex/g4-renderer-native-exit. Existing combined token/graphics histories remain;
exclude rejected ECN accept-ignore stub. Do not duplicate the parallel docs-only
Wine API assessment; finish/deliver that report independently as already assigned.

1. Start from retained logs and source. Look for existing kernel fatal-signal,
   OOM or Crashpad evidence tied to this renderer's actual Linux identity, and
   for Wine protocol/abort paths before the server hangup. No new VM just to
   repeat the same observation. Map Wine PID/TID to Linux identity from the
   actual creation path, never by assuming equal IDs or adopting arbitrary PIDs.
2. If retained evidence is insufficient, add only the missing opt-in native
   exit/abort reason at the relevant Wine abort_thread/abort_process and caller
   paths, plus existing fatal-signal reporting where useful. Preserve status,
   errno, thread accounting and signal behavior; no allocating/stdio/backtrace
   machinery in signal handlers, no signal interception to keep a process alive.
   Prefer existing facilities; no global process observer or all-syscall trace.
   Correlate an actual pre-server-kill exit/fault with process identity. A later
   wineserver SIGQUIT/core is not initiating-fault evidence. If a kernel cause
   remains unavailable, say unknown rather than infer it from socket HUP.
3. Reuse OWN configured D-backed WSL build; incrementally build only changed
   components into a NEW matched private full client/server candidate. ONE fresh
   normal Chromium diagnostic is authorized with genuinely new exit evidence.
   Keep original HTTP fixture/security/features, current owned R4 disk/runtime,
   unique sockets/private loopback/actual VirGL, space/time/ownership guards and
   exact owned cleanup. No debugger attach/suspend, bypass flags, new image,
   reinstall/download, shared loaded build writes or master merge.
4. If the evidence proves a small real Wine defect, implement it on that own
   child branch, run the focused existing/real PE regression that exercises the
   behavior, and perform ONE fresh post-fix normal page verification without
   waiting for another paperwork approval. Otherwise stop with the precise new
   observation and smallest justified next action; do not repeat unchanged runs.

Use fresh stage/output/profile/helper paths. Preserve remaining attempts and
evidence; do not delete caches or old candidates to pass a space guard. Report
an actual space blocker before destructive cleanup. No harness redesign,
exhaustive conformance matrix, guessed API success or speculative DXIL fix.

Commit AND push root/Wine branches and original
docs/CHROMIUM_RENDERER_NATIVE_EXIT_RESULT_20261028.md. Report actual cause/status
and what remains unknown, HTTP/JS/readiness/front/native outcomes and shutdown;
collection success must remain separate. Reply to Codex then stop for review.
At95% five-hour use safely finish bounded cleanup/checkpoint/push; weekly99%
stop. Never consume reset credits. Actual Chromium functionality is the goal.
