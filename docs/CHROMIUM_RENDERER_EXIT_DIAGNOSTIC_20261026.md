<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# Parent acceptance and Antigravity next step: observe the killed renderer

## Accepted work

Reviewed14d25fcc/98e50300, importedc7f5fac0/e9893d9a. Parent independently
ran38 renderer/runtime/native/view tests under Debian:7.777s,0 failures/skips.
Read real mode/probe selection tests and changes. Both runtime-unblock and
renderer-unblock now select compositor prefix stripping and R6 metadata phase.
Four-module view/path/CLI wiring agrees. Original native helper artifact's AMD64
header/hash and output-only header verified; helper SHA
b8d03b0b01c4c60dee6eed3dc5c9dda2ce8fb86903b6b08427f7ce6be0e5d837.
Fixed new run directory remains absent. No new VM/browser executed by parent.

Report analysis remains tentative. Earlier renderer activity at times53/54/66
does not prove a process never exited later or that Mojo disconnected. Likewise
network_service_instance_impl's message alone does not prove the previous service
was in-process. Distinguish last observed alive from full lifetime/exit evidence.
The Wine trace signal(22,handler) is MSVCRT signal-handler registration, not an
observed delivered abort;22 is the MSVCRT SIGABRT value, not Linux SIGABRT.
These wording corrections need no independent conformance exercise.

Important parent source finding: in accepted clean Wine source
/var/tmp/wex-g4-ac-parent-20261026/wine/dlls/ntdll/unix/process.c,
NtTerminateProcess has NO TRACE statement. Therefore lack of a process-channel
TerminateProcess line cannot exclude termination. Existing +process/+seh logging
already ran; merely repeating those channels is not the useful next experiment.
The clean source also has syscall trace support in unix/syscall.c/server.c;
consider actual available observability before designing custom infrastructure.

## Assigned continuation, same owner/branch

Antigravity continues own D: codex/chromium-renderer-unblock. Accept wiring,
correct unsupported report claims in next report, and perform ONE targeted fresh
renderer-unblock diagnostic that adds genuinely new child-exit/termination or
specific IPC failure evidence. Use the prepared output identity if still absent.
Do not repeat an identical prior control or start speculative token/API patches.

Choose the simplest sufficient observation: existing Wine/server tracing when
it actually exposes needed calls/status, or a small read-only owned Chromium
child-exit observer. Record actual PID/parent/role and observed exit status/timing
of content renderers and network service, separate extension renderer. Retain
browser sad-tab time, HTTP outcome and front image. If a relevant process stays
alive, say so only for its observed interval; if terminal status is unavailable,
report unknown. No inference of Mojo disconnection solely from a sad-tab label.

Avoid huge generic logs and new frameworks. Drain logs under a reasonable bounded
private D: capture; preserve useful tail/failure on quota, not an assertion that
truncated evidence proves absence. Small observer/output-only helper changes and
focused tests are allowed if needed, with no debugger attachment/suspend/privilege
or browser-security/feature change. Reuse accepted PE when no helper change is
needed. No exhaustive test matrix or extra offline review gate before this one
authorized diagnostic. Keep original HTTP fixture/eight args and normal sandbox.

Same accepted metadata DLLs, guest/runtime/existing D: R4 disk. Metadata probe
must actually run under corrected mode; distinguish its result from child outcome.
No runtime download/reinstall/new image/other worker checkout or loaded-build edit.
Fresh staging/output, matched helper paths, unique sockets/privateLO/actualVirGL,
normal current time/space/ownership guards and exact owned cleanup retained.
No timeout inflation or second run in this order. At95%five-hour finish bounded
cleanup/checkpoint/push; weekly99%stop. Never reset credits.

Prefer no Wine rebuild. If adequate observation genuinely requires a Wine source
change/build rather than existing trace facilities, report the tiny proposed
instrumentation first instead of modifying shared/dirty builds. This is an
evidence decision, not permission to add unrelated API behavior. Existing Token
Security integration remains authorized once actual failure supports that path;
do not build it solely because a handle-table stub appears in logs.

Commit AND push focused source/tests/report docs/CHROMIUM_RENDERER_EXIT_RESULT_20261026.md,
reply codex with actual new evidence and smallest supported next fix, then stop.
No master merge. Prioritize diagnosing the real page failure, not harness polish.
