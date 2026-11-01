<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review: distinguish control-channel loss from a renderer crash

## Review of002e5b49 / Winea91ceea

Parent read the actual tracepoint source, server callers and retained
helper.stderr.log. Independently33 host tests pass6.508s,0 skips. Tracepoints
are a useful narrow diagnostic; no ECN success stub is present. Do not interpret
violent_death=1 as a captured unhandled signal or exception.

In the exact candidate source server/process.c process_poll_event:
    if (event & (POLLERR | POLLHUP))
        kill_process(process, !process->is_terminating);
Other kill_process(...,1) callers are in server/request.c FD recvmsg/sendmsg
protocol-error paths. kill_process(...,1) itself invokes
terminate_process(process,NULL,1). Thus this boolean can mean wineserver chose
to terminate after unexpected control-socket loss; it does NOT contain a Linux
wait status, exception code or faulting instruction. The report's definitive
unhandled-crash conclusion and request to find its RIP are premature.

Actual observed useful sequence is server kill_process(0x268,1), forced exit1,
then browser NT-terminate request for that process. This establishes earlier
server-side termination before browser's later request, not why it happened.
Parent found no Protocol error line around this event in retained helper log.
The poll-event path is a strong candidate, not yet a directly recorded event.
Network-service crashed log still doesn't prove a separate utility death.
Preserve this correction in the next report; no report-polish round needed.

## Next task: exact control-socket failure and the smallest real fix

Antigravity continues OWN D: root codex/chromium-termination-trace or NEW own
codex/chromium-control-channel, OWN Wine child ofa91ceea (basefffa7de plus inert
diagnostics, excluding rejected ECN stub). Reuse current configured own WSL
incremental build and matched private candidate. No full rebuild/new harness.

1. Add the missing tiny opt-in causal trace at process_poll_event: target Wine
   PID, event flags, is_terminating, server Unix fd and available peer/process
   identity. In server/request.c recvmsg/sendmsg error paths, record actual
   syscall result/errno and relevant target before calling kill_process. Save
   errno before logging; do not alter request/error handling or skip kills.
   Use the existing WEX_TERM_TRACE switch; no new generic protocol/framework.
2. Correlate with client-side control-channel/socket initialization and shutdown
   in ntdll/unix/server.c. Where an existing close/error path can explain the
   unexpected channel loss, add a tiny matching trace (owned Linux PID/fd/reason/
   errno). Prefer evidence already available in source over broad syscall logs.
   Don't add global/proc scanning or all-syscall/winsock/relay tracing.
3. If actual original signal/exit evidence exists in retained kernel/Crashpad/core
   data, inspect it read-only/offline. Optional scoped fault logging/core capture
   inside the disposable owned guest is allowed if simple. No debugger attachment,
   suspend, security bypass or global host crash-policy change. A core induced
   by wineserver's later SIGQUIT is not proof of the initiating client fault.
4. Run ONE fresh normal Chromium control with that genuinely new causal evidence.
   If it proves a small actual Wine socket/protocol/lifetime bug, fix that behavior
   with a focused real regression and ONE additional fresh post-fix page verification
   is authorized without a paperwork pause. No unchanged repeated diagnostics,
   no silencing error/status checks, fake API success or guessing a crash site.
   If no supported fix appears, report the exact new event/error and stop.

Keep original HTTP fixture and browser sandbox/features; native held-handle
journal, actual fixture requests/readiness/front and before-cleanup timeline.
Current own D:R4 disk/runtime/fullcombinedclientserver, unique sockets/privateLO/
actualVirGL/space/idleness/time bounds/cleanup reserve/exact owned shutdown stay.
Fresh stage/output/profile/helper paths, no consumed evidence clearing/reuse,
new image/download/reinstall/shared loaded build writes/master merge/other VMs.

Commit AND push original root/Wine changes and
docs/CHROMIUM_CONTROL_CHANNEL_RESULT_20261027.md with actual event/errno/status,
classification, supported fix/test and page outcome. All private logs/art/runtime
D:/D-backed WSL.95%five-hour safe bounded cleanup/checkpoint/push,weekly99%stop,
nevercredits. Reply codex then stop. The goal remains getting Chromium's page
to survive and load, not turning an internal boolean into a crash narrative.
