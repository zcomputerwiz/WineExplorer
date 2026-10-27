<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent review and Antigravity next task: network-service bootstrap

## Resource measurement reviewed, scoped result accepted

Read97922ec5 probe source/report and actual memory-probe.json/measure-summary.json.
Real candidate API returned success, physical/available-commit about3428MiB,
load12%; no code change or browser replay. Accept that idle-state measurement;
do not call it a measurement of Chromium's evaluator during the failing run.
No evidence here justifies a memory API patch or RAM comparison. Move on.

Correct report overstatements in the next report, no extra memory test cycle:
-3428MiB is GREATER than2GiB, so the stated early-exit predicate is true.
-3428MiB is below both bracketing MemAvailable samples3600/3453MiB, not between
 them. Different sample times/Wine warmup can account for a small difference;
 the arithmetic does not prove exact mapping at the same instant.
-Idle measurements do not conclusively exclude load-dependent pressure, but
 they provide no positive reason to pursue memory instead of actual bootstrap.
-The prior journal wait was45000ms, not220seconds (220 was a Windows PID).

## Actual pinned source narrows the network candidate

Parent read exact7d9824099030f5c28c939ba08654562faaad7c09 source:
https://chromium.googlesource.com/chromium/src/+/7d9824099030f5c28c939ba08654562faaad7c09/content/browser/network_service_instance_impl.cc
Lines629–662 enter when the remote is not bound or not connected. The652 error
logs when service_was_bound in the OUT-OF-PROCESS branch, immediately before
ServiceProcessHost::Launch. It is not a captured crash signature or evidence
that a prior in-process service faulted. A disconnected Mojo remote/spawn or
handoff failure remains possible. Do not label the earlier0.9s message a proven
process death. No faulting PC/status has yet been established.

## Assigned task: find and repair the real IPC/bootstrap failure

Use own NEW D: root codex/chromium-network-bootstrap from resource branch97922ec5,
retain existing combined Winefffa7de/full compatible build. NEW own Wine child
branch codex/g4-network-bootstrap only if actual instrumentation/fix is needed.
No token reimplementation, unrelated UAC work or generic observer/harness project.

1. Read retained combined-build Chromium/Wine logs and pinned ServiceProcessHost/
   utility-child/Mojo Windows platform-channel startup paths. Identify the actual
   first network-service spawn attempt and receiver handoff/disconnect sequence.
   Map browser/network child roles and observed result; don't infer from a warning.
   Distinguish failing process creation, natural termination, policy kill and a
   live process whose channel never connects. Absence from a trace without the
   corresponding tracepoint does not exclude a call.
2. Prefer existing supported targeted Chromium VLOG/vmodule and real Wine trace
   facilities that reveal the needed return statuses. Logging-only verbosity
   switches are allowed; normal sandbox/features stay. Do not repeat identical
   process+seh logs and expect new exit evidence. No global /proc observer.
3. If adequate evidence requires tiny Wine diagnostics, this order authorizes
   semantically inert, narrowly targeted lifecycle/IPC tracepoints on YOUR child
   branch and incremental OWN compatible build: e.g. caller/target/exit status
   in NtTerminateProcess/server termination and relevant Windows named-pipe or
   handle-transfer failure statuses. Keep logs private/bounded and do not add
   fake API successes or broad security changes. No modifying shared/loaded
   build. Do not spend another round asking to approve a few trace lines.
4. If retained/source evidence identifies a small real Wine incompatibility,
   implement the real fix with focused functional tests and use ONE fresh
   normal Chromium comparison. Otherwise execute ONE targeted fresh bootstrap
   diagnostic with genuinely new status/IPC evidence, then report the smallest
   supported fix; no second run in this order. Goal is get the page through
   networking/rendering, not perfect instrumentation or speculative API coverage.

Reuse current owned D:R4 disk/runtime/combined build, no full rebuild unless
required by actual changed component. Matched client/server from candidate,
fresh stage/output/profile/helper paths, preserve ALL consumed attempts. No
deletion/reuse of old stages, new image/download/reinstall. Check space before
transfer, original HTTP fixture, privateLO/unique sockets/actualVirGL, native wait
and whole deadlines/cleanup reserve/ownership/exact owned shutdown retained.
No debugger/attach/suspend, sandbox/feature workaround flags, in-process-network
switch, memory/discard disabling or master merge. Logging-only switches must be
recorded as the diagnostic variable, not successful security proof.

Commit AND push original code/tests and docs/CHROMIUM_NETWORK_BOOTSTRAP_RESULT_20261027.md,
plus OWN Wine branch if modified. Report actual process/IPC evidence, justified
fix or specific remaining failure, native/HTTP HTML+JS/readiness/front outcome,
and shutdown. Screenshots/runtime/private logs remain D:/D-backed WSL. At95%
five-hour bounded cleanup/checkpoint/push/defer; weekly99%stop, nevercredits.
Reply codex then stop for parent review. Actual Chromium functionality first.
